/*
Avi2 - Copyright (c) 2025 by Dennis Hawkins. All rights reserved.

BSD License

Redistribution and use in source and binary forms are permitted provided
that the above copyright notice and this paragraph are duplicated in all
such forms and that any documentation, advertising materials, and other
materials related to such distribution and use acknowledge that the
software was developed by the copyright holder. The name of the copyright
holder may not be used to endorse or promote products derived from this
software without specific prior written permission.  THIS SOFTWARE IS
PROVIDED `'AS IS? AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE.

Although not required, attribution is requested for any source code
used by others.
*/

// avi2_write.c
// Functions for writing AVI and AVI2 files.
// This file is written in ANSI C.

#include "avi2.h"
#include <math.h>

typedef struct
{
    int num;
    int den;
} FRACTION;

// Helper function prototypes
static int  AllocateIndex(INDEX_ROOT *rt);
static int  CheckFileLimit(AVI2 *avi, long payload_size);
static int  CloseCurrentRIFFSegment(AVI2 *avi);
static int  StartNewRIFFSegment(AVI2 *avi);
static int  WriteSegmentIndexes(AVI2 *avi);
static int  WriteLegacyIndex(AVI2 *avi);
static int  WriteHeaders(AVI2 *avi);
static void WriteAVIMainHeader(AVI2 *avi);
static void WriteVideoStreamHeaders(AVI2 *avi,int);
static void WriteAudioStreamHeaders(AVI2 *avi);
static void WriteODMLHeader(AVI2 *avi);
static void WriteINFOList(MFILE *fp);
static void WriteVideoPropHeader(AVI2 *avi);
static int  find_gcd(int a, int b);
static FRACTION    get_fps_strict(double fps);

// Standard GCD for reduction
// Find the greatest common denominator

static int find_gcd(int a, int b)
{
    int temp;

    while (b != 0)
    {
        temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}


// Convert fps into standard fraction

static FRACTION get_fps_strict(double fps)
{
    FRACTION res;
//    double integral_part;
    double rounded_fps;
    int common;

    fps = fabs(fps);   // positive values only
    rounded_fps = floor(fps + 0.5); // Round nearest whole
 //   modf(fps, &integral_part); // integral_part populated but we primarily use rounded_fps

    // Case 1: Whole numbers (PAL 25, 50, or clean 15, 30, etc.)
    if (fabs(fps - rounded_fps) < 0.001)
    {
        res.num = (int)rounded_fps;
        res.den = 1;
        return res;
    }

    // Case 2: Strict NTSC Logic (Industry Standard 1000/1001)
    res.num = (int)(rounded_fps * 1000.0);
    res.den = 1001;

    // Check if the 1001 scale is actually accurate for this input
    // If the difference is more than 0.01 FPS, it's probably not NTSC.
    if (fabs(fps - ((double)res.num / res.den)) > 0.01)
    {
        // Fallback: Scale to 1000 and reduce
        res.num = (int)floor(fps * 1000.0 + 0.5);
        res.den = 1000;

        common = find_gcd(res.num, res.den);
        if (common == 0) common = 1;  // Prevent division by zero
        res.num /= common;
        res.den /= common;
    }

    return res;
}


// Helper function to allocate or expand index arrays
// Returns 0 on success and index will hold one more entry,
// Returns error code on failure.  Note this function cannot change AVIerr.

static int AllocateIndex(INDEX_ROOT *rt)
{
    MEMINDEXENTRY *newIdx;
    DWORD newSize;                                // bytes
    DWORD allocated = rt->idx_blocks * INDEX_BLOCK_SIZE;  // entries

    if (allocated >= rt->index_entries + 1)
        return(AVIERR_NO_ERROR);  // Already have enough space

    // Expand or initialize index array
    rt->idx_blocks++;

    // Check Buffer Overflow
    if (rt->idx_blocks > (DWORD_MAX / INDEX_BLOCK_SIZE / sizeof(MEMINDEXENTRY)) )
        return(AVIERR_OVERFLOW);

    newSize = rt->idx_blocks * INDEX_BLOCK_SIZE * sizeof(MEMINDEXENTRY); // bytes
    newIdx = realloc(rt->Idx, newSize);
    if (!newIdx)  // failed
    {   // The previous memory block is still allocated and must be freed
        if (rt->Idx) free(rt->Idx);
        rt->Idx = NULL;
        return(AVIERR_MALLOC);
    }
    rt->Idx = newIdx;

    return(AVIERR_NO_ERROR);
}


// Check if writing payload_size bytes plus two indexes would exceed
// the 2GB limit for legacy mode (hard limit).  For ODML mode, it
// only checks if the payload itself would exceed 1GB (soft limit).
// Returns 1 (TRUE) if room to write, 0 (FALSE) if would exceed limit

static int CheckFileLimit(AVI2 *avi, long payload_size)
{
    DWORD current_pos;
    DWORD legacy_index_size;
    DWORD bytes_needed;

    // Get current file position
    current_pos = File64GetPos(avi->fp);

    if (avi->ODMLmode == STRICT_LEGACY)
    {
        // Calculate size of legacy index that will be written
        // Each index entry is 16 bytes
        legacy_index_size = 8;  // 'idx1' + size
        legacy_index_size += sizeof(AVIINDEXENTRY) *
            (avi->VidRt.index_entries + avi->AudRt.index_entries + 1);

        // odml index is not written in legacy mode

        // Calculate total file size after this write
        bytes_needed = current_pos + 8 + payload_size + legacy_index_size;

        // Check against 2GB limit (0x7FFFFFF0 is max signed 32-bit value)
        if (bytes_needed >= AVI_MAX_RIFF_SIZE)
            return(FALSE);  // Would exceed limit

    }
    else   // strict odml or hybrid
    {
        // In ODML mode, the limit is a soft 1GB.
        if (current_pos + payload_size > 0x40000000)   // 1GB
            return(FALSE);   // over
    }
    return(TRUE);  // Safe to write
}


// Write ODML index helper
// Write ODML index at the current file location.
// Add an entry to the corresponding SUPER INDEX.
// Return 0 if OK, else error code.

static int WriteODMLIndexHelper(AVI2 *avi, DWORD Stream)
{
    INDX_CHUNK idxChunk;
    INDEX_ROOT *rt;
    STDINDEXENTRY stdEntry;
    SUPERINDEXENTRY supEntry;
    QWORD IndexPtr;
    DWORD i, size, cnt, save_fp, NewOffset, AudByteCtr = 0;
    char StreamText[8];
    FOURCC fcc;

    if (Stream > 99)    // no more than 99 streams allowed
    {
        return(avi->AVIerr = AVIERR_STREAM_INVALID);
    }
    rt = (Stream == 0) ? &avi->VidRt : &avi->AudRt;
    sprintf(StreamText, "%02u%.2s", Stream, Stream == 0 ? "dc" : "wb");
    fcc = *((FOURCC *) StreamText);

    // Get an absolute pointer to the start of the index
    IndexPtr = File64GetBase(avi->fp) + File64GetPos(avi->fp);

    // Write odml index ix<stream num>
    if (rt->index_entries > 0)
    {
        // Write ix## fourcc
        WriteFCC(avi->fp, 'ix##', Stream);

        // Calculate size: INDX_CHUNK + entries
        if (rt->index_entries > (DWORD_MAX - sizeof(INDX_CHUNK)) / sizeof(STDINDEXENTRY))
           return(avi->AVIerr = AVIERR_OVERFLOW);
        size = sizeof(INDX_CHUNK) +
                (rt->index_entries * sizeof(STDINDEXENTRY));
        WriteDWORD(avi->fp, size);

        // Fill INDX_CHUNK header
        idxChunk.wLongsPerEntry = sizeof(STDINDEXENTRY) / 4;
        idxChunk.bIndexSubType = AVI_INDEX_STANDARD;
        idxChunk.bIndexType = AVI_INDEX_OF_CHUNKS;
        idxChunk.nEntriesInUse = rt->index_entries;
        idxChunk.dwChunkId = FIX_LIT(fcc);
        // the base address for indexes always points to the 'm' in 'movi'
        idxChunk.qwBaseOffset = File64GetBase(avi->fp) + avi->movi_start - 4;
        idxChunk.dwReserved = 0;

        cnt = File64Write(avi->fp, &idxChunk, sizeof(INDX_CHUNK));

        // Write index entries
        for (i = 0; i < rt->index_entries; i++)
        {
            memcpy(&stdEntry, &rt->Idx[i], sizeof(MEMINDEXENTRY));
            NewOffset = stdEntry.dwOffset - avi->movi_start + 4;
            stdEntry.dwOffset = NewOffset;
            stdEntry.dwSize &= 0x80FFFFFF;  // mask out base index
            AudByteCtr += stdEntry.dwSize;  // only used for audio
            cnt += File64Write(avi->fp, &stdEntry, sizeof(STDINDEXENTRY));
        }

        if (cnt != sizeof(INDX_CHUNK) + rt->index_entries * sizeof(STDINDEXENTRY))
        {
            // Failed to write all the data
            return(avi->AVIerr = AVIERR_CANT_WRITE_FILE);
        }

        // Now go back and write the superindex entry
        save_fp = File64GetPos(avi->fp);  // save where we are at
        supEntry.qwOffset = IndexPtr;
        supEntry.dwSize = size + 8;
        supEntry.dwDuration = rt->index_entries;  // for video only
        if (Stream != 0)   // audio track is calculated differently
        {
            // dwDuration = Total Bytes of Audio in Sub-Index / nBlockAlign
            if (avi->Aud.nBlockAlign == 0) avi->Aud.nBlockAlign = 1;
            supEntry.dwDuration = AudByteCtr / avi->Aud.nBlockAlign;
        }

        // Seek to superindex entry location
        // We used Qseek because it doesn't disturb the base pointer
        File64Qseek(avi->fp, (QWORD) rt->SuperIdxOffset);
        cnt = File64Write(avi->fp, &supEntry, sizeof(SUPERINDEXENTRY));
        rt->SuperIdxOffset += sizeof(SUPERINDEXENTRY);

        // Return to our previous position
        File64SetPos(avi->fp, save_fp, SEEK_SET);  // save where we are at

        if (cnt != sizeof(SUPERINDEXENTRY))
        {
            // Failed to write all the data
            return(avi->AVIerr = AVIERR_CANT_WRITE_FILE);
        }
    }

    return(0);
}


// Write ODML standard indexes (ix00, ix01) for current RIFF segment
// Return 0 if OK, or error code.

static int WriteSegmentIndexes(AVI2 *avi)
{

    int ret;

    // Write video index ix00
    if (avi->has_video)
    {
        ret = WriteODMLIndexHelper(avi, 0);
        if (ret) return(ret);
    }

    // Write audio index ix01
    if (avi->has_audio)
    {
        ret = WriteODMLIndexHelper(avi, 1);
        if (ret)return(ret);
    }

    return(0);
}


// Write legacy index (idx1) for first RIFF segment
// Returns 0 if OK, else error code.

static int WriteLegacyIndex(AVI2 *avi)
{
    AVIINDEXENTRY entry;
    DWORD i, vidIdx, audIdx, size;
    DWORD totalEntries;
    DWORD vidOffset, audOffset;

    totalEntries = avi->VidRt.index_entries + avi->AudRt.index_entries;
    if (totalEntries == 0)
        return 0;

    // Write 'idx1' fourcc
    WriteFCC(avi->fp, 'idx1', 0);

    // Write size
    size = totalEntries * sizeof(AVIINDEXENTRY);
    WriteDWORD(avi->fp, size);


    // Legacy indexes are combined.
    // Merge video and audio indexes in the order they were written
    vidIdx = 0;
    audIdx = 0;

    for (i = 0; i < totalEntries; i++)
    {
        // The offset is already relative to movi_start and
        // pointing to the video data.  We need to subtrace 8
        // To make it point to the FourCC.

        vidOffset = (vidIdx >= avi->VidRt.index_entries) ? 0xFFFFFFFF :
                      avi->VidRt.Idx[vidIdx].dwOffset;
        audOffset = (audIdx >= avi->AudRt.index_entries) ? 0xFFFFFFFF :
                      avi->AudRt.Idx[audIdx].dwOffset;

        // Write whichever came first in the file
        if (vidOffset < audOffset)
        {
            // Write Video Chunk
            size = avi->VidRt.Idx[vidIdx].dwSize;
            entry.ckid = FIX_LIT('00dc');
            entry.dwFlags = (size & 0x80000000) ? 0: AVIIF_KEYFRAME;
            entry.dwChunkOffset = vidOffset - avi->movi_start - 4;
            entry.dwChunkLength = GET_CHUNK_SIZE(size);
            vidIdx++;
        }
        else
        {
            // Write Audio Chunk
            size = avi->AudRt.Idx[audIdx].dwSize;
            entry.ckid = FIX_LIT('01wb');
            entry.dwFlags = AVIIF_KEYFRAME;  // Audio chunks are always keyframes
            entry.dwChunkOffset = audOffset - avi->movi_start - 4;    // point to '00dc'
            entry.dwChunkLength = GET_CHUNK_SIZE(size);
            audIdx++;
        }

        if (File64Write(avi->fp, &entry, sizeof(AVIINDEXENTRY)) != sizeof(AVIINDEXENTRY))
            return(avi->AVIerr = AVIERR_CANT_WRITE_FILE);
    }


    return 0;
}


// Close current RIFF segment
// Called after the last MOVI chunk.
// We assume that the Base file pointer is set to point to the 'R' in 'RIFF'.
// Return 0 if OK, else error code.

static int CloseCurrentRIFFSegment(AVI2 *avi)
{
    DWORD  moviSize;
    int ret;
    DWORD finalPos;
    DWORD EndMoviPos;

    // Write ODML indexes if in ODML or hybrid mode
    // ODML indexes are not written in STRICT_LEGACY mode
    if (avi->ODMLmode != STRICT_LEGACY)
    {
        ret = WriteSegmentIndexes(avi);
        if (ret != 0)
            return ret;
    }


    // Go back and fix MOVI length
    // Get the position of the end of the movi and all odml indexes.
    EndMoviPos = File64GetPos(avi->fp);
    moviSize = EndMoviPos - avi->movi_start + 4;  // +4 to include 'movi' itself
    // seek to the movi list size.
    File64SetPos(avi->fp, avi->movi_start - 8, SEEK_SET);
    WriteDWORD(avi->fp, moviSize);
    File64SetPos(avi->fp, EndMoviPos, SEEK_SET);  // back to present

    // Write legacy index if this is the first segment
    // Legacy index is not written for strict odml
    if (avi->ODMLmode != STRICT_ODML && avi->NumBases == 1)
    {
        ret = WriteLegacyIndex(avi);
        if (ret != 0)
            return ret;
    }

    // Go back and fix RIFF length
    finalPos = File64GetPos(avi->fp);    // Get final RIFF length
//    avi->current_riff_size = finalPos ;
    File64SetPos(avi->fp, 4, SEEK_SET);  // seek to RIFF length
    WriteDWORD(avi->fp, finalPos - 8);   // Length of current RIFF
    File64SetPos(avi->fp, finalPos, SEEK_SET);  // back to present

    // Reset index counters for next segment
    avi->VidRt.index_entries = 0;
    avi->AudRt.index_entries = 0;

    return 0;
}


// Start a new RIFF-AVIX segment
// Change base pointer to point to current position.
// This is never the first segment.

static int StartNewRIFFSegment(AVI2 *avi)
{
    QWORD RiffPos;    // absolute RIFF start

    // Set the new base pointer to the curent location
    if (avi->NumBases >= MAX_RIFF)
        return(avi->AVIerr = AVIERR_TOO_MANY_SEGMENTS);

    RiffPos = File64GetBase(avi->fp);  // get old base
    RiffPos += File64GetPos(avi->fp); // Add offset to get absolute addr
    File64SetBase(avi->fp, RiffPos);   // set new base file pointer
    avi->BaseTable[avi->NumBases++] = RiffPos;


    // Write RIFF header
    WriteFCC(avi->fp, 'RIFF', 0);
    WriteDWORD(avi->fp, 0);  // Size - will fix later
    WriteFCC(avi->fp, 'AVIX', 0);    // AVIX segment

    // Write movi LIST header
    WriteFCC(avi->fp, 'LIST', 0);
    WriteDWORD(avi->fp, 0);  // Size - will fix later
    WriteFCC(avi->fp, 'movi', 0);
    avi->movi_start = File64GetPos(avi->fp);  // points to '00dc'

//    avi->current_riff_size = 0;

    return 0;
}


// Write video property header
// Write 'vprp'

static void WriteVideoPropHeader(AVI2 *avi)
{
    MFILE *fp = avi->fp;
    VideoPropHeader vprp;
    VIDEO_FIELD_DESC field;
    DWORD size, gcd;

    // Calculate size
    size = sizeof(VideoPropHeader) + sizeof(VIDEO_FIELD_DESC);

    WriteFCC(fp, 'vprp', 0);
    WriteDWORD(fp, size);


    // Find the Greatest Common Divisor
    gcd = find_gcd(avi->width, avi->height);

    // Fill video property header
    vprp.VideoFormatToken = 0;
    vprp.VideoStandard = 0;
    vprp.dwVerticalRefreshRate = (DWORD)(avi->fps + 0.5);
    vprp.dwHTotalInT = 0;
    vprp.dwVTotalInLines = avi->height;
    vprp.dwFrameAspectRatio = ((avi->width / gcd) << 16) | (avi->height / gcd);
    vprp.dwFrameWidthInPixels = avi->width;
    vprp.dwFrameHeightInLines = avi->height;
    vprp.nbFieldPerFrame = 1;  // Progressive

    File64Write(fp, &vprp, sizeof(VideoPropHeader));

    // Fill field descriptor
    field.CompressedBMHeight = avi->height;
    field.CompressedBMWidth = avi->width;
    field.ValidBMHeight = avi->height;
    field.ValidBMWidth = avi->width;
    field.ValidBMXOffset = 0;
    field.ValidBMYOffset = 0;
    field.VideoXOffsetInT = 0;
    field.VideoYValidStartLine = 0;

    File64Write(fp, &field, sizeof(VIDEO_FIELD_DESC));
}


// Write INFO list
// Write 'LIST' -> 'INFO' -> 'ISFT'

static void WriteINFOList(MFILE *fp)
{
    char software[256];
    DWORD size, totalSize;

    size = 1 + sprintf(software, "AVI 2.0 (ODML) Library by Dennis Hawkins v%s", AVI2_LIB_VERSION);

    if (NEED_PAD_EVEN(size)) size++;

    totalSize = 12 + size;  // 'ISFT' + size + data

    WriteFCC(fp, 'LIST', 0);
    WriteDWORD(fp, totalSize);
    WriteFCC(fp, 'INFO', 0);

    WriteFCC(fp, 'ISFT', 0);
    WriteDWORD(fp, size);
    File64Write(fp, software, size);
}


// Write ODML extended header
// This header has the true number of frames for entire file.
// Write 'LIST' -> 'odml' -> 'dmlh'

static void WriteODMLHeader(AVI2 *avi)
{
    MFILE *fp = avi->fp;
    AVIEXTHEADER extHdr;
    DWORD size = 4 + 8 + sizeof(AVIEXTHEADER);

    WriteFCC(fp, 'LIST', 0);
    WriteDWORD(fp, size);
    WriteFCC(fp, 'odml', 0);

    WriteFCC(fp, 'dmlh', 0);
    WriteDWORD(fp, sizeof(AVIEXTHEADER));

    extHdr.dwTotalFrames = avi->num_video_frames;
    File64Write(fp, &extHdr, sizeof(AVIEXTHEADER));
}


// Write AVI main header
// Write 'avih'

static void WriteAVIMainHeader(AVI2 *avi)
{
    MFILE *fp = avi->fp;
    AVIMainHeader mainHdr;
    DWORD maxBytesPerSec = 0;

    // Calculate MaxBytesPerSec
    if (avi->has_video && avi->fps > 0)
    {
        maxBytesPerSec = (DWORD)(avi->max_video_frame_size * avi->fps);
    }
    if (avi->has_audio)
    {
//        maxBytesPerSec += avi->Aud.nAvgBytesPerSec;
    }

    mainHdr.MicroSecPerFrame = (avi->fps > 0) ? (DWORD)(1000000.0 / avi->fps) : 0;
    mainHdr.MaxBytesPerSec = maxBytesPerSec;
    mainHdr.PaddingGranularity = 2;
    mainHdr.Flags = AVIF_HASINDEX | AVIF_ISINTERLEAVED;
    if (avi->ODMLmode != STRICT_LEGACY)
        mainHdr.Flags |= AVIF_TRUSTCKTYPE;
    mainHdr.TotalFrames = avi->num_video_frames;
    mainHdr.InitialFrames = 0;
    mainHdr.NumStreams = avi->has_video + avi->has_audio;
    mainHdr.SuggestedBufferSize =
        avi->max_video_frame_size + avi->max_audio_chunk_size;
    mainHdr.Width = avi->width;
    mainHdr.Height = avi->height;
    memset(mainHdr.Reserved, 0, sizeof(mainHdr.Reserved));

    WriteFCC(fp, 'avih', 0);
    WriteDWORD(fp, sizeof(AVIMainHeader));
    File64Write(fp, &mainHdr, sizeof(AVIMainHeader));
}



// Audio Stream Header Helper

static DWORD WriteVids(AVI2 *avi)
{
    FRACTION res;
    MFILE *fp = avi->fp;
    AVIStreamHeader56 strh;
    STREAMFORMATVID strf;
    DWORD ret;

    // Write strh
    WriteFCC(fp, 'strh', 0);
    WriteDWORD(fp, sizeof(AVIStreamHeader56));

    res = get_fps_strict(avi->fps);

    strh.fccType = FIX_LIT('vids');
    strh.fccHandler = avi->VideoCodec;
    strh.Flags = 0;
    strh.Priority = 0;
    strh.Language = 0;
    strh.InitialFrames = 0;
    strh.TimeScale = res.den;
    strh.Rate = res.num;
    strh.StartTime = 0;
    strh.Length = avi->num_video_frames;
    strh.SuggestedBufferSize = avi->max_video_frame_size;
    strh.Quality = 0xFFFFFFFF;
    strh.SampleSize = 0;
    strh.Frame.Left = 0;
    strh.Frame.Top = 0;
    strh.Frame.Right = (short)avi->width;
    strh.Frame.Bottom = (short)avi->height;

    File64Write(fp, &strh, sizeof(AVIStreamHeader56));

    // Write strf
    WriteFCC(fp, 'strf', 0);
    WriteDWORD(fp, sizeof(STREAMFORMATVID));

    strf.header_size = sizeof(STREAMFORMATVID);
    strf.biWidth = avi->width;
    strf.biHeight = avi->height;
    strf.biPlanes = 1;
    strf.bits_per_pixel = 24;
    strf.biCompression = avi->VideoCodec;
    strf.biSizeImage = avi->width * avi->height * 3;
    strf.biXPelsPerMeter = 0;
    strf.biYPelsPerMeter = 0;
    strf.biClrUsed = 0;
    strf.biClrImportant = 0;

    // If there was an error on earlier writes,
    // it will be the same for this one.
    // No need to check for errors on all.
    ret = File64Write(fp, &strf, sizeof(STREAMFORMATVID));

    return(ret);
}


// Video Stream Header Helper

static DWORD WriteAuds(AVI2 *avi)
{
    MFILE *fp = avi->fp;
    AVIStreamHeader56 strh;
    DWORD ret;

    // Write strh
    WriteFCC(fp, 'strh', 0);
    WriteDWORD(fp, sizeof(AVIStreamHeader56));

    strh.fccType = FIX_LIT('auds');
    strh.fccHandler = 0;
    strh.Flags = 0;
    strh.Priority = 0;
    strh.Language = 0;
    strh.InitialFrames = 0;
    strh.TimeScale = avi->Aud.nBlockAlign;
    strh.Rate = avi->Aud.nAvgBytesPerSec;
    strh.StartTime = 0;
    strh.Length = avi->num_audio_frames * avi->Aud.nBlockAlign;
    strh.SuggestedBufferSize = avi->max_audio_chunk_size;
    strh.Quality = 0xFFFFFFFF;
    strh.SampleSize = avi->Aud.nBlockAlign;
    strh.Frame.Left = 0;
    strh.Frame.Top = 0;
    strh.Frame.Right = 0;
    strh.Frame.Bottom = 0;

    File64Write(fp, &strh, sizeof(AVIStreamHeader56));

    // Write strf (WAVEFORMATEX)
    WriteFCC(fp, 'strf', 0);
    WriteDWORD(fp, sizeof(STREAMFORMATAUD));

    avi->Aud.wFormatTag = (WORD)avi->AudioCodec;
//    avi->Aud.nBlockAlign = (WORD)((avi->Aud.nChannels * avi->Aud.wBitsPerSample) / 8);
    avi->Aud.nAvgBytesPerSec = avi->Aud.nSamplesPerSec * avi->Aud.nBlockAlign;
    avi->Aud.cbSize = 0;

    // If there was an error on earlier writes,
    // it will be the same for this one.
    // No need to check for errors on all.
    ret = File64Write(fp, &avi->Aud, sizeof(STREAMFORMATAUD));
    return(ret);
}





// Write video stream headers
// Write 'LIST' -> 'strl' -> ('strh' + 'strf' + 'indx' + 'vprp' + 'strn')

static void WriteStreamHeaders(AVI2 *avi, DWORD Stream)
{
    MFILE *fp = avi->fp;
    DWORD strlSize, startPos, endPos;
    INDEX_ROOT *rt;
    FOURCC CkId;

    // Mark start of strl LIST
    startPos = File64GetPos(fp);

    WriteFCC(fp, 'LIST', 0);
    WriteDWORD(fp, 0);  // Size - will fix later
    WriteFCC(fp, 'strl', 0);

    if (Stream == 0)    // video stream
    {
        WriteVids(avi);
        rt = &avi->VidRt;
        CkId = FIX_LIT('00dc');
    }
    else                // audio stream
    {
        WriteAuds(avi);
        rt = &avi->AudRt;
        CkId = FIX_LIT('00wb');
    }
//    NamePtr = rt->Name;

    // Jump over super indx if in ODML mode
    // This is written by the odml index writing function so we don't
    // touch this.  NumBases must be accurate and should be the number
    // of entries including the first one (starts at 1).  We have
    // previously reserved enough disk space for MAX_RIFF entries.  Mark
    // unused index entries as 'JUNK'.  This area is initialized to
    // all zeros.

    if (avi->ODMLmode != STRICT_LEGACY)
    {
        DWORD idxSize, maxSize;
        INDX_CHUNK idxChunk;

        maxSize = MAX_RIFF * sizeof(SUPERINDEXENTRY);
        idxSize = avi->NumBases * sizeof(SUPERINDEXENTRY);

        WriteFCC(fp, 'indx', 0);
        WriteDWORD(fp, sizeof(INDX_CHUNK) + idxSize);      // changes with each new segment

        // Write superindex header
        idxChunk.wLongsPerEntry = sizeof(SUPERINDEXENTRY) / 4;
        idxChunk.bIndexSubType = 0;
        idxChunk.bIndexType = AVI_INDEX_OF_INDEXES;
        idxChunk.nEntriesInUse = avi->NumBases;
        idxChunk.dwChunkId = CkId;
        idxChunk.qwBaseOffset = 0;
        idxChunk.dwReserved = 0;
        File64Write(fp, &idxChunk, sizeof(INDX_CHUNK));

        // Save location of first superindex entry
        rt->SuperIdxOffset = File64GetPos(avi->fp);

        // jump over index entries that should already be there
        File64SetPos(avi->fp, idxSize, SEEK_CUR);   // relative jump

        if (maxSize != idxSize)   // put a junk chunk to fill the gap
        {
            DWORD junkSize = maxSize - idxSize - 8;
            WriteFCC(fp, 'JUNK', 0);
            WriteDWORD(fp, junkSize);  // junk size

            // Jump over JUNK
            File64SetPos(avi->fp, junkSize, SEEK_CUR);   // relative jump
        }
    }  // if (isODML)

    // Write vprp
    if (Stream == 0) WriteVideoPropHeader(avi);  // only for video

    // Write stream name 'strn'
    // No padding needed since we use the entire 32 byte buffer
    // The 'sizeof(avi->VidRt.Name)' is the same for audio.
    WriteFCC(fp, 'strn', 0);
    WriteDWORD(fp, sizeof(rt->Name));   // Size
    File64Write(fp, rt->Name, sizeof(rt->Name));  // name

    // Fix strl LIST size
    endPos = File64GetPos(fp);
    strlSize = endPos - startPos - 8;
    File64SetPos(fp, startPos + 4, SEEK_SET);
    WriteDWORD(fp, strlSize);
    File64SetPos(fp, endPos, SEEK_SET);
}


// Write complete header structure at beginning of file
// This is called when all the streams are known, and
// again when closing the file.  Returns 0 or error code.

static int WriteHeaders(AVI2 *avi)
{
    DWORD hdrlSize, headerEnd;
    DWORD startPos, endPos;
    DWORD junkSize;
    MFILE *fp = avi->fp;

    // Seek to beginning
    File64SetPos(fp, 0, SEEK_SET);

    // Write RIFF header
    WriteFCC(fp, 'RIFF', 0);

    // skip over size for now
    // written by CloseCurrentRIFF()
    File64SetPos(fp, 4, SEEK_CUR);

    WriteFCC(fp, avi->ODMLmode == STRICT_ODML ? 'AVIX' : 'AVI ', 0);

    // Start hdrl LIST
    startPos = File64GetPos(fp);
    WriteFCC(fp, 'LIST', 0);
    WriteDWORD(fp, 0);  // Size - will fix later
    WriteFCC(fp, 'hdrl', 0);

    // Write main AVI header
    WriteAVIMainHeader(avi);

    // Write video stream headers
    WriteStreamHeaders(avi, 0);

    // Write audio stream headers
    if (avi->has_audio)
        WriteStreamHeaders(avi, 1);

    // Write ODML header if in ODML mode
    if (avi->ODMLmode != STRICT_LEGACY)
        WriteODMLHeader(avi);

    // Fix hdrl LIST size
    endPos = File64GetPos(fp);
    hdrlSize = endPos - startPos - 8;
    File64SetPos(fp, startPos + 4, SEEK_SET);
    WriteDWORD(fp, hdrlSize);
    File64SetPos(fp, endPos, SEEK_SET);

    // Write INFO list
    WriteINFOList(fp);

    // Calculate how much JUNK we need to reach movi_start
    headerEnd = File64GetPos(fp);
    if (headerEnd > avi->movi_start - 20)
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

    junkSize = avi->movi_start - headerEnd - 20;
    WriteFCC(fp, 'JUNK', 0);
    WriteDWORD(fp, junkSize);


    return 0;
}


// This internal function is called by the AVI_Close() function.
// Its purpose is to write any buffers and headers to the file
// prior to everything being closed down.

int FinalizeWrite(AVI2 *avi)
{
    int err;

    // Close current RIFF segment (write indexes, fix sizes)
    err = CloseCurrentRIFFSegment(avi);
    if (err) return(err);

    // Write all headers at beginning of file
    File64SetBase(avi->fp, 0);   // headers go at beginning
    err = WriteHeaders(avi);
    if (err) return(err);

    return(err);
}


// start the very first 'movi' LIST
// This gets called whenever the first chunk is added.

static void BeginMovi(AVI2 *avi)
{
    // Write movi LIST header
    WriteFCC(avi->fp, 'LIST', 0);
    WriteDWORD(avi->fp, 0);  // Size - will fix later
    WriteFCC(avi->fp, 'movi', 0);

    // Store movi base address
    avi->movi_start = File64GetPos(avi->fp);   // should be 0 up to this point

    WriteHeaders(avi);
    File64SetPos(avi->fp, avi->movi_start, SEEK_SET);
    avi->NumBases = 1;  // starting first base

    return;
}


// Add an internal memory index entry.
// Len is the length of the payload without headers.
// The system basefilepointer must be set to the start of the RIFF
// File pointer must point to start of data not FourCC.

int AddIndexEntry(AVI2 *avi, INDEX_ROOT *rt, DWORD len, DWORD Key)
{
    DWORD offset;
    int ret = AllocateIndex(rt);
    if (ret)
        return ret;

    // Calculate offset to add to index offset
    // Note that our offset points to the data not the '00dc'
//    offset = File64GetPos(avi->fp) - avi->movi_start + 12; // 4 + 8
    // In our memory index, the offset is offset only to the start of
    // the RIFF segment.
    offset = File64GetPos(avi->fp);

    rt->Idx[rt->index_entries].dwOffset = offset;  // Point to data
    rt->Idx[rt->index_entries].dwSize =
        MAKE_DWORD_CHUNK(len, avi->NumBases - 1, Key);
    rt->index_entries++;

    return(0);
}



// This function is called by the user to set the basic video parameters
// when creating an AVI file.  It must be called after opening the file
// in FOR_WRITING mode. The 4cc codec is fixed so multicharacter literals work.
// The IsODML boolean flag forces the file type.  If false, and the file
// exceeds 2GB, the library will continue to accept video and sound
// frames, but will ignore them and not actually write them to the file.
// If TRUE, the library will write files up to (MAX_RIFF) GB.  Each RIFF segment
// is limited to approximately 1GB.  The first segment is a hybrid RIFF
// segment that is labeled 'AVI ' and contains both ODML indexes and
// legacy avi indexes.  Subsequent RIFF segments are labeled 'AVIX'.
// Returns 0 if OK, else error code.

int AVI_SetVideo(AVI2 *avi, char *name, DWORD width, DWORD height,
                 double fps, FOURCC codec)
{
    BYTE filler[2048];


    if (!avi)
        return(AVIERR_AVI_STRUCT_BAD);

    avi->AVIerr = AVIERR_NO_ERROR;

    if (avi->filemode != FOR_WRITING)
        return(avi->AVIerr = AVIERR_WRONG_FILE_MODE);

    // Ultra large video formats not allowed here
    if (height > MAX_HEIGHT || width > MAX_WIDTH ||
        fps <= 0.0 || fps > MAX_FPS || codec == 0 || !name)
            return(avi->AVIerr = AVIERR_BAD_PARAMETER);

    // This function should not be called after chunks already added
    if (avi->VidRt.index_entries + avi->AudRt.index_entries != 0)
        return(avi->AVIerr = AVIERR_FUNCTION_ORDER);


    avi->width = width;
    avi->height = height;
    avi->fps = fps;
    avi->VideoCodec = FIX_LIT(codec);
    strncpy(avi->VidRt.Name, name, sizeof(avi->VidRt.Name));
    avi->VidRt.Name[sizeof(avi->VidRt.Name) - 1] = '\0';

    avi->has_video = TRUE;

    // Write reserved header space
    if (avi->ODMLmode != STRICT_LEGACY)
    {
        // Reserve additional space for video superindex
        memset(filler, 0, sizeof(filler));
        File64Write(avi->fp, filler, sizeof(filler));
    }

    return(0);
}




// Write a video frame to the file.
// VidBuf contains a buffer of video data exactly how it is to be
// written to the file.  Len is the length of the data in that buffer.
// If keyframe is true, the frame is marked as such in the index.
// This function does not do any compression.  It will also cause
// an associated index buffer to be written.
// Returns 0 if len buffer bytes were written, else error code.

int AVI_WriteVframe(AVI2 *avi, BYTE *VidBuf, DWORD len, int keyframe)
{
//    DWORD offset;
    int ret;


    if (!avi)
        return(AVIERR_AVI_STRUCT_BAD);

    avi->AVIerr = AVIERR_NO_ERROR;

    if (avi->filemode != FOR_WRITING)
        return(avi->AVIerr = AVIERR_WRONG_FILE_MODE);

    if (!avi->has_video)
        return(avi->AVIerr = AVIERR_MISSING_VIDEO);

    if (!VidBuf || len == 0)
        return(avi->AVIerr = AVIERR_BAD_PARAMETER);

    // Check legacy 2GB limit
    if (!CheckFileLimit(avi, len))
    {
        if (avi->ODMLmode != STRICT_LEGACY)
        {
            // File would be > 1GB for ODML
            ret = CloseCurrentRIFFSegment(avi);
            if (ret != 0)
                return ret;
            ret = StartNewRIFFSegment(avi);
            if (ret != 0)
                return ret;

        }
        else  // Silently ignore when legacy file has hit 2GB limit
            return 0;
    }

    if (avi->movi_start == 0)   // start the movi LIST
        BeginMovi(avi);

    // Write video chunk header first
    WriteFCC(avi->fp, '##dc', 0);
    WriteDWORD(avi->fp, len);

    // Add index entry to point to movi data
    ret = AddIndexEntry(avi, &avi->VidRt, len, keyframe);
    if (ret)
        return ret;

    // write movi data    
    File64Write(avi->fp, VidBuf, len);
    avi->num_video_frames++;


    // Pad after writing buffer - not included in LEN
    if (NEED_PAD_EVEN(len))
        File64Putchar(avi->fp, 0);

    // Track max frame size
    if ((DWORD) len > avi->max_video_frame_size)
        avi->max_video_frame_size = len;

    return 0;
}


// This function sets the audio parameters when creating a file.
// If the file is to have audio, this must be called after opening
// the file in FOR_WRITING mode.

int AVI_SetAudio(AVI2 *avi, char *name, int NumChannels, long SamplesPerSecond,
                 long BitsPerSample, long codec)
{
    BYTE filler[2048];


    if (!avi)
        return(AVIERR_AVI_STRUCT_BAD);

    avi->AVIerr = AVIERR_NO_ERROR;

    if (avi->filemode != FOR_WRITING)
        return(avi->AVIerr = AVIERR_WRONG_FILE_MODE);

    if (NumChannels > MAX_AUDIO_CHANNELS)
        return(avi->AVIerr = AVIERR_TOO_MANY_AUD_CHANNELS);

    if (BitsPerSample != 8 && BitsPerSample != 16 && BitsPerSample != 24 && BitsPerSample != 32)
       return(avi->AVIerr = AVIERR_BAD_PARAMETER);

   if (!name || SamplesPerSecond < 8000 || SamplesPerSecond > 192000 || NumChannels <= 0)
       return(avi->AVIerr = AVIERR_BAD_PARAMETER);

    if (!avi->has_video)
        return(avi->AVIerr = AVIERR_MISSING_VIDEO);

    // This function should not be called after chunks already added
    if (avi->VidRt.index_entries + avi->AudRt.index_entries != 0)
        return(avi->AVIerr = AVIERR_FUNCTION_ORDER);

    avi->Aud.nChannels = (WORD)NumChannels;
    avi->Aud.nSamplesPerSec = SamplesPerSecond;
    avi->Aud.wBitsPerSample = (WORD)BitsPerSample;
    avi->AudioCodec = codec;
    avi->has_audio = 1;

    strncpy(avi->AudRt.Name, name, sizeof(avi->AudRt.Name));
    avi->AudRt.Name[sizeof(avi->AudRt.Name) - 1] = '\0';

    // Calculate block alignment
    avi->Aud.nBlockAlign = (WORD)((avi->Aud.nChannels * avi->Aud.wBitsPerSample) / 8);
    if (avi->Aud.nBlockAlign == 0)
        return(avi->AVIerr = AVIERR_BAD_PARAMETER);

    // Reserve additional space for audio superindex
    if (avi->ODMLmode != STRICT_LEGACY)
    {
        memset(filler, 0, sizeof(filler));
        File64Write(avi->fp, filler, sizeof(filler));
    }

    return(0);
}


// Write an audio chunk to the file.
// This function will create an audio stream chunk, write NumBytes
// of AudioBuf to the file and also create the applicable indexes.
// Return 0 if written OK, else error code.

int AVI_WriteAframe(AVI2 *avi, BYTE *AudBuf, DWORD len)
{
//    DWORD offset;
    int ret;


    if (!avi)
        return(AVIERR_AVI_STRUCT_BAD);

    avi->AVIerr = AVIERR_NO_ERROR;

    if (avi->filemode != FOR_WRITING)
        return(avi->AVIerr = AVIERR_WRONG_FILE_MODE);

    if (!avi->has_video || !avi->has_audio)
        return(avi->AVIerr = AVIERR_MISSING_VIDEO);

    if (!AudBuf || !len)
        return(avi->AVIerr = AVIERR_BAD_PARAMETER);

    // Check legacy 2GB limit
    if (!CheckFileLimit(avi, len))
    {
        if (avi->ODMLmode != STRICT_LEGACY)
        {
            // File would be > 1GB for ODML
            ret = CloseCurrentRIFFSegment(avi);
            if (ret != 0)
                return ret;
            ret = StartNewRIFFSegment(avi);
            if (ret != 0)
                return ret;

        }
        else  // Silently ignore when legacy file has hit 2GB limit
            return 0;
    }

    if (avi->movi_start == 0)   // start the movi LIST
        BeginMovi(avi);


    // Write audio chunk header first
    WriteFCC(avi->fp, '##wb', 0);
    WriteDWORD(avi->fp, len);

    // Add index entry to point to start of data
    ret = AddIndexEntry(avi, &avi->AudRt, len, TRUE);
    if (ret)
        return ret;

    // Write actual chunk
    File64Write(avi->fp, AudBuf, len);

    avi->num_audio_frames++;

    // Pad after writing buffer - not included in LEN
    if (NEED_PAD_EVEN(len))
        File64Putchar(avi->fp, 0);

    // Track max frame size
    if ((DWORD) len > avi->max_audio_chunk_size)
        avi->max_audio_chunk_size = len;


    return 0;
}






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


// avi2_read.c
// Functions for reading AVI and AVI2 files

#include "avi2.h"

// Internal helper function declarations
static int ParseHeaderList(AVI2 *avi, DWORD list_size);
static int ParseStreamList(AVI2 *avi, DWORD list_size);
static int ParseLegacyIndex(AVI2 *avi, DWORD index_size);
static int ParseMasterIndex(AVI2 *avi, INDX_CHUNK *idxh, DWORD list_size);
static int ParseChunkIndex(AVI2 *avi, INDX_CHUNK *idxh, DWORD list_size);
static int GenerateIndex(AVI2 *avi);
static int WalkRiff(AVI2 *avi);


// This function is for debugging only
char *ShowFcc(DWORD fcctype)
{
    static char tmpstr[10];

    tmpstr[4] = 0;
    memcpy(tmpstr, &fcctype, 4);
    return(tmpstr);
}


// Internal function to parse AVI file structure
// Return 0 if ok, else error code.

int ParseAVIFile(AVI2 *avi)
{
    FOURCC fcc, ListType;
    DWORD  RiffSize, ChunkSize;
    DWORD  filepos;
    int    ret;

    // walk the RIFF file and collect the start of each RIFF segment
    ret = WalkRiff(avi);
    if (ret) return(ret);

    // Read RIFF header
    File64SetPos(avi->fp, 0, SEEK_SET);

    fcc = ReadFCC(avi->fp, NULL);   // read FIXed literal

    if (fcc != 'RIFF')
    {
        // If the first 4 chars in the file aren't 'RIFF' then
        // we aren't reading a real AVI file.
        AVI_DBG("Not a RIFF file");
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);  // File is corrupted
    }

    // Get RIFF chunk size
    // This should be about the same as the file size if we are
    // reading a standard AVI file.  However, AVI2 files will be
    // significantly larger than this number since it only
    // represents the first RIFF segment.

    // Note that all sizes do not include the first 8 bytes in the chunk.

    File64Read(avi->fp, &RiffSize, 4);
    if (RiffSize < 100 || RiffSize >= AVI_MAX_RIFF_SIZE)
    {
        AVI_DBG("Not a RIFF chunk size invalid.");
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);  // File is corrupted
    }

    fcc = ReadFCC(avi->fp, NULL);  // should be 'AVI ' or 'AVIX'
    if (fcc != 'AVI ' && fcc != 'AVIX')
    {
        AVI_DBG("Not an AVI file");
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);  // File is corrupted
    }

    // A pure odml file tends to have the 'AVIX' tag
    // here.  Hybrid odml files with legacy indexes
    // will still have 'AVI '. So if this is 'AVI ',
    // it cannot be assumed that the file is not
    // odml, however 'AVIX' means the file is
    // defintely odml.
//    if (fcc == 'AVIX') avi->ODMLmode = STRICT_ODML;

    // Parse chunks under RIFF
    while (1)
    {
        // The first time through this loop, we have already
        // read 12 bytes.  So filepos is
        filepos = File64GetPos(avi->fp);  // Get current file position.
        if (filepos >= RiffSize + 8) break;  // First RIFF segment only

        fcc = ReadFCC(avi->fp, NULL);     // Read next FourCC
AVI_DBG_1s("Read Fcc: %.4s\n", FCC2STR(fcc));
        if (fcc == (FOURCC)-1) break;     // EOF

        if (File64Read(avi->fp, &ChunkSize, 4) != 4)    // Get size of chunk
            return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

        switch(fcc)
        {
            case 'LIST':   // list directly under RIFF
            {
                ListType = ReadFCC(avi->fp, NULL);
AVI_DBG_1s("Read List Fcc: %.4s\n", FCC2STR(ListType));

                switch(ListType)
                {
                    case 'hdrl':
                    {
                        // Parse header list
                        if (ParseHeaderList(avi, ChunkSize - 4))
                            return(avi->AVIerr);
                        break;
                    }

                    case 'movi':   // movi list under main RIFF
                    {
                        // Found movie data
                        avi->movi_start = File64GetPos(avi->fp);
                        // Skip to end of movi list at end of switch()
                        break;
                    }
                    case 'INFO':
                        // This is a valid chunk, but we don't care
AVI_DBG("Processing INFO\n");
                        break;

                    default:
                    {
                        // Skip unknown LIST
AVI_DBG_1s("Unknown List: '%.4s'\n", FCC2STR(ListType));
                        break;
                    }
                }  // end switch(type)

                break;
            }  // case LIST

            case 'idx1':   // This is not a LIST type
            {
                int rt = ParseLegacyIndex(avi, ChunkSize);
                // Parse legacy index
AVI_DBG("LegacyIndex\n");
                if (rt == AVIERR_NO_INDEX && avi->ODMLmode == AUTO_INDEX)
                {
                    // kill index error because we autoindex later
                    avi->AVIerr = AVIERR_NO_ERROR;
                }
                else if (rt)     // any other error
                {
                    return(avi->AVIerr);
                }
                break;
            }

            default:
            {
                // Skip unknown chunk
AVI_DBG_1s("Unknown RIFF Chunk: '%.4s'\n", FCC2STR(fcc));
//                File64SetPos(avi->fp, filepos + 8 + ChunkSize, SEEK_SET);
                break;
            }
        } // end switch(fcc)

        // Make sure we start on the next item
        File64SetPos(avi->fp, filepos + 8 + ChunkSize, SEEK_SET);

        // Align to WORD boundary
        if (NEED_PAD_EVEN(ChunkSize))  // read extra byte to keep WORD alignment
            File64Getchar(avi->fp);

    }  // Main RIFF while()

    // Verify we have minimum required data
    if (avi->has_video == FALSE || avi->movi_start == 0)
    {
        AVI_DBG("No video stream found");
        return(avi->AVIerr = AVIERR_MISSING_VIDEO);  // AVI file missing video or MOVI list
    }

    if (avi->has_audio && avi->Aud.nBlockAlign == 0)
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

    // If requested, generate an index if there is none.
    if (!avi->VidRt.Idx)
    {
        if (avi->ODMLmode == AUTO_INDEX)
        {
            int ret = GenerateIndex(avi);
            if (ret) return(ret);
        }
        else return(avi->AVIerr = AVIERR_NO_INDEX);
    }

    return 0;
}


// Read the ODML list
// This is only supposed to have one item in it called 'dmlh'
// which contains the true number of frames in the entire file.
// Return zero if OK, else error code.

// Note that some ODML writers incorrectly put padding at the end
// of the ODML->dmlh-><num frames> without proper 'JUNK'<size>
// attributes.  Unfortunately, we must be tolerant of that bug and
// quietly ignore the illegal padding.

static int ParseOdmlList(AVI2 *avi, DWORD list_size)
{
    FOURCC fcc;
    DWORD size;
    DWORD frames;
//    DWORD filepos;

    if (list_size < 12)  // 'dmlh' + size + frames
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

//    filepos = File64GetPos(avi->fp);  // Save current file position.

    fcc = ReadFCC(avi->fp, NULL);    // Read the list element name
    if (fcc == (FOURCC)-1 || fcc != 'dmlh')
    {
AVI_DBG_1s("Unknown ODML List Element: '%.4s'\n", FCC2STR(fcc));
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
    }

    File64Read(avi->fp, &size, 4);   // Get Element size

    if (size < 4)  // frames
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

    File64Read(avi->fp, &frames, 4);   // get the actual frame count

    avi->num_video_frames = frames;

    return(0);
}


// Parse the 'hdrl' LIST to extract stream information
// The 'hdrl' can contain the main AVI header 'avih', the
// Stream List 'strl', and the odml List 'odml'.
// Return zero if no errors, else error code.

static int ParseHeaderList(AVI2 *avi, DWORD list_size)
{
    AVIMainHeader avih;
    FOURCC HdrFcc, HdrType;
    DWORD HdrSize;
    DWORD file_pos, end_pos;
    int First = TRUE;

    end_pos = File64GetPos(avi->fp) + list_size;

    // Parse the header lists
    while ((file_pos = File64GetPos(avi->fp)) < end_pos)
    {
        // the while() condition guaranties that we should be
        // reading valid data, so any EOF error is file corruption.

        HdrFcc = ReadFCC(avi->fp, NULL);   // get header fcc
        if (HdrFcc == (FOURCC)-1)
            goto corrupted;

        // get header length
        if (File64Read(avi->fp, &HdrSize, 4) != 4) goto corrupted;

        switch (HdrFcc)
        {
            case 'avih':      // main AVI header
                if (!First)   // this must be the first in the list
                {
                    // If not, then its a corrupted file.
corrupted:
                    return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
                }

                // Read main header
                if (File64Read(avi->fp, &avih, sizeof(AVIMainHeader)) != sizeof(AVIMainHeader))
                    goto corrupted;

                // extract frame dimensions and rate.
                avi->width = avih.Width;
                avi->height = avih.Height;

                // require width, height, microseconds to be within spec
                if (avi->width > MAX_WIDTH || avi->height > MAX_HEIGHT ||
                    avih.MicroSecPerFrame < 8000 || avih.MicroSecPerFrame > 1000000)
                    return(avi->AVIerr = AVIERR_NOT_SUPPORTED);

                // Convert microseconds per frame to FPS
                avi->fps = 1000000.0 / avih.MicroSecPerFrame;
                // round to three decimal places
                avi->fps += 0.0005;
                avi->fps = (double)((DWORD) (avi->fps * 1000.0) / 1000);
                break;

            case 'LIST':
                // This is a list under header list - now 2 deep
                HdrType = ReadFCC(avi->fp, NULL);  // get list type
                if (HdrType == (DWORD) -1) goto corrupted;

                switch(HdrType)
                {
                    case 'strl':
                        // Parse stream list
                        if (ParseStreamList(avi, HdrSize - 4))
                            return(avi->AVIerr);
                        break;

                    case 'odml':
//                        avi->ODMLmode = STRICT_ODML;
                        if (ParseOdmlList(avi, HdrSize - 4))
                            return(avi->AVIerr);
                        break;

                    default:
                        // This is not an error, but we don't process it.
AVI_DBG_1s("Unknown Header List: '%.4s'\n", FCC2STR(HdrType));
                        break;
                }  // end switch(type)
                break;  // end LIST switch

            default:    // unknown 'hdrl' LIST element
                // Not an error, but we don't process it.
AVI_DBG_1s("Unknown Header Type: '%.4s'\n", FCC2STR(HdrFcc));
                break;

        } // end switch(fcc) for 'hdrl' list

        First = FALSE;

        // jump to next chunk
        File64SetPos(avi->fp, file_pos + 8 + HdrSize, SEEK_SET);

        // Align if needed
        if (NEED_PAD_EVEN(HdrSize))  // read extra byte to keep WORD alignment
            File64Getchar(avi->fp);

    }  // while()

    return(AVIERR_NO_ERROR);
}







// Parse a stream list (video or audio) 'strl'
// The stream list contains the stream header 'strh' which is
// immediately followed by the stream format 'strf'.
// The stream list 'strl' also contains the 'indx' chunk
// which is usually a master odml index but can be a regular index.
// Return 0 if no errors, else error code.

static int ParseStreamList(AVI2 *avi, DWORD list_size)
{
    FOURCC fcc, fccType;
    DWORD size;
    DWORD end_pos, file_pos;
    AVIStreamHeader64 strh = {0};
    INDX_CHUNK idxh;
    enum StreamTypes { UNKNOWN_STREAM=0, VIDEO_STREAM, AUDIO_STREAM };
    int StreamType = UNKNOWN_STREAM;
    int ret;

    end_pos = File64GetPos(avi->fp) + list_size;

    while ((file_pos = File64GetPos(avi->fp)) < end_pos)
    {
        // the while() condition guaranties that we should be
        // reading valid data, so any EOF error is file corruption.

        // Read stream header
        fcc = ReadFCC(avi->fp, NULL);   // Get stream name
        if (fcc == (FOURCC)-1)
        {
corrupted:
            return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
        }

        // Get stream length
        if (File64Read(avi->fp, &size, 4) != 4) goto corrupted;
        if (size > 0x7FFFFFFF)   // validate against oversize chunk sizes
            goto corrupted;

AVI_DBG_1s("Stream List: '%.4s'\n", FCC2STR(fcc));

        switch (fcc)
        {
            case 'strh':    // stream header
                // The official AVI specs from Microsoft say that
                // only the 56 byte version of AVIStreamHeader should
                // be used.  The size is sometimes ambiguous due
                // to different versions of the RECT structure that
                // the rcFrame member uses.  In older 16 bit versions
                // of Windows, RECT was defined as having four 16 bit
                // integers.  This is what the AVI spec says to use.
                // In newer systems, RECT was redefined as having
                // four 32 bit integers.  So some AVI libraries will
                // generate the correct 56 byte structures and others
                // will generate 64 byte structures.  To make things
                // even more complicated, some libraries don't
                // include the rcFrame RECT at all and use a
                // truncated 48 byte structure.  Here we handle all
                // three possibilities.  Since we don't actually use
                // the rcFrame member here, we just use the largest
                // version, but only read the actual number of bytes
                // delcared in the chunk size above.  However, when
                // writing AVI files, this library will only use the
                // correct 56 byte version.

                // Make sure size is <= our structure in case there
                // is another version I don't know about.

                if (size > sizeof(AVIStreamHeader64))
                    goto corrupted;

                if (File64Read(avi->fp, &strh, size) != size)  // read the stream header
                    goto corrupted;

                fccType = FIX_LIT(strh.fccType);

                if (fccType == 'vids')  // video stream
                {
                    StreamType = VIDEO_STREAM;  // indicate that this is a video stream
                    avi->has_video = TRUE;
                    avi->VideoCodec = FIX_LIT(strh.fccHandler);
                    avi->num_video_frames = strh.Length;
                }
                else if (fccType == 'auds')   // Audio stream
                {
                    StreamType = AUDIO_STREAM;  // Indicate that this is an audio stream
                    avi->has_audio = TRUE;
                    avi->num_audio_frames = strh.Length;
                }
                // There can be other stream types, but we ignore them.
                break;

            case 'strf':    // stream format
AVI_DBG_1d("Stream format #: %d\n", StreamType);
                if (StreamType == VIDEO_STREAM)    // Video stream format
                {
                    STREAMFORMATVID vfmt;

                    if (File64Read(avi->fp, &vfmt, sizeof(STREAMFORMATVID)) !=
                        sizeof(STREAMFORMATVID)) goto corrupted;

                    // Update width/height from stream format if needed
                    if (vfmt.biWidth == 0  || vfmt.biWidth > MAX_WIDTH ||
                        vfmt.biHeight == 0 || vfmt.biHeight > MAX_HEIGHT)
                        return(avi->AVIerr = AVIERR_NOT_SUPPORTED);
                    avi->width = vfmt.biWidth;
                    avi->height = vfmt.biHeight;
                }
                else if (StreamType == AUDIO_STREAM)  // Audio stream format
                {
                    // read directly into our avi structure
                    if (File64Read(avi->fp, &avi->Aud,
                        sizeof(STREAMFORMATAUD)) != sizeof(STREAMFORMATAUD))
                            goto corrupted;

                    avi->AudioCodec = avi->Aud.wFormatTag;
                }
                else  // StreamType is UNKNOWN_STREAM
                {
                    // This section is where other stream formats can be
                    // processed, but for now, do nothing.
                }
                break;

            case 'indx':    // odml index
            {
AVI_DBG_1d("ODML index Stream#: %d\n", StreamType);
                // This is one per stream and could either be
                // master index or stream index.

                if (StreamType == UNKNOWN_STREAM)  // we only process known streams
                    break;

                // Read the index chunk
                if (File64Read(avi->fp, &idxh, sizeof(INDX_CHUNK)) != sizeof(INDX_CHUNK))
                    goto corrupted;

                if (idxh.bIndexType == AVI_INDEX_OF_INDEXES)
                {
                    // Some incorrectly designed AVI ODML writers will
                    // put padding bytes at the end of the superindex
                    // without marking them as proper 'JUNK'<size>
                    // chunks.  This illegal space is not to spec, but
                    // we must still handle it here.

                    if (idxh.wLongsPerEntry != sizeof(SUPERINDEXENTRY) / sizeof(DWORD))
                        goto corrupted;

                    if (idxh.nEntriesInUse * sizeof(SUPERINDEXENTRY) >
                        size - sizeof(INDX_CHUNK) )
                    {
AVI_DBG("Superindex size mismatch.\n");
                        goto corrupted;
                    }

                    ret = ParseMasterIndex(avi, &idxh, size - 4);
CheckAutoIndex:
                    if (ret == AVIERR_NO_INDEX && avi->ODMLmode == AUTO_INDEX)
                    {
                        // Error is killed here because index will be auto generated later
                        avi->AVIerr = AVIERR_NO_ERROR;
                    }
                    else if (ret)    // all other errors
                    {
                        return(ret);
                    }
                }
                else if (idxh.bIndexType == AVI_INDEX_OF_CHUNKS)
                {
                    if (idxh.wLongsPerEntry != sizeof(STDINDEXENTRY) / sizeof(DWORD))
                        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

                    if (idxh.nEntriesInUse !=
                        ((size - 4) - sizeof(INDX_CHUNK)) / sizeof(STDINDEXENTRY))
                    {
AVI_DBG("ODML Index size mismatch.\n");
                        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
                    }

                    ret = ParseChunkIndex(avi, &idxh, size - 4);
                    if (ret) goto CheckAutoIndex;
                }
                else goto corrupted;   // error
//                avi->ODMLmode = STRICT_ODML;
                break;
            }

            case 'vprp':    // video properties header
                if (StreamType != VIDEO_STREAM) goto corrupted;
                // the rest is ignored.
                break;

            default:
            // don't change stream type here.
            //    StreamType = UNKNOWN_STREAM;    // stream type unknown if here
                // Other streams besides 'vids' and 'auds' can be
                // handled here.  For now, do nothing.
                break;

        }  // switch()

        if (NEED_PAD_EVEN(size))  // read extra byte to keep WORD alignment
            File64Getchar(avi->fp);

        // Skip to end of list
        File64SetPos(avi->fp, file_pos + 8 + size, SEEK_SET);

    }  // while()

    return(AVIERR_NO_ERROR);
}


// Parse the idx1 legacy index if not odml
// Convert to internal memory index
// AVI2 parameter is presumed good.
// avi->num_video_frames is presumed good.
// Return 0 if no errors

static int ParseLegacyIndex(AVI2 *avi, DWORD index_size)
{
    DWORD num_entries = index_size / sizeof(AVIINDEXENTRY);
    DWORD num_video_entries;
    DWORD num_audio_entries;
    DWORD i, ac, vc, sz, tmpFcc, ofs, keyf;
    AVIINDEXENTRY *LegacyIdx;
    MEMINDEXENTRY *TmpVidIdx = NULL, *TmpAudIdx = NULL;
    MEMINDEXENTRY *ptr;
    FOURCC fcc_fixed;
    int IdxRelMovi, ret = -1;
    char tmpChar;

    // Skip if index already allocated
    if (avi->VidRt.Idx)
        return(0); // No error, but another index has already been loaded

    avi->VidRt.index_entries = 0;
    avi->AudRt.index_entries = 0;

    if (num_entries == 0)  // no index
    {
        avi->AVIerr = AVIERR_NO_INDEX;
        goto err_general;
    }


    // Allocate the entire index all at once
    // Unfortunately this will leave a hole in memory when this is free'd
    // It will work, but it should be fixed later.
    if ((LegacyIdx = malloc(index_size)) == NULL)
    {
    err_malloc:
        avi->AVIerr = AVIERR_MALLOC;
    err_dealloc:
        if (TmpVidIdx) free(TmpVidIdx);
        if (TmpAudIdx) free(TmpAudIdx);
        if (LegacyIdx) free(LegacyIdx);
        goto err_general;
    }

    // Read the main index all at once
    if (File64Read(avi->fp, LegacyIdx, index_size) != index_size)
    {
err_corrupted:
        avi->AVIerr = AVIERR_FILE_CORRUPTED;
        goto err_dealloc;
    }

    // Warning: The official spec says that the dwChunkOffset member
    // of the AVIINDEXENTRY structure is the offset from the 'm'
    // in 'movi' in the movi list chunk.  This means that the first
    // entry in the index will always have a dwChunkOffset of 4.
    // However, some older AVI libraries that incorrectly
    // used the absolute frame address rather than the offset.  An
    // AVI File parser must be able to handle both versions.

    // Now determine which kind we have.
    IdxRelMovi = FALSE;
    i = LegacyIdx[0].dwChunkOffset;
    if (i == 4) IdxRelMovi = TRUE;  // relative to 'movi'
    else if (i < avi->movi_start)   // problem - index is corrupted
        goto err_corrupted;

    // Although the file spec says that dwChunkOffset is relative to
    // the start of the movi chunk, we change it here to absolute
    // addresses to make access simpler.  When files are written,
    // the correct offset is used.  Also, count audio and video entries.

    ofs = avi->movi_start - 4;
    num_video_entries = num_audio_entries = 0;
    for (i = 0; i < num_entries; i++)
    {
        // change to absolute address if necessary
        if (IdxRelMovi) LegacyIdx[i].dwChunkOffset += ofs;

        // count audio and video entries separately
        tmpChar = ((char *)(&LegacyIdx[i].ckid))[2];
        if (tmpChar == 'w') num_audio_entries++;
        if (tmpChar == 'd') num_video_entries++;
    }

    if (num_video_entries == 0)   // no video
    {
        avi->AVIerr = AVIERR_MISSING_VIDEO;
        goto err_dealloc;
    }

    // Just one final check to make sure we have good data.
    // Goto the frame in the movi section
    File64SetPos(avi->fp, LegacyIdx[0].dwChunkOffset, SEEK_SET);

    // Read FourCC from movi section
    tmpFcc = ReadFCC(avi->fp, NULL);
    fcc_fixed = LegacyIdx[0].ckid;
    fcc_fixed = FIX_LIT(fcc_fixed);  // more efficient this way
    if (tmpFcc == 'LIST')  // its a list rec
    {
        // For 'LIST rec' entries, we need to  skip over LIST
        ReadFCC(avi->fp, NULL);  // dummy read to get over size
        tmpFcc = ReadFCC(avi->fp, NULL);  // get new fcc ('rec ')
    }

    if (tmpFcc != fcc_fixed) goto err_corrupted;

    // If we are here, then it checks out.

    // For legacy AVI, the base address is always zero.
//    avi->BaseTable[0] = 0;
//    avi->NumBases = 1;

    // Now we build two index arrays using our internal memory index.
    // One is for audio and the other is for video.

    // allocate temporary indexes
    TmpVidIdx = malloc(num_video_entries * sizeof(MEMINDEXENTRY));
    if (!TmpVidIdx) goto err_malloc;

    if (num_audio_entries)   // not zero
    {
        TmpAudIdx = malloc(num_audio_entries * sizeof(MEMINDEXENTRY));
        if (!TmpAudIdx) goto err_malloc;
    }

    // save it to the main structure
    avi->AudRt.index_entries = num_audio_entries;
    avi->VidRt.index_entries = num_video_entries;

    // Now go through the main index and create the separate
    // audio and video sub indexes.  Again, we ignore
    // everything except video and audio entries.

    ac = vc = 0;
    for (i = 0; i < num_entries; i++)
    {
        sz = LegacyIdx[i].dwChunkLength;   // Get Header Length

        tmpChar = ((char *)(&LegacyIdx[i].ckid))[2];
        switch(tmpChar)
        {
            case 'w':   // audio
                ptr = &TmpAudIdx[ac++];   // pointer to new index
                if (sz > avi->max_audio_chunk_size) // track max audio size
                    avi->max_audio_chunk_size = sz;
                break;

            case 'd':    // video
                ptr = &TmpVidIdx[vc++];
                if (sz > avi->max_video_frame_size) // track max video size
                    avi->max_video_frame_size = sz;
                break;

            default:
                ptr = NULL;
                break;
        }

        if (ptr)  // only audio and video
        {
            // gather parts
            keyf = !(AVIIF_KEYFRAME & LegacyIdx[i].dwFlags) & 0x1;
            ptr->dwSize = MAKE_DWORD_CHUNK(sz, 0, keyf);
            ptr->dwOffset = LegacyIdx[i].dwChunkOffset + 8;  // header not included in odml index
        }
    }

    // save indexes
    avi->AudRt.Idx = TmpAudIdx;
    avi->VidRt.Idx = TmpVidIdx;
    if (LegacyIdx) free(LegacyIdx);
    ret = 0;
err_general:
    return(ret);
}




// Seek to start of file (frame 0)
int AVI_SeekStart(AVI2 *avi)
{
    if (!avi)
        return(AVIERR_AVI_STRUCT_BAD);

    avi->AVIerr = AVIERR_NO_ERROR;

    if (!avi->has_video)
        return(avi->AVIerr = AVIERR_MISSING_VIDEO);

    avi->current_video_frame = 0;
    avi->current_audio_frame = 0;

    return(0);
}




// Read current video frame
// Return bytes read.  If VidBuf == NULL, return frame chunk size
// without doing anything else.  Return 0 if ERROR, else bytes read.
// Check AVIerr to make sure returned size is correct if return is zero.
// If both return code and AVIerr are zero on return, the avi parameter is NULL.
//
DWORD AVI_ReadVframe(AVI2 *avi, BYTE *VidBuf, DWORD VidBufSize, int *keyframe)
{
    MEMINDEXENTRY *entry;
    DWORD ckSize, baseIdx, key;
    DWORD bytes_read;

    if (!avi)
        return 0;

    avi->AVIerr = AVIERR_NO_ERROR;

    if (avi->filemode != FOR_READING)
    {
        avi->AVIerr = AVIERR_WRONG_FILE_MODE;  // Function incompatible with mode
        return 0;
    }

    if (!avi->VidRt.Idx)
    {
        avi->AVIerr = AVIERR_NO_INDEX;  // No index found
        return 0;
    }

    if (avi->current_video_frame >= avi->VidRt.index_entries)
    {
        avi->AVIerr = AVIERR_EOF;  // No more frames
        return 0;
    }

    // get entry from index
    entry = &avi->VidRt.Idx[avi->current_video_frame];
    key = GET_CHUNK_KEYFRAME(entry->dwSize);
    ckSize = GET_CHUNK_SIZE(entry->dwSize);
    baseIdx = GET_CHUNK_BASEINDEX(entry->dwSize);
    if (!VidBuf)   // just return size
    {
        return(ckSize);
    }

    // Check buffer size
    if (VidBufSize < ckSize)
    {
        avi->AVIerr = AVIERR_BUFFER_SIZE;  // Buffer too small
        return 0;
    }

    // Seek to movi chunk position
    File64SetBase(avi->fp, avi->BaseTable[baseIdx]);
    File64SetPos(avi->fp, entry->dwOffset, SEEK_SET);

    // Read frame data
    bytes_read = File64Read(avi->fp, VidBuf, ckSize);

    // Set keyframe flag if requested
    if (keyframe)
    {
        // Bit 4 (0x10) of dwFlags indicates keyframe
        *keyframe = (key) ? FALSE : TRUE;
    }

    // Advance to next frame
    avi->current_video_frame++;

    return bytes_read;
}


// Read current audio chunk
// Return bytes read.  If AudioBuf == NULL, return chunk size
// from index without doing anything else.
// Return 0 on error.  Check AVIerr to make sure returned size
// is correct if return is zero.

DWORD AVI_ReadAframe(AVI2 *avi, BYTE *AudioBuf, DWORD BufSize)
{
    MEMINDEXENTRY *entry;
    DWORD bytes_read;
    DWORD ckSize, BaseIdx;

    if (!avi)
    {
        return(0);
    }

    avi->AVIerr = AVIERR_NO_ERROR;

    if (avi->filemode != FOR_READING)
    {
        avi->AVIerr = AVIERR_WRONG_FILE_MODE;  // Function incompatible with mode
        return 0;
    }

    if (!avi->AudRt.Idx)
    {
        avi->AVIerr = AVIERR_NO_INDEX;  // No index found
        return 0;
    }

    if (avi->current_audio_frame >= avi->AudRt.index_entries)
    {
        avi->AVIerr = AVIERR_FRAME_NOT_EXIST;  // No more frames
        return 0;
    }

    entry = &avi->AudRt.Idx[avi->current_audio_frame];
    ckSize = GET_CHUNK_SIZE(entry->dwSize);
    BaseIdx = GET_CHUNK_BASEINDEX(entry->dwSize);

    if (!AudioBuf)   // just return size
    {
        if (ckSize == 0)
        {
AVI_DBG_1d("Returning size=0, frm=%d\n", avi->current_audio_frame);
        }
        return(ckSize);
    }

    // Check buffer size
    if (BufSize < ckSize)
    {
        avi->AVIerr = AVIERR_BUFFER_SIZE;  // Buffer too small
        return 0;
    }

    // Seek to chunk position
    File64SetBase(avi->fp, avi->BaseTable[BaseIdx]);
    File64SetPos(avi->fp, entry->dwOffset, SEEK_SET);

    // Read audio data
    bytes_read = File64Read(avi->fp, AudioBuf, ckSize);

    // Advance to next chunk
    avi->current_audio_frame++;

    return bytes_read;
}


// Return the index of the basetable[] that the qwOffset belongs in


static int GetBaseTableIdx(AVI2 *avi, QWORD qwOffset)
{
    DWORD i;

    for (i = 1; i < avi->NumBases; i++)
    {
        if (qwOffset < avi->BaseTable[i])
            break;
    }
    return(i - 1);
}









/*

// Get current location and add it to the BaseTable[].
// This gets called for every new RIFF.
// Returns 0 if OK or error code.

static int SetBaseTable(AVI2 *avi)
{
    QWORD Absolute;

    // Check to make sure there is room for another RIFF segment
    if (avi->NumBases >= MAX_RIFF)  // Check for overflow
        return(avi->AVIerr = AVIERR_NOT_SUPPORTED);

    Absolute = File64GetBase(avi->fp);
    Absolute += (QWORD) File64GetPos(avi->fp);
    File64SetBase(avi->fp, Absolute);  // set current base address

    // Add new entry
    // Current base address is the same as the entry
    avi->BaseTable[avi->NumBases++] = base_addr;

    return(0);
}
*/


// Helper function to process a standard chunk index
// This reads the index entries and updates the dwSize field with base index.
// On entry, fp is already pointing to the INDX_CHUNK header.
// The idx_ptr is already allocated.
//
// Returns the max chunk size, or negative error code

static int
ChunkIndexHelper(AVI2 *avi, MEMINDEXENTRY *idx_ptr, DWORD len)
{
    DWORD i, num_entries, chunk_size, max_chunk_size = 0;
    DWORD entries_size;
    int base_idx;
    INDX_CHUNK idxh;
    QWORD AbsOffset, AbsRiffBase, NewOffset;


    // Get the INDX+CHUNK header
    if (File64Read(avi->fp, &idxh, sizeof(INDX_CHUNK)) != sizeof(INDX_CHUNK))
        return(-(avi->AVIerr = AVIERR_FILE_CORRUPTED));

    // Get index to BaseTable[] with proper RIFF base address
    base_idx = GetBaseTableIdx(avi, idxh.qwBaseOffset);
    if (base_idx < 0)
        return(base_idx);    // error already negative

    AbsRiffBase = avi->BaseTable[base_idx];

    num_entries = idxh.nEntriesInUse;
    if (num_entries == 0)
        return 0;   // zero is unusual, but not an error.

    if (num_entries != len)
        return(-(avi->AVIerr = AVIERR_FILE_CORRUPTED));


    // Read all index entries directly into the memory index
    // Note: MEMINDEXENTRY and STDINDEXENTRY have identical layout
    entries_size = num_entries * sizeof(MEMINDEXENTRY);
    if (File64Read(avi->fp, idx_ptr, entries_size) != entries_size)
        return(-(avi->AVIerr = AVIERR_FILE_CORRUPTED));

    // Update dwSize fields in place to add base index
    for (i = 0; i < num_entries; i++)
    {
        chunk_size = GET_CHUNK_SIZE(idx_ptr[i].dwSize);
        if (chunk_size > 0x1000000)
            return(-(avi->AVIerr = AVIERR_FILE_CORRUPTED));

        // Update max chunk size if needed
        if (chunk_size > max_chunk_size)
            max_chunk_size = chunk_size;

        // Update dwSize to include base index
        // This preserves keyframe bit and size.
        idx_ptr[i].dwSize = MAKE_AVI2_DWSIZE(idx_ptr[i].dwSize, base_idx);

        // Make offset be offset to RIFF base.
        // First get absolute pointer
        AbsOffset = idxh.qwBaseOffset + (QWORD) idx_ptr[i].dwOffset;
        // Make relative to RIFF base
        NewOffset = AbsOffset - AbsRiffBase;
        if (NewOffset > AVI_MAX_RIFF_SIZE)
            return(-(avi->AVIerr = AVIERR_FILE_CORRUPTED));

        idx_ptr[i].dwOffset = (DWORD) NewOffset;  // Save corrected offset
    }


    return(max_chunk_size);
}

// Parse a standard chunk index (non-master index)
// This is used when ODML files skip the master index and put a
// standard index directly in the hdrl section.  The index is
// allocated here since there was no master index to do it.  The
// file pointer is expected to be pointing to the first index entry.


static int ParseChunkIndex(AVI2 *avi, INDX_CHUNK *idxh, DWORD list_size)
{
    MEMINDEXENTRY *idx_array;
    DWORD num_entries, shouldBeEntries;
    DWORD max_chunk_size;
    char chunk_type;
    DWORD save_pos;

    // Save position after index header for proper file positioning on return
    save_pos = File64GetPos(avi->fp);

    num_entries = idxh->nEntriesInUse;   // Number of standard index entries
    if (num_entries == 0)
    {
        // We don't consider this an error.  Skip to end of chunk
        File64SetPos(avi->fp, save_pos + list_size - sizeof(INDX_CHUNK), SEEK_SET);
        return 0;
    }

    // calculate the number of entries according to the chunk size
    shouldBeEntries = (list_size - sizeof(INDX_CHUNK)) / sizeof(STDINDEXENTRY);
    if (num_entries != shouldBeEntries)  // #entries mismatch
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

    // Determine stream type from chunk ID (look at 3rd character)
    chunk_type = ((char *)&idxh->dwChunkId)[2];
    if (chunk_type != 'd' && chunk_type != 'w')
    {
        // Unknown stream type, error
        // Since it has to be 'auds' or 'vids' this is a fatal error if here.
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
    }

    // Allocate memory for index array
    // Note: This is only done for indexes under stream list in place
    // of the superindex
    // Note: Only one index in hdrl allowed.
    idx_array = (MEMINDEXENTRY *)malloc(num_entries * sizeof(MEMINDEXENTRY));
    if (!idx_array)
        return(avi->AVIerr = AVIERR_MALLOC);


    // Go back to the start of the INDEX_CHUNK header
    // This is necessary because of how ChunkIndexHelper() works.
    File64SetPos(avi->fp, -(long) sizeof(INDX_CHUNK), SEEK_CUR);

    // Process the chunk index
    max_chunk_size = ChunkIndexHelper(avi, idx_array, num_entries);
    if (max_chunk_size < 0)
        return(-avi->AVIerr);

    // Save index array and metadata to appropriate stream
    if (chunk_type == 'd')  // Video stream
    {
        avi->VidRt.Idx = idx_array;
        avi->VidRt.index_entries = num_entries;
        avi->max_video_frame_size = max_chunk_size;
    }
    else  // Audio stream ('w')
    {
        avi->AudRt.Idx = idx_array;
        avi->AudRt.index_entries = num_entries;
        avi->max_audio_chunk_size = max_chunk_size;
    }

    // Position file pointer to end of chunk for parser continuation
    File64SetPos(avi->fp, save_pos + list_size - sizeof(INDX_CHUNK), SEEK_SET);

    return 0;
}


// Parse a master index (super index)
// This is the normal situation and all other indexes of the same
// stream number hang off this master index.  This functon reads
// all the super index entries and then processes each lower index.
// It will allocate all memory for the lower indexes.
// Return 0 if OK, else error code.

static int ParseMasterIndex(AVI2 *avi, INDX_CHUNK *idxh, DWORD list_size)
{
    SUPERINDEXENTRY superIdx[MAX_RIFF];
    DWORD IndexLen[MAX_RIFF];
    MEMINDEXENTRY *idx_array, *idx_ptr;
    DWORD i, x, num_master_entries, total_entries;
    DWORD master_size;
    DWORD max_chunk_size = 0;
    DWORD save_pos;
    char chunk_type;
    int result;

    // Save position after master index header
    save_pos = File64GetPos(avi->fp);

    num_master_entries = idxh->nEntriesInUse;

    // Since it has a superindex, it must have at least one regular index
    if (num_master_entries == 0)
        return(avi->AVIerr = AVIERR_NO_INDEX);

    // Make sure we don't overflow
    if (num_master_entries > MAX_RIFF)
        return(avi->AVIerr = AVIERR_OVERFLOW);

    // Determine stream type from chunk ID (look at 3rd character)
    chunk_type = ((char *)&idxh->dwChunkId)[2];

    if (chunk_type != 'd' && chunk_type != 'w')
    {
        // Unknown stream type, error because the only way to get here
        // is to have a preceeding 'auds' or 'vids'
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
    }

    master_size = num_master_entries * sizeof(SUPERINDEXENTRY);
    if (master_size > sizeof(superIdx))
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

    // Read all master index entries
    if (File64Read(avi->fp, superIdx, master_size) != master_size)
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

    // First pass: count total index entries across all chunk indexes
    total_entries = 0;
    for (i = 0; i < num_master_entries; i++)
    {
        if (superIdx[i].qwOffset == 0)  // unused entry (should never happen)
            continue;

        // subtract out the IDX_CHUNK header from the total chunk size
        // and also the 'id##' header and size.
        x = superIdx[i].dwSize - sizeof(INDX_CHUNK) - 8;
        IndexLen[i] = x / sizeof(STDINDEXENTRY);   // #entries
        if (IndexLen[i] > 1000000)
            return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
        total_entries += IndexLen[i];
    }


    // must have at least one index entry
    if (total_entries == 0)
        return(avi->AVIerr = AVIERR_NO_INDEX);

    // Make sure we don't overflow our 32 bit integer in malloc
    if (total_entries > (DWORD_MAX / sizeof(MEMINDEXENTRY)))
        return(avi->AVIerr = AVIERR_OVERFLOW);

    // Now we allocate a buffer big enough for all index entries
    // for this stream.
    idx_array = (MEMINDEXENTRY *)
        malloc(total_entries * sizeof(MEMINDEXENTRY));

    // This may be a design flaw, but in order to seek properly, we
    // need the entire index for the entire file in memory.
    if (!idx_array)
        return(avi->AVIerr = AVIERR_MALLOC);

    // Second pass: read and process each chunk index
    idx_ptr = idx_array;  // start at begining

    for (i = 0; i < num_master_entries; i++)
    {
        if (superIdx[i].qwOffset == 0)  // unused entry (should never happen)
            continue;


        // Jump to chunk index 64 bit location
        File64Qseek(avi->fp, superIdx[i].qwOffset + 8);

        // Process this chunk index into our array
        // returns max chunk size or negative error code.
        result = ChunkIndexHelper(avi, idx_ptr, IndexLen[i]);
        if (result < 0)
        {
            free(idx_array);  // Didn't work, free our index
            return(-result);  // invert error code
        }

        // update max chunk size
        if ((DWORD) result > max_chunk_size) max_chunk_size = result;

        idx_ptr += IndexLen[i];
    }

    // Save index array and metadata to appropriate stream
    if (chunk_type == 'd')  // Video stream
    {
        avi->VidRt.Idx = idx_array;
        avi->VidRt.index_entries = total_entries;
        avi->max_video_frame_size = max_chunk_size;
    }
    else  // Audio stream ('w')
    {
        avi->AudRt.Idx = idx_array;
        avi->AudRt.index_entries = total_entries;
        avi->max_audio_chunk_size = max_chunk_size;
    }

    // Position file pointer to end of master index chunk for parser continuation
    File64SetPos(avi->fp, save_pos + list_size - sizeof(INDX_CHUNK), SEEK_SET);

    return 0;
}


// The caller has requested that a temporary index
// be made if there is none in the AVI file.  Also,
// if the file has an index, but it is malformed,
// this will attempt to build a new one.  Note that
// this only works on legacy files because odml files
// are required to have an index.

static int GenerateIndex(AVI2 *avi)
{
    FOURCC fcc;
    DWORD  size, filePos, endPos, len;
    DWORD  ChunkSize;
    int    stream, ret;

    if (avi->movi_start < 50)  // invalid
    {
corrupted:
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);
    }

    // movi_start points to the first movi record.
    // we need to go back and get the movi list size.
    File64SetPos(avi->fp, avi->movi_start - 8, SEEK_SET);
    File64Read(avi->fp, &size, 4);
    endPos = avi->movi_start + size - 4;   // Take off 4 for 'movi'
    File64Read(avi->fp, &size, 4);  // jump over 'movi' to get back to first chunk

    // Set correct base table entry
    avi->BaseTable[0] = 0;
    avi->NumBases = 1;
    avi->max_audio_chunk_size = avi->max_video_frame_size = 0;
    avi->VidRt.index_entries = avi->AudRt.index_entries = 0;

    // Search every record in movi list
    while ((filePos = File64GetPos(avi->fp)) < endPos)
    {
        fcc = ReadFCC(avi->fp, &stream);
        len = File64Read(avi->fp, &ChunkSize, 4);
        if (fcc == (DWORD) -1 || len != 4)
            goto corrupted;

        ret = 0;
        if (fcc == '##db' || fcc == '##dc') // video
        {
            // We assume this is a keyframe.  We don't really know.
            // This might cause a problem later.
            ret = AddIndexEntry(avi, &avi->VidRt, ChunkSize, TRUE);
            if (ChunkSize > avi->max_video_frame_size)
                avi->max_video_frame_size = ChunkSize;
//            avi->VidRt.index_entries++;
        }
        else if (fcc == '##wb')   // audio
        {
            ret = AddIndexEntry(avi, &avi->AudRt, ChunkSize, TRUE);
            if (ChunkSize > avi->max_audio_chunk_size)
                avi->max_audio_chunk_size = ChunkSize;
//            avi->VidRt.index_entries++;
        }
        else if (fcc == 'ix##')    // odml index
        {
            break;   // no movi records after odml index starts
        }
        // else ignore

        if (ret)
            return(ret);

        // jump to next movi entry
        if (NEED_PAD_EVEN(ChunkSize)) ChunkSize++;
        File64SetPos(avi->fp, filePos + 8 + ChunkSize, SEEK_SET);
    }

    return(AVIERR_NO_ERROR);
}


// Walk the RIFF segments and initialize the BaseTable[] and numbases.
// Return 0 or error code.

static int WalkRiff(AVI2 *avi)
{
    FOURCC fcc;
    DWORD size;
    QWORD qpos;

    File64Qseek(avi->fp, 0);
    avi->NumBases = 0;
    qpos = 0;

    while ((fcc = ReadFCC(avi->fp, NULL)) != (FOURCC) -1)
    {
        // Check if we found a 'RIFF'
        if (fcc == 'RIFF')
        {
            // add it to table
            if (avi->NumBases >= MAX_RIFF)   //  check if over limit
                return(avi->AVIerr = AVIERR_NOT_SUPPORTED);

            avi->BaseTable[avi->NumBases++] = qpos;

            // If we had a RIFF, then we must have a length
            if (File64Read(avi->fp, &size, 4) != 4)
                return(avi->AVIerr = AVIERR_FILE_CORRUPTED);

            if (NEED_PAD_EVEN(size)) size++;  // add padding if necessary

            qpos += (QWORD) size + 8;
            File64Qseek(avi->fp, qpos);

        }
        else break;   // RIFF not found so we're done.
    }

    if (avi->NumBases == 0)    // No RIFFs found
        return(avi->AVIerr = AVIERR_FILE_CORRUPTED);  // File is corrupted

    // Return to beginning
    File64Qseek(avi->fp, 0);

    return(0);
}




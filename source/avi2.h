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


#ifndef AVI2_H
#define AVI2_H

#define AVI2_LIB_VERSION "1.0.0"
#define AVI_DEBUG      // define if debug messages wanted

// This section should be automatically included with GCC and MINGW
#if defined(__GNUC__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__TINYC__)
  #pragma GCC diagnostic ignored "-Wmultichar"

  // Large File Support for MinGW/Linux
  #define _FILE_OFFSET_BITS 64
  #ifndef _LARGEFILE64_SOURCE
      #define _LARGEFILE64_SOURCE 1
  #endif

  // Use standard types to avoid "redefinition" errors
  #include <stdint.h>
  typedef uint64_t  QWORD;
  typedef int64_t   QINT;
  typedef int32_t   LONG; // Guaranteed 4 bytes regardless of 32/64 bit
  typedef uint32_t  FOURCC;
  typedef uint32_t  DWORD;

  #define ASSERT_SIZE(type, expected_size) \
      _Static_assert(sizeof(type) == (expected_size), #type " size mismatch")

  #ifndef min
      #define min(a,b) (((a) < (b)) ? (a) : (b))
  #endif

  #if defined(__TINYC__)
      static inline uint32_t __builtin_bswap32(uint32_t x)
      {
          __asm__ ("bswap %0" : "=r" (x) : "0" (x));
          return x;
      }
  #endif


  // GCC requires multicharacter literals to be flipped
  #define FIX_LIT(n) ((uint32_t)__builtin_bswap32(n))
  #define FCC2STR(n) Fcc2Str(n)
#endif

#if defined(__BORLANDC__)
  // Borland C is a 32bit compiler.
  typedef unsigned __int64 QWORD;     // different
  typedef signed   __int64 QINT;
  typedef long             LONG;     // long is 8 bytes on 64bit compilers, must be 4 bytes here.
  typedef unsigned int     FOURCC;
  typedef unsigned long    DWORD;
  #define ASSERT_SIZE(type, expected_size) \
    typedef char type##_size_check[(sizeof(type) == (expected_size)) ? 1 : -1]

  // Multicharacter literals are handled properly with Borland and
  // do not need to be flipped.
  #define FIX_LIT(n) (n)
  #define FCC2STR(n) ((char *)&(n))
#endif


// This section is included for Microsoft Visual Studio
// Note that I have not tested this, but it should work properly.
#if defined(_MSC_VER)
  #pragma warning(disable : 4066)  // disable multicharacter literal warnings

  #define _CRT_SECURE_NO_WARNINGS   // Allow 64bit file seeks

  // Use standard types (MSVC includes these in <stdint.h> since VS 2010)
  #include <stdint.h>
  typedef uint64_t  QWORD;
  typedef int64_t   QINT;
  typedef int32_t   LONG;   // MSVC 'long' is 32-bit, but int32_t is safer
  typedef uint32_t  FOURCC;
  typedef uint32_t  DWORD;

  // MSVC uses static_assert (C11/C++11) or _STATIC_ASSERT
  #define ASSERT_SIZE(type, expected_size) \
      static_assert(sizeof(type) == (expected_size), #type " size mismatch")

  #ifndef min
      #define min(a,b) (((a) < (b)) ? (a) : (b))
  #endif

  // MSVC intrinsic for byte swapping (equivalent to __builtin_bswap32)
  #include <stdlib.h>
  #define FIX_LIT(n) ((uint32_t)_byteswap_ulong(n))
  #define FCC2STR(n) Fcc2Str(n)
#endif


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#define FALSE   0
#define TRUE   !FALSE

typedef unsigned short WORD;
typedef unsigned char  BYTE;

// Make sure all structures are byte aligned
#ifdef __GNUC__
    #pragma pack(push, 1)
#elif defined(__BORLANDC__)
    #pragma option -a1
#endif

// These are compiler checks to make sure all the typedefs
// are the proper size.  If you get any compiler errors here
// then the type definitions are not correct.

ASSERT_SIZE(QWORD, 8);
ASSERT_SIZE(DWORD, 4);
ASSERT_SIZE(FOURCC, 4);
ASSERT_SIZE(LONG, 4);
ASSERT_SIZE(WORD, 2);
ASSERT_SIZE(BYTE, 1);


#define NEED_PAD_EVEN(x)    ((x) & 0x01)
#define MAX_AUDIO_CHANNELS  16
#define AVI_MAX_RIFF_SIZE   0x7FFFFFF0  // Just under the 2GB limit for standard AVI
#define INDEX_BLOCK_SIZE    512         // Number of index entries in an allocation block
#define MAX_RIFF            128         // Max RIFF segments - must be at least 1
#define MAX_HEIGHT          4096        // Max screen height
#define MAX_WIDTH           8192        // max screen width
#define MAX_FPS             120.0       // max frames/second
#define DWORD_MAX           0xFFFFFFFF

#ifndef SEEK_SET
  #define SEEK_SET 0
  #define SEEK_CUR 1
  #define SEEK_END 2
#endif



// Debug macro
// Borland C does not support variable length arguments in macros so this must be written like this.
// This limits the error to a single string, but the macro can also take a pointer to a buffer that
// can be pre-built if necessary.
#ifdef AVI_DEBUG
  #define AVI_DBG(ErrStr) printf("%s\n", ErrStr)
  #define AVI_DBG_1s(fmt, eStr) printf(fmt,eStr)
  #define AVI_DBG_1d(fmt, x) printf(fmt, x)
#else
  #define AVI_DBG(ErrStr)        /* */
  #define AVI_DBG_1s(fmt, eStr)  /* */
  #define AVI_DBG_1d(fmt, x)     /* */
#endif

// Helper macros to write DWORDs and WORDs.
#define WriteDWORD(fpout, Dval)  {DWORD x=(Dval); File64Write((fpout), (BYTE *) &x, 4);}

#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

// Define the file open modes
#define FOR_READING      0
// Can be OR'ed with this if Auto-indexing if permitted.
#define AUTO_INDEX       0x3000     // If the AVI file
    // does not have a valid index, then a temporary
    // index will be built based on the order of chunks
    // in the movi list.  Note that this only works on
    // legacy AVI files because odml files must have an
    // index.

#define FOR_WRITING      1
// Should be OR'ed with one of the following if writing
#define HYBRID_ODML      0x0000     // A hybrid file is
    // generated such that a legacy player will only be
    // able to play the first RIFF chunk, but modern
    // players will play entire file which can be up to
    // 128GB in size.
#define STRICT_LEGACY    0x1000    // Will only write a
    // single RIFF segment legacy file less than 2GB in
    // size.  No ODML indexes will be written.  Attempts
    // to write files > 2GB are ignored and such files
    // will be truncated wiithout warning.
#define STRICT_ODML      0x2000    // Writes a pure ODML
    // file.  This file can be up to 128 GB in size.
    // No legacy index is written.  The file cannot be
    // played on legacy players.


typedef struct
{
    FILE *fp;
    QWORD SeekBase;   // Base File Pointer
} MFILE;


typedef struct
{
    DWORD MicroSecPerFrame; // frame display rate (or 0)
    DWORD MaxBytesPerSec; // max. transfer rate
    DWORD PaddingGranularity; // pad to multiples of this size;
    DWORD Flags; // the ever-present flags
    DWORD TotalFrames; // # frames in file
    DWORD InitialFrames;
    DWORD NumStreams;
    DWORD SuggestedBufferSize;
    DWORD Width;
    DWORD Height;
    DWORD Reserved[4];
} AVIMainHeader;  // For avih under hdrl (header list)


// Flags for MainAVIHeader
#define 	AVIF_HASINDEX        0x00000010   // file has an index
#define 	AVIF_MUSTUSEINDEX    0x00000020   // index use mandatory
#define 	AVIF_ISINTERLEAVED   0x00000100   // Streams are properly interleaved
#define 	AVIF_TRUSTCKTYPE     0x00000800   // set for ODML files
#define 	AVIF_WASCAPTUREFILE  0x00010000   // chunk interleave untrustable
#define 	AVIF_COPYRIGHTED     0x00020000   // Set if copyrighted - ignore.

// SMALL_RECT is also defined in wincon.h which is included with windows.h
typedef struct
{
    short Left;
    short Top;
    short Right;
    short Bottom;
} SMALL_RECT;

// RECT is also defined in windef.h which is included with windows.h
typedef struct
{
    LONG Left;
    LONG Top;
    LONG Right;
    LONG Bottom;
} RECT;

// GUID is also defined in winnt.h which is included with windows.h
typedef struct
{
    DWORD Data1;
    WORD  Data2;
    WORD  Data3;
    BYTE  Data4[8];
} GUID;

// Due to multiple definitions of AVI Stream Header we have to define all three.

typedef struct     // strh - compact version - 48 bytes
{
    FOURCC fccType;     // Can be 'auds', 'mids', 'txts', or 'vids'
    FOURCC fccHandler;
    DWORD  Flags;
    WORD   Priority;
    WORD   Language;
    DWORD  InitialFrames;
    DWORD  TimeScale;
    DWORD  Rate;   /* Rate / TimeScale == samples/second */
    DWORD  StartTime;
    DWORD  Length;    /* In units above... */
    DWORD  SuggestedBufferSize;
    DWORD  Quality;
    DWORD  SampleSize;
} AVIStreamHeader48;

typedef struct     // strh - mid sized version - 56 bytes
{
    FOURCC fccType;     // Can be 'auds', 'mids', 'txts', or 'vids'
    FOURCC fccHandler;
    DWORD  Flags;
    WORD   Priority;
    WORD   Language;
    DWORD  InitialFrames;
    DWORD  TimeScale;
    DWORD  Rate;   /* Rate / TimeScale == samples/second */
    DWORD  StartTime;
    DWORD  Length;    /* In units above... */
    DWORD  SuggestedBufferSize;
    DWORD  Quality;
    DWORD  SampleSize;
    SMALL_RECT Frame;
} AVIStreamHeader56;

typedef struct     // strh - large version - 64 bytes
{
    FOURCC fccType;     // Can be 'auds', 'mids', 'txts', or 'vids'
    FOURCC fccHandler;
    DWORD  Flags;
    WORD   Priority;
    WORD   Language;
    DWORD  InitialFrames;
    DWORD  TimeScale;
    DWORD  Rate;   /* Rate / TimeScale == samples/second */
    DWORD  StartTime;
    DWORD  Length;    /* In units above... */
    DWORD  SuggestedBufferSize;
    DWORD  Quality;
    DWORD  SampleSize;
    RECT   Frame;
} AVIStreamHeader64;

// Flags for AVIStreamHeader

#define AVISF_DISABLED          0x00000001
#define AVISF_VIDEO_PALCHANGES  0x00010000


typedef struct       // used when format is 'vids'
{ // bmih
   DWORD  header_size;
   LONG   biWidth;
   LONG   biHeight;
   WORD   biPlanes;
   WORD   bits_per_pixel;
   DWORD  biCompression;
   DWORD  biSizeImage;   // total bytes in image
   LONG   biXPelsPerMeter;
   LONG   biYPelsPerMeter;
   DWORD  biClrUsed;
   DWORD  biClrImportant;
   // Variable-length palette data follows in file but not in struct
} STREAMFORMATVID;                   // size=40 + palette size

typedef struct
{ // rgbq
    BYTE    rgbBlue;
    BYTE    rgbGreen;
    BYTE    rgbRed;
    BYTE    rgbReserved;
} VIDPALETTE;    // palette for STREAMFORMATVIDS


/*
struct stream_header_auds_t
{
  int format_type;
  int number_of_channels;
  int sample_rate;
  int bytes_per_second;
  int block_size_of_data;
  int bits_per_sample;
  int byte_count_extended;
};
*/


typedef struct
{
  WORD  wFormatTag;
  WORD  nChannels;
  DWORD nSamplesPerSec;
  DWORD nAvgBytesPerSec;
  WORD  nBlockAlign;
  WORD  wBitsPerSample;
  WORD  cbSize;
} STREAMFORMATAUD;    // same as WAVEFORMATEX   size=18 bytes


typedef struct            // 22 bytes
{
    union
    {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        WORD wReserved;
    } Samples;
    DWORD        dwChannelMask;
    GUID         SubFormat;         // 16 bytes
} AUDIOEXTENSION;

typedef struct   // 12 bytes - for use when wFormatTag = 0x0055 (MP3)
{
    WORD  wID;
    DWORD fdwFlags;
    WORD  nBlockSize;
    WORD  nFramesPerBlock;
    WORD  nCodecDelay;
} MP3EXT;

#define AVIIF_LIST          0x00000001L // chunk is a 'LIST'
#define AVIIF_KEYFRAME      0x00000010L // this frame is a key frame.
#define AVIIF_FIRSTPART     0x00000020L // this frame is the start of a partial frame.
#define AVIIF_LASTPART      0x00000040L // this frame is the end of a partial frame.
#define AVIIF_NO_TIME	    0x00000100L // this frame doesn't take any time
#define AVIIF_COMPUSE       0x0FFF0000L


typedef struct     // legacy index structure
{
    DWORD ckid;
    DWORD dwFlags;
    DWORD dwChunkOffset;
    DWORD dwChunkLength;
} AVIINDEXENTRY;

typedef struct
{
    DWORD CompressedBMHeight;
    DWORD CompressedBMWidth;
    DWORD ValidBMHeight;
    DWORD ValidBMWidth;
    DWORD ValidBMXOffset;
    DWORD ValidBMYOffset;
    DWORD VideoXOffsetInT;
    DWORD VideoYValidStartLine;
} VIDEO_FIELD_DESC;


typedef struct
{
    DWORD VideoFormatToken;
    DWORD VideoStandard;
    DWORD dwVerticalRefreshRate;
    DWORD dwHTotalInT;
    DWORD dwVTotalInLines;
    DWORD dwFrameAspectRatio;
    DWORD dwFrameWidthInPixels;
    DWORD dwFrameHeightInLines;
    DWORD nbFieldPerFrame;
    VIDEO_FIELD_DESC FieldInfo[];    // nbFieldPerFrame
} VideoPropHeader;


typedef struct
{
    DWORD dwTotalFrames;    // total frames in all riffs combined.
//    DWORD dwReserved[61];   // junk not used - note the spec doesn't actually have this
} AVIEXTHEADER;


// bIndexType codes
#define AVI_INDEX_OF_INDEXES 0x00   // when each entry in aIndex array points to an index chunk
#define AVI_INDEX_OF_CHUNKS  0x01   // when each entry in aIndex array points to a chunk in the file
#define AVI_INDEX_IS_DATA    0x80   // when each entry is aIndex is really the data

// bIndexSubtype codes for INDEX_OF_CHUNKS
#define AVI_INDEX_STANDARD   0x00   // Standard index chunks
#define AVI_INDEX_2FIELD     0x01   // when fields within frames are also indexed


// This is the master index header common to all indx chunks.
//
typedef struct
{
    WORD   wLongsPerEntry;   // size of each entry in aIndex array
    BYTE   bIndexSubType;    // Set to AVI_INDEX_2FIELD if for 2 field video
    BYTE   bIndexType;       // one of AVI_INDEX_* codes
                             // AVI_INDEX_OF_INDEXES - super index,
                             // AVI_INDEX_OF_CHUNKS - regular index
    DWORD  nEntriesInUse;    // index of first unused member in aIndex array
    DWORD  dwChunkId;        // fcc of what is indexed
    QWORD  qwBaseOffset;     // offsets in StdIndex array are relative to this.
                             // Not used for superindex.  Each entry has its own.
    DWORD  dwReserved;       // must be 0
} INDX_CHUNK;   // 6 longs = 24 bytes


// This is a standard index entry.  From the INDX_CHUNK above, there are
// nEntriesInUse of these in the file.  The bIndexType for this index
// is AVI_INDEX_OF_CHUNKS.
typedef struct
{
    DWORD dwOffset;    // qwBaseOffset + this is absolute file offset
                    // Note that this points to the data not the chunk
    DWORD dwSize;      // bit 31 is set if this is NOT a keyframe
} STDINDEXENTRY;


// This is the same as the STDINDEXENTRY except that an addional offset
// is added for the second field.
typedef struct
{
    DWORD dwOffset;
    DWORD dwSize;      // size of all fields (bit 31 set for NON-keyframes)
    DWORD dwOffsetField2; // offset to second field
} FIELDINDEXENTRY;


// The bIndexType is AVI_INDEX_OF_INDEXES.

typedef struct
{
    QWORD qwOffset;   // absolute file offset, offset 0 is unused entry??
    DWORD dwSize;     // size of index chunk at this offset
    DWORD dwDuration; // time span in stream ticks
} SUPERINDEXENTRY;


// Memory Index Entry Struct
// This is the private in-memory structure for index entries.
// This uses the same number of bytes as STDINDEXENTRY and is designed
// to be overlaid onto that structure.  However, we carve out a
// portion of the dwSize element to support an extra bitfield.
// This limits the size of each chunk to be no more than 16MB which
// is plenty enough for an uncompressed 1080p video frame.  The
// following macros are used to access the bitfields because of
// inconsistencies across C compilers in how they handle structure
// defined bitfields.

// To convert bitfield to regular DWORD
#define GET_CHUNK_SIZE(size)       ((DWORD)((size) & 0xFFFFFF))
#define GET_CHUNK_BASEINDEX(size)  ((DWORD)(((size) & 0x7F000000) >> 24))
#define GET_CHUNK_KEYFRAME(size)   ((DWORD)(((size) >> 31) & 0x00000001))

// To make a DWORD bitfield (assumes [parameters are all DWORD)
#define MAKE_DWORD_CHUNK(size, base, key) \
    (DWORD)(((size) & 0x00FFFFFF) | (((base) & 0x7F) << 24) | ((key == FALSE) << 31) )

// To add new baseIdx bitfield to existing AVI2 index dwSize
#define MAKE_AVI2_DWSIZE(size, base)   \
    (DWORD)(((size) & 0x80FFFFFF) | (((base) & 0x7F) << 24))

// The base index is an index to the BaseTable[] array.   This
// is an array of RIFF base addresses.  The base addresses are
// always the address of the 'R' in 'RIFF' - one for each RIFF
// segment.  These get added to the offset to make an absolute
// QWORD address.

typedef struct
{
    DWORD dwOffset;
    // This is the offset that when combined with the base addres
    // will form a QWORD absolute address that points to the first
    // byte of a movi chunk payload.  It does not point to the
    // FourCC or its size.
    DWORD dwSize;  // see bit mappings
    // Actual chunk length is (dwSize & 0x00FFFFFF).
    // Base Index is ((dwSize & 0x7F000000) >> 24)
    // bit 31 ((dwSize >> 31) & 0x00000001) is set if this is NOT a keyframe
} MEMINDEXENTRY;



// The following structure describes a single audio track.  An AVI file
// can have multiple audio tracks for things like different languages,
// commentaries, laugh tracks, etc.  For things like stereo, it is best
// to keep that as a single stream and let the codec worry about it.  It
// is possible to have two mono streams and try to merge them together
// to make stereo, but that almost never works in practice.

typedef struct
{
    DWORD  Codec;             // Audio codec
    DWORD  Channels;          // Audio channels per sample - 2
    DWORD  Rate;              // Samples Per Second        - 22050
    DWORD  BitsPerSample;     // bits per audio sample     - 16
    DWORD  BytesPerSecond;    // Bytes per second          - 88200
    DWORD  BlockSize;         // Bytes per Sample          - 4

    DWORD  StreamNum;         // Stream number
    DWORD  NumChunks;         // Total audio chunks in file

    FOURCC fcc;               // Fourcc tag used - '01wb'
    DWORD  CurChunk;          // Current chunk as indexed

    MEMINDEXENTRY *idx;       // pointer to index

} AUDIO_STREAM_BLOCK;


// Root of indexes
typedef struct index_root
{
    DWORD index_entries;   // number of entries actually used
    DWORD idx_blocks;      // number of blocks allocated
    DWORD SuperIdxOffset;  // next location for superindex entry
//    DWORD nIndexes;        // Number of superindexes written
    char  Name[32];        // Name of stream
    MEMINDEXENTRY *Idx;    // video index
} INDEX_ROOT;


// Main AVI structure
// Note that long types are 64 bits with a 64 bit compiler and 32 bits on a 32 bit compiler like Borland.

typedef struct
{
    MFILE *fp;
    WORD   filemode;      // for_reading, for_writing
    WORD   ODMLmode;     // hybrid_odml, strict_legacy, strict_odml
    int has_video;
    int has_audio;
//    int isODML;          // TRUE if this is an AVI2 ODML file
    int AVIerr;


    // Video info
    DWORD width;
    DWORD height;
    double fps;
    FOURCC VideoCodec;
    DWORD num_video_frames;
    DWORD current_video_frame;
    DWORD max_video_frame_size;
    INDEX_ROOT VidRt;    // video index root

    // Audio info
    STREAMFORMATAUD Aud;   // same as WAVEFORAMTEX
    DWORD AudioCodec;
    DWORD current_audio_frame;
    DWORD num_audio_frames;         // ADD THIS
    DWORD max_audio_chunk_size;
    INDEX_ROOT AudRt;          // audio index root

// The base index is an index to the BaseTable[] array.   This
// is an array of RIFF base addresses.  The base addresses are
// always the address of the 'R' in 'RIFF' - one for each RIFF
// segment.  These get added to the offset to make an absolute
// QWORD address.

    DWORD NumBases;                 // number of base table entries 0=uninitialized
    QWORD BaseTable[MAX_RIFF];      // Table of base address for RIFF segments

    // File structure info
    DWORD movi_start;           // file position of first 'movi' list record
//    DWORD header_pos;      // ADD THIS - position where header starts
    DWORD current_riff_size; // ADD THIS - size of current RIFF segment
//    DWORD total_bytes_written;  // Track total bytes to detect 2GB threshold

} AVI2;


// Error Defines
enum errvals
{
    AVIERR_NO_ERROR=0,
    AVIERR_CANT_CREATE,
    AVIERR_FILE_NOT_EXIST,
    AVIERR_FILE_CORRUPTED,
    AVIERR_CANT_WRITE_FILE,
    AVIERR_WRONG_FILE_MODE,
    AVIERR_FRAME_NOT_EXIST,
    AVIERR_NO_INDEX,
    AVIERR_BUFFER_SIZE,
    AVIERR_EOF,
    AVIERR_TOO_MANY_AUD_CHANNELS,
    AVIERR_MISSING_VIDEO,
    AVIERR_BAD_CLOSE,
    AVIERR_MALLOC,
    AVIERR_AVI_STRUCT_BAD,
    AVIERR_NOT_SUPPORTED,
    AVIERR_CANT_CREATE_FILE,
    AVIERR_BAD_PARAMETER,
    AVIERR_STREAM_INVALID,
    AVIERR_FUNCTION_ORDER,
    AVIERR_OVERFLOW,
    AVIERR_TOO_MANY_SEGMENTS,
    AVIERR_UNKNOWN,
    AVIERR_COUNT     // count of all enums
};



// Internal File64.c prototypes

void   File64SetBase(MFILE *mfp, QWORD NewBase);
QWORD  File64GetBase(MFILE *mfp);
MFILE *File64Open(char *fname, char *mode);
int    File64Close(MFILE *mfp);
size_t File64Read(MFILE *mfp, void *buffer, int len);
size_t File64Write(MFILE *mfp, void *buffer, int len);
int    File64Qseek(MFILE *mfp, QWORD AbsAddr);
int    File64SetPos(MFILE *mfp, LONG offset, int whence);
DWORD  File64GetPos(MFILE *mfp);
BYTE   File64Getchar(MFILE *mfp);
BYTE   File64Putchar(MFILE *mfp, BYTE ch);


// Internal Common functions
char  *AVI_StrError(int errnum);
FOURCC ReadFCC(MFILE *in, int *StreamNum);
int    WriteFCC(MFILE *out, FOURCC fccval, int StreamNum);
DWORD  ReverseLiteral(DWORD val);
int    ParseAVIFile(AVI2 *avi);
int    FinalizeWrite(AVI2 *avi);
int    AddIndexEntry(AVI2 *avi, INDEX_ROOT *rt, DWORD len, DWORD Key);
char  *Fcc2Str(FOURCC val);



// File I/O
AVI2 *AVI_Open(const char *filename, DWORD mode, int *err);
int   AVI_Close(AVI2 *avi);
int   AVI_WriteHeader(AVI2 *avi);
int   AVI_SeekStart(AVI2 *avi);

// Video output
int AVI_SetVideo(AVI2 *avi, char *name, DWORD width, DWORD height, double fps, FOURCC codec);
int AVI_WriteVframe(AVI2 *avi, BYTE *VidBuf, DWORD len, int keyframe);

// Video input
DWORD AVI_ReadVframe(AVI2 *avi, BYTE *VidBuf, DWORD VidBufSize, int *keyframe);
DWORD AVI_GetVframeSize(AVI2 *avi, DWORD FrameNum);
//DWORD AVI_GetVideoFrameFilePointer(AVI2 *avi, DWORD frame);
DWORD AVI_SetCurrentVideoFrame(AVI2 *avi, DWORD frame);

// Audio output
int AVI_SetAudio(AVI2 *avi, char *name, int NumChannels, long SamplesPerSecond,
                 long BitsPerSample, long codec);
int AVI_WriteAframe(AVI2 *avi, BYTE *AudBuf, DWORD len);

// Audio input
DWORD AVI_ReadAframe(AVI2 *avi, BYTE *AudioBuf, DWORD BufSize);
int AVI_set_audio_position(AVI2 *avi, DWORD frame);


// HELPER MACROS

// Macro to return video frame file pointer
#define GET_VIDEO_FILE_POSITION(avi, frame) (avi->VidIdx[frame].dwOffset)

// Macro to return the current video frame
#define GET_CURRENT_VIDEO_FRAME_NUM(avi) (avi->current_video_frame)

// Macro to set the audio position
#define SET_AUDIO_POSITION(avi, frame) {avi->current_audio_frame = frame;}




// end if AVI2.h file
#endif

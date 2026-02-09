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

// Compile on Linux for linux
// gcc  -m32 avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm
// gcc  -m64 avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm

// Compile on linux for windows
// i686-w64-mingw32-gcc  avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c winjpeg.c -o avi2.exe -I. -I./source -O2 -lgdi32 -luser32 -lole32 -loleaut32 -lwinmm
// x86_64-w64-mingw32-gcc avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c winjpeg.c -o avi2.exe -I. -I./source -O2 -lgdi32 -luser32 -lole32 -loleaut32 -lwinmm

// Compile on windows using Borland C
// bcc32.exe -4 -Isource avi2.c audio2.c gui.c  source/avi2_common.c source/avi2_read.c source/avi2_write.c source/file64.c jpg2raw.c jpeg6lib.lib

// Compile with Tiny C
// tcc  -m32 -w avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm
// tcc  -m64 -w avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm







#include "source/avi2.h"

#if defined(__WIN32__)
  #define PIXELSIZE 3
#else
  #define PIXELSIZE 4
#endif


#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
  #define DECODE_JPEG(a,b,c,d)  WinJpg2Raw(a,b,c,d)
#else
   // for Borland, GCC
  #define DECODE_JPEG(a,b,c,d)  Jpg2Raw(a,b,c,d,JCS_RGB)
#endif
#define JCS_RGB 2   // for jpeg library


int Jpg2Raw(BYTE *DecodedBuf, DWORD DecodedBufSize, BYTE *JpgPtr, DWORD JpgLen, int ClrSpc);
int WinJpg2Raw(BYTE *DecodedBuf, DWORD DecodedBufSize, BYTE *JpgPtr, DWORD JpgLen);


// Program to test avi2 library

// Fake windows defines
#define WM_QUIT         0x0012
#define WM_LBUTTONDOWN  0x0201

// GUI Functions
DWORD ticks(void);
void *InitWindows(int Width, int Height);
int ProcessGuiMessages(void);
void GuiShowFrame(void);
void CloseGui(void);
void GuiSleep(DWORD ms);


/* Global variables for the buffer */
void *pPixels = NULL;
AVI2 *avi, *aviout;
int vWidth, vHeight;
int BufJpegSize, VidBufSize;   // in bytes
BYTE *BufJpeg;

DWORD StartAtFrame = 0;  // frame playback starts at

#define INTERVAL_MS (DWORD)(1000.0 / avi->fps)


// Sound functions
int InitWindowsAudio(STREAMFORMATAUD *Aud, DWORD max_chunk_size);
int AddChunkToWavQ(BYTE *Buffer, DWORD BufLen);
int CheckWavQ(void);
BYTE *GetFreeWavBuffer(void);
void CloseWindowsAudio(void);
DWORD GetAudioPos(void);
void SetAudioPos(DWORD startSample);


// returns -1 on failure

int initVideo(char *fname)
{
    char fnameout[256];
    int err;

    avi = AVI_Open(fname, FOR_READING | AUTO_INDEX, &err);   // open avi
    if (!avi)
    {
        if (err)
        {
            printf("Error opening file:\n%s\n",
                   AVI_StrError(err));
        }
        return(-1);   // failed to open
    }

    AVI_SeekStart(avi);  // move to first frame

    vWidth = avi->width;
    vHeight = avi->height;
    BufJpegSize = avi->max_video_frame_size;
    VidBufSize = vWidth * vHeight * PIXELSIZE;
    BufJpeg = malloc(BufJpegSize);
    if (!BufJpeg) return(-1);

    // init video for writing
    strcpy(fnameout, fname);
    strcat(fnameout, "out.avi");
    aviout = AVI_Open(fnameout, FOR_WRITING | HYBRID_ODML, &err);   // open avi
    if (!aviout) return(-1);   // failed to open
    AVI_SetVideo(aviout, "Video", vWidth, vHeight, avi->fps, avi->VideoCodec);
    if (avi->has_audio)
        AVI_SetAudio(aviout, "Audio", avi->Aud.nChannels, avi->Aud.nSamplesPerSec,
                 avi->Aud.wBitsPerSample, avi->AudioCodec);

    return(0);
}

void CloseVideo(AVI2 *avi)
{
    AVI_Close(avi);
    AVI_Close(aviout);
    free(BufJpeg);
}

// Reverse order of colors for Windows

void ConvertBGRtoRGB(void)
{
#if defined(__WIN32__)
  #if defined(__BORLANDC__)
    // Windows: Standard 24-bit BGR swap if needed
    int imageSize = vWidth * vHeight * 3;
    int i;
    BYTE temp, *Buffer;

    Buffer = pPixels;

    for (i = 0; i < imageSize; i += 3, Buffer += 3)
    {
        temp = *Buffer;
        *Buffer = Buffer[2];
        Buffer[2] = temp;
    }
  #endif
#else
// LINUX: Expand 24-bit RGB to 32-bit BGRA
    long totalPixels = (long)vWidth * vHeight;
    unsigned char *src = (unsigned char *)pPixels + (totalPixels * 3) - 3;
    unsigned char *dst = (unsigned char *)pPixels + (totalPixels * 4) - 4;
    long i;

    // Work backwards to avoid overwriting source data
    for (i = 0; i < totalPixels; i++)
    {
        dst[0] = src[2]; // B
        dst[1] = src[1]; // G
        dst[2] = src[0]; // R
        dst[3] = 0xFF;   // A

        if (i < totalPixels - 1)
        {
            dst -= 4;
            src -= 3;
        }
    }
#endif
}


int GetFrame(void)
{
    int len, rt = FALSE;

    len = AVI_ReadVframe(avi, BufJpeg, BufJpegSize, NULL);
    if (len <= 0)
    {
        if (len < 0) printf("ReadVframe() returned error: %d\n", len);
        return(TRUE);
    }

    // Write to new file
    if (AVI_WriteVframe(aviout, BufJpeg, len, TRUE))
    {
        printf("Failed to write frame.\n");
        return(TRUE);
    }

    if (avi->VideoCodec == 'MJPG')
    {
        DWORD jpegOutputSize = vWidth * vHeight * 3;

        if (BufJpeg[0] != 0xFF)
        {
            printf("Not a JPEG at frame %d\n", avi->current_video_frame - 1);
        }


        rt = DECODE_JPEG(pPixels, jpegOutputSize, BufJpeg, len);
        if (rt != 0)
        {
            printf("Jpg2Raw failed with error: %d\n", rt);
            return(TRUE);
        }
        ConvertBGRtoRGB();
    }

    return(rt);
}


DWORD GetMasterFrameTarget(int NoAud, int endOfAudio, DWORD StartTime)
{
    if (!NoAud && !endOfAudio)
    {
        // Use Audio Hardware as the Master Clock
        DWORD audPos = GetAudioPos();
        DWORD target = (DWORD)((double)audPos / avi->Aud.nSamplesPerSec * avi->fps);
        return target;
    }
    else
    {
        // Use System Timer as the Master Clock
        DWORD now = ticks();
        DWORD ElapsedMs = now - StartTime;
        DWORD target = (DWORD)(((double)ElapsedMs / 1000.0) * avi->fps);
        return target;
    }
}


void FrameSeek(DWORD targetFrame, int NoAud, DWORD *pStartTime)
{
    double seconds;
    DWORD samples;

    avi->current_video_frame = targetFrame;
    avi->current_audio_frame = targetFrame;

    seconds = (double)targetFrame / avi->fps;
    samples = (DWORD)(seconds * (double)avi->Aud.nSamplesPerSec);

    if (!NoAud)
    {
        SetAudioPos(samples);
    }

    *pStartTime = ticks() - (DWORD)(seconds * 1000.0);
}


void myexit(void)
{
   printf("The program has exited.\n");
}


int main(int argc, char *argv[])
{
    int NoAud;
    int endOfVideo = 0;
    int endOfAudio = 0;
    int ret;
    DWORD StartTime;

    atexit(myexit);

    /* 1. Argument Parsing */
    if (argc != 2 && argc != 3)
    {
        printf("USAGE: avi2 <path_to_AVI_file> [start_frame]\n\n");
        return 0;
    }

    if (argc == 3) StartAtFrame = (DWORD)atoi(argv[2]);

    /* 2. Initialization */
    if (initVideo(argv[1]))
    {
        printf("AVI error\n");
        return 0;
    }

    printf("Video: %dx%d @ %.1f fps, %d frames\n", 
           vWidth, vHeight, avi->fps, avi->num_video_frames);

    NoAud = InitWindowsAudio(&avi->Aud, avi->max_audio_chunk_size);
    if (NoAud)
    {
        endOfAudio = TRUE;
        printf("No audio available\n");
    }
    else
    {
        printf("Audio format: %d channels, %d bits, %d Hz, %d bytes/frame\n",
           avi->Aud.nChannels, avi->Aud.wBitsPerSample, avi->Aud.nSamplesPerSec,
           (avi->Aud.wBitsPerSample / 8) * avi->Aud.nChannels);
    }

    pPixels = InitWindows(avi->width, avi->height);
    if (!pPixels)
    {
        printf("Failed to initialize window/pixel buffer\n");
        return 1;
    }

    /* Synchronize all clocks to the starting position */
    FrameSeek(StartAtFrame, NoAud, &StartTime);

    /* 3. Main Message/Playback Loop */
    while (!endOfVideo || !endOfAudio)
    {
        /* A. Process GUI Messages */
        while (TRUE)
        {
            ret = ProcessGuiMessages();
            if (ret == 0) break;
            
            if (ret == WM_QUIT)
            {
                printf("Window closed by user\n");
//                endOfAudio = endOfVideo = TRUE;
                CloseWindowsAudio();  // CRITICAL: Stop audio immediately
                CloseVideo(avi);
                CloseGui();
                return 0;  // Exit cleanly
            }
            
            if (ret == WM_LBUTTONDOWN)
            {
                DWORD framesToSkip = (DWORD)(30.0 * avi->fps);
                DWORD newFrame = avi->current_video_frame + framesToSkip;
                printf("Seeking forward 30 seconds\n");

                if (newFrame >= avi->num_video_frames)
                    newFrame = avi->num_video_frames - 1;

                FrameSeek(newFrame, NoAud, &StartTime);
            }
        }

        /* B. Audio: Only add ONE chunk per loop iteration */
        if (!endOfAudio)
        {
            BYTE *ABuf = GetFreeWavBuffer();
            
            if (ABuf != NULL)
            {
                DWORD BufLen = AVI_ReadAframe(avi, NULL, 0);
                if (BufLen > 0)
                {
                    AVI_ReadAframe(avi, ABuf, BufLen);
                    AVI_WriteAframe(aviout, ABuf, BufLen);
                    AddChunkToWavQ(ABuf, BufLen);
                }
                else
                {
                    endOfAudio = 1;
                }
            }
        }

        /* C. Video Sync and Rendering */
        if (!endOfVideo)
        {
            DWORD targetFrame = GetMasterFrameTarget(NoAud, endOfAudio, StartTime);

            if (targetFrame >= avi->num_video_frames)
            {
                targetFrame = avi->num_video_frames;
                endOfVideo = TRUE;
            }

            if (targetFrame > avi->current_video_frame)
            {
                // Decode and display ONE frame per iteration
                // This keeps the GUI responsive
                if (GetFrame())
                {
                    printf("GetFrame failed at frame %d\n", avi->current_video_frame);
                    endOfVideo = 1;
                }
                else
                {
                    GuiShowFrame();

                    // Debug output every second
                #if defined(AVI_DEBUG)
                    if (avi->current_video_frame % (int)avi->fps == 0)
                    {
                        printf("Frame %d, target %d, audio frame %d\n",
                               avi->current_video_frame, targetFrame,
                               avi->current_audio_frame);
                    }
                #endif
                }
            }
            else
            {
                // We're ahead of schedule, sleep a bit
                GuiSleep(1);
            }
        }

        // If video done but audio still playing, sleep
        if (endOfVideo && !endOfAudio)
        {
            GuiSleep(10);
        }

        // Final safety break
        if (endOfVideo && endOfAudio) break;
    }

    /* 4. Safe Cleanup */
    CloseWindowsAudio();
    CloseVideo(avi);
    CloseGui();

    return 0;
}





// This returns the actual "hardware time" in milliseconds
// samples / samples_per_sec * 1000

DWORD GetAudioClockMs(void)
{
    double samples = (double)GetAudioPos();

    return (DWORD)((samples / (double)avi->Aud.nSamplesPerSec) * 1000.0);
}



// Tracks the system time when we last updated the audio position
static DWORD last_system_ticks = 0;
static DWORD last_audio_ms = 0;

DWORD GetMasterClockMs(int NoAud, int endOfAudio)
{
    if (!NoAud && !endOfAudio)
    {
        // Calculate MS from Audio Samples: (Samples / SamplesPerSec) * 1000
        double samples = (double)GetAudioPos();
        last_audio_ms = (DWORD)((samples / (double)avi->Aud.nSamplesPerSec) * 1000.0);
        last_system_ticks = ticks(); // Sync the OS timer to the audio clock
        return last_audio_ms;
    }
    else
    {
        // Audio is gone. Use the OS timer, but offset it by the last
        // known good audio position so there is no "jump" in time.
        if (last_system_ticks == 0) return ticks(); // Fallback if audio never started

        return last_audio_ms + (ticks() - last_system_ticks);
    }
}




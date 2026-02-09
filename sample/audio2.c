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


#ifdef _WIN32      // defined by Borland C
  #include <windows.h>
  #include <mmsystem.h>
#else
  #include <alsa/asoundlib.h>
  #include <string.h>
  #include <stdlib.h>
  #include <unistd.h>
  #include <pthread.h>
  #include <stdint.h>
  #include <inttypes.h>
  typedef uint64_t  QWORD;
  typedef uint32_t  DWORD;
  typedef uint16_t  WORD;
  typedef uint8_t   BYTE;
  typedef int64_t   QINT;
  typedef int32_t   LONG; // Guaranteed 4 bytes regardless of 32/64 bit


#endif

#include <math.h>

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


//#pragma comment(lib, "winmm.lib") // Link the winmm library automatically with MSVC

#define MAX_WHDR    5

// Platform-specific globals
#ifdef _WIN32
  static HWAVEOUT hWaveOut;
  static WAVEHDR whdr[MAX_WHDR];
#else
  static snd_pcm_t *pcm_handle = NULL;
  static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
  static int buffer_status[MAX_WHDR];  // 0 = free, 1 = in use
  static int64_t samples_written = 0;
  static int bytes_per_frame = 0;  // Will be calculated based on format
  static unsigned int sample_rate = 0;  // Store sample rate for buffer calculations
  static snd_pcm_uframes_t max_delay_frames = 0;  // Maximum allowed delay
#endif

static BYTE *WavBuf[MAX_WHDR];   // Store pointers to buffers
static DWORD AudioBaseSample = 0;    // Offset to GetAudioPos()

// Function prototypes
int InitWindowsAudio(STREAMFORMATAUD *Aud, DWORD max_chunk_size);
int AddChunkToWavQ(BYTE *Buffer, DWORD BufLen);
int CheckWavQ(void);
BYTE *GetFreeWavBuffer(void);
void CloseWindowsAudio(void);
DWORD GetAudioPos(void);
void SetAudioPos(DWORD startSample);


// Initialize audio output
// return non-zero on error

int InitWindowsAudio(STREAMFORMATAUD *Aud, DWORD max_chunk_size)
{
#ifdef _WIN32
    WAVEFORMATEX wfx;
    MMRESULT ret;
#else
    snd_pcm_hw_params_t *hw_params;
    int err;
#endif
    int i;

    if (max_chunk_size == 0)  // size uninitialized
    {
        return(-1);
    }

    // allocate buffers
    memset(WavBuf, 0, sizeof(WavBuf));  // clear pointers only
    WavBuf[0] = malloc(max_chunk_size * MAX_WHDR);
    if (!WavBuf[0])
    {
        return(-2);    // insufficient memory
    }

    // fix the pointers
    memset(WavBuf[0], 0, max_chunk_size * MAX_WHDR);
    for (i = 1; i < MAX_WHDR; i++)
        WavBuf[i] = WavBuf[i - 1] + max_chunk_size;

#ifdef _WIN32
    // Windows implementation
    memcpy(&wfx, Aud, sizeof(wfx));

    // Open the audio device
    ret = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (ret != MMSYSERR_NOERROR)
    {
        free(WavBuf[0]);   // free buffer memory
        WavBuf[0] = NULL;
        return((int) ret);
    }

    memset(whdr, 0, sizeof(whdr));
#else
    // Linux ALSA implementation

    // Calculate bytes per frame
    bytes_per_frame = (Aud->wBitsPerSample / 8) * Aud->nChannels;

    // Open PCM device for playback in BLOCKING mode
    // Blocking is GOOD - it naturally paces our audio to real-time
    err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
    {
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-3);
    }

    // Allocate hardware parameter structure
    snd_pcm_hw_params_alloca(&hw_params);

    // Initialize hardware parameter structure
    err = snd_pcm_hw_params_any(pcm_handle, hw_params);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-4);
    }

    // Set access type to interleaved
    err = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-5);
    }

    // Set sample format
    snd_pcm_format_t format;
    if (Aud->wBitsPerSample == 16)
        format = SND_PCM_FORMAT_S16_LE;
    else if (Aud->wBitsPerSample == 8)
        format = SND_PCM_FORMAT_U8;
    else
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-6);  // unsupported format
    }

    err = snd_pcm_hw_params_set_format(pcm_handle, hw_params, format);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-7);
    }

    // Set number of channels
    err = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, Aud->nChannels);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-8);
    }

    // Set sample rate
    unsigned int rate = Aud->nSamplesPerSec;
    err = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-9);
    }

    // Store the actual sample rate for later use
    sample_rate = rate;
    
    // Set maximum delay threshold: allow 0.5 seconds of buffering
    // This prevents audio from getting too far ahead of video
    max_delay_frames = sample_rate / 2;  // 0.5 seconds worth of frames

    // Apply hardware parameters
    err = snd_pcm_hw_params(pcm_handle, hw_params);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-10);
    }

    // Prepare the PCM for use
    err = snd_pcm_prepare(pcm_handle);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        free(WavBuf[0]);
        WavBuf[0] = NULL;
        return(-11);
    }

    // Initialize buffer status
    pthread_mutex_lock(&audio_mutex);
    memset(buffer_status, 0, sizeof(buffer_status));
    samples_written = 0;
    AudioBaseSample = 0;
    pthread_mutex_unlock(&audio_mutex);

//    printf("ALSA initialized: %d Hz, %d bytes/frame, max buffer delay: %lu frames (%.1f sec)\n",
//           sample_rate, bytes_per_frame, max_delay_frames,
//           (double)max_delay_frames / sample_rate);

#endif

    return(0);
}


// Add a chunk of audio to the wav queue
// Return 0 if successfully added, or non-zero if buffer full.

int AddChunkToWavQ(BYTE *Buffer, DWORD BufLen)
{
    int i;

#ifdef _WIN32
    if (!hWaveOut) return(1);

    // first, look for a free whdr
    for (i = 0; i < MAX_WHDR; i++)
        if (!whdr[i].lpData) break;  // found a free one.
    if (i == MAX_WHDR) return(1);    // no free buffers

    // Prepare the wave header
    memset(&whdr[i], 0, sizeof(WAVEHDR));   // clear previous junk
    whdr[i].lpData = (LPSTR) Buffer;
    whdr[i].dwBufferLength = BufLen;
    whdr[i].dwFlags = 0;
    whdr[i].dwLoops = 0;

    // Prepare memory (legacy 16 bit system throwback)
    waveOutPrepareHeader(hWaveOut, &whdr[i], sizeof(WAVEHDR));

    // Add to playback queue
    waveOutWrite(hWaveOut, &whdr[i], sizeof(WAVEHDR));
#else
    snd_pcm_sframes_t written;
    snd_pcm_uframes_t frames_to_write;

    if (!pcm_handle) return 1;

    // Find which index this pointer belongs to
    for (i = 0; i < MAX_WHDR; i++)
    {
        if (WavBuf[i] == Buffer) break;
    }
    if (i == MAX_WHDR) return 1;

    // Check if this buffer is already busy
    pthread_mutex_lock(&audio_mutex);
    if (buffer_status[i] == 1)
    {
        pthread_mutex_unlock(&audio_mutex);
        return 1;  // Buffer still in use
    }
    buffer_status[i] = 1; // Mark as BUSY
    pthread_mutex_unlock(&audio_mutex);

    // CRITICAL FIX: Calculate frames correctly based on actual format
    // A "frame" is one sample across all channels
    // bytes_per_frame = (bits_per_sample / 8) * num_channels
    frames_to_write = BufLen / bytes_per_frame;

    // This call will BLOCK if the ALSA buffer is full,
    // which naturally paces playback to real-time
    written = snd_pcm_writei(pcm_handle, Buffer, frames_to_write);

    if (written == -EPIPE)
    {
        // Underrun occurred, recover
        snd_pcm_prepare(pcm_handle);
        written = snd_pcm_writei(pcm_handle, Buffer, frames_to_write);
    }

    pthread_mutex_lock(&audio_mutex);
    if (written > 0)
    {
        samples_written += written;
    }
//    else if (written < 0)
//    {
//AVI_DBG_1s("ALSA write error: %s\n", snd_strerror(written));
//    }
    buffer_status[i] = 0; // Mark as FREE
    pthread_mutex_unlock(&audio_mutex);

    if (written < 0)
    {
        return 1;  // Error
    }
#endif

    return(0);
}


// Check the queue for buffers that have finished playing.
// If found, free them.  Return number of free buffers available.

int CheckWavQ(void)
{
    int i, freebufs = 0;

#ifdef _WIN32
    if (!hWaveOut) return(0);

    for (i = 0; i < MAX_WHDR; i++)
    {
        if (!whdr[i].lpData)   // unused
        {
            freebufs++;
        }
        else if ((whdr[i].dwFlags & WHDR_DONE))   // completed playback
        {
            waveOutUnprepareHeader(hWaveOut, &whdr[i], sizeof(WAVEHDR));
            freebufs++;
            whdr[i].lpData = NULL;   // mark empty
        }
    }
#else
    if (!pcm_handle) return(0);

    pthread_mutex_lock(&audio_mutex);
    for (i = 0; i < MAX_WHDR; i++)
    {
        if (buffer_status[i] == 0)
            freebufs++;
    }
    pthread_mutex_unlock(&audio_mutex);
#endif

    return(freebufs);
}


// Return a pointer to a free WAV buffer or NULL if none available.

BYTE *GetFreeWavBuffer(void)
{
    int i;

#ifdef _WIN32
    if (!hWaveOut) return(NULL);

    // Check to make sure there is at least one free buffer
    if (CheckWavQ() == 0) return(NULL);

    // find a free buffer
    for (i = 0; i < MAX_WHDR && whdr[i].lpData; i++);
    if (i == MAX_WHDR) return(NULL);   // this should never actually happen

    // return the corresponding buffer
    return(WavBuf[i]);
#else
    snd_pcm_sframes_t delay;
    int err;
    
    if (!pcm_handle) return(NULL);

    // Check how many frames are queued in ALSA's buffer
    err = snd_pcm_delay(pcm_handle, &delay);
    if (err < 0)
    {
        // If we get an error, recover and try to continue
        if (err == -EPIPE)
        {
            snd_pcm_prepare(pcm_handle);
        }
        delay = 0;
    }

    // CRITICAL: Limit buffer depth to prevent audio from getting too far ahead
    // This keeps audio-video sync tight
    if (delay > (snd_pcm_sframes_t)max_delay_frames)
    {
    #if defined(AVI_DEBUG)
        // Optional debug output (comment out after testing)
        static int warn_count = 0;
        if (warn_count++ < 5)
        {
            printf("Audio buffer full: %ld frames queued (max %lu), throttling...\n",
                   (long)delay, max_delay_frames);
        }
    #endif
        return NULL;  // Too much audio queued, don't add more yet
    }

    // Find a free software buffer
    pthread_mutex_lock(&audio_mutex);
    for (i = 0; i < MAX_WHDR; i++)
    {
        if (buffer_status[i] == 0)
        {
            pthread_mutex_unlock(&audio_mutex);
            return(WavBuf[i]);
        }
    }
    pthread_mutex_unlock(&audio_mutex);
    
    return(NULL);
#endif
}


void CloseWindowsAudio(void)
{
    int i;

#ifdef _WIN32
    if (!hWaveOut) return;

    // 1. Immediately stop the hardware from processing any more data
    waveOutReset(hWaveOut);

    // 2. Explicitly unprepare every header
    for (i = 0; i < MAX_WHDR; i++)
    {
        if (whdr[i].lpData)
        {
            waveOutUnprepareHeader(hWaveOut, &whdr[i], sizeof(WAVEHDR));
            whdr[i].lpData = NULL;
        }
    }

    // 3. Close the device handle
    waveOutClose(hWaveOut);
    hWaveOut = NULL;
#else
    if (!pcm_handle) return;

    // Drain any remaining audio
    snd_pcm_drain(pcm_handle);
    
    // Close the PCM device
    snd_pcm_close(pcm_handle);
    pcm_handle = NULL;
    
    pthread_mutex_destroy(&audio_mutex);
#endif

    // 4. Finally, free the memory
    if (WavBuf[0])
    {
        free(WavBuf[0]);
        WavBuf[0] = NULL;
    }
}


// Get the actual time from the audio hardware

DWORD GetAudioPos(void)
{
#ifdef _WIN32
    MMTIME mmt;

    if (!hWaveOut) return(0);

    mmt.wType = TIME_SAMPLES;
    waveOutGetPosition(hWaveOut, &mmt, sizeof(MMTIME));

    return(mmt.u.sample + AudioBaseSample);
#else
    snd_pcm_sframes_t delay = 0;
    DWORD current_pos;

    if (!pcm_handle) return(0);

    pthread_mutex_lock(&audio_mutex);
    int err = snd_pcm_delay(pcm_handle, &delay);

    if (err >= 0)
        current_pos = (samples_written > (int64_t)delay) ? (DWORD)(samples_written - delay) : 0;
    else
        current_pos = (DWORD)samples_written;

    pthread_mutex_unlock(&audio_mutex);
    return(current_pos + AudioBaseSample);
#endif
}


void SetAudioPos(DWORD startSample)
{
    int i;

#ifdef _WIN32
    if (!hWaveOut) return;

    // 1. Reset the Windows Audio Hardware
    waveOutReset(hWaveOut);

    // 2. Clear your internal software queue
    for (i = 0; i < MAX_WHDR; i++)
    {
        if (whdr[i].lpData)   // memory still active
        {
            waveOutUnprepareHeader(hWaveOut, &whdr[i], sizeof(WAVEHDR));
            whdr[i].lpData = NULL;   // mark empty
        }
    }

    // 3. Update the library's internal sample counter
    AudioBaseSample = startSample;
#else
    if (!pcm_handle) return;

    // Drop all pending audio
    snd_pcm_drop(pcm_handle);

    // Prepare for new playback
    snd_pcm_prepare(pcm_handle);

    pthread_mutex_lock(&audio_mutex);

    // Clear buffer status
    for (i = 0; i < MAX_WHDR; i++)
    {
        buffer_status[i] = 0;
    }

    samples_written = 0;
    AudioBaseSample = startSample;
    
    pthread_mutex_unlock(&audio_mutex);
#endif
}

# AVI Container Library

**AVI Container Library for Reading and Writing AVI 2.0 ODML Files**  
Written by Dennis Hawkins

This is an AVI Container library written entirely in ANSI C (not C++) and can be compiled with GCC and Borland C++ v5.02. While there are other libraries that do similar things, I was not able to find an open source one that was written in real C and also supported Open DML extensions. Without Open DML 'ODML' support, AVI files are limited to 2GB. This library works with both Windows and Linux.

## What This Library Can Do

This library has a simple open-read/write-close architecture that is very easy for the programmer to use. Here is the basic workflow:

### For Reading

```c
avi = AVI_Open("myVid.avi", FOR_READING | AUTO_INDEX, NULL);  // open the file
AVI_SeekStart(avi);                                           // Start at beginning
while (!done)
{
    AVI_ReadVframe(avi, VidBuf, BufSize, &keyframe);          // Read a video frame into buffer
    AVI_ReadAframe(avi, AudBuf, BufSize);                     // Read an audio frame into buffer
    // Do stuff
}
AVI_Close(avi);                                               // Close the file
```

### For Writing

```c
avi = AVI_Open("MyOutVid.avi", FOR_WRITING | HYBRID_ODML, NULL);   // Create a file for writing
AVI_SetVideo(avi, "Video", 1920, 1080, 24.0, 'MJPG');         // 1920x1080 @ 24 FPS using MJPG codec
AVI_SetAudio(avi, "Audio", 2, 22050, 16, MS_PCM);             // 16bit @ 22050 stereo
while (!done)
{
    // Do stuff to get audio and video frames
    AVI_WriteVframe(avi, VidBuf, BufLen, keyframe);           // Write video frame 
    AVI_WriteAframe(avi, AudBuf, BufLen);                     // Write audio frame      
}
AVI_Close(avi);                                               // Close the file
```

That's pretty much it. If you already know how to read and write files, then you already know how to use 90% of this library. It needs to be stated for those that don't know, that this is only an AVI container library. It will not try to compress/decompress anything. It just reads whatever is in the file. It does not automatically apply a CODEC. The programmer will need to send the extracted data to the appropriate CODEC for further processing.

## Thread Safety

This library is thread safe. You can open multiple files for simultaneous reading and writing in multiple threads. I have not done any exhaustive testing of this, but the code was written with multi-threaded operation in mind. However, this only applies to threads that contain the full workflow. So, you can open multiple files and operate on them in multiple threads, but the file you open in that thread must stay in that thread. That is, you cannot open an AVI file in one thread, write video in another, and write audio in yet another. What you can do is have 100 threads, each running their own workflow, and operating on one file for each thread.

Another thing that is possible is that you can open multiple files in a single thread. So it's trivial to open a file for reading and then another for writing, both at the same time. The `AVI_Open()` function returns a unique file pointer that is used to differentiate between the files. This works exactly like `fopen()` in the standard C library.

## Known Limitations

- Currently only one video and one audio track are allowed
- You can have video without audio, but you must have video
- In non-ODML mode, files are limited to 2GB. If reading an oversize file, the results are unpredictable
- In ODML mode, files are limited to 128 RIFF segments which can go to about 150GB in total
- When writing, the programmer must write the audio and video frames at the same time or else they will lose sync
- When writing, the programmer must handle the situation where the file already exists

## Sample Program

A sample program `avi2.c` is included for those that want to see an example of the library in action. The sample program will read an AVI file from the command line, create a new AVI file using the original file name with "out.avi" concatenated (that is, "MyVideo.avi" in, "MyVideo.aviout.avi" out), and display the video in a window on the screen. The sample program can compress/decompress MJPG video and also play the audio to the system speakers. I have to say that 95% of the complexity of the sample program was the result of code required for playback.

The MJPG codec is called Motion JPEG. This is a very simple, yet powerful, codec. The sample program leverages the Linux built-in LibJpeg library. For Windows, a special built version for Borland C v5.02 is included. When compiling with MinGW, a function that uses Windows OLE library is used.

To make this compile and run under both Windows and Linux, I wrote wrapper functions that call the appropriate GUI functions depending on which compiler is used. The wrapper functions are designed to be independent of this program so that anybody can use them in other, unrelated programs, if they want sound and GUI cross compatibility between Linux and Windows without having to change their source code.

## Compiling The Sample Program

The libraries and sample program were written in ANSI C with `//` comments. Essentially, this is ANSI C with Borland extensions. Because of this, the source files are backwards compatible with older compilers. So if you are using a fairly modern compiler from 1995 up, you should have no trouble compiling. In my testing, the code works for both 32 and 64 bit compiles.

### Directory Setup

To set up the proper directories, copy the sample directory to a working area of your choice. Then copy the source directory under that. In the source directory, make sure you have the following files:

- `avi2_common.c`
- `avi2_Read.c`
- `avi2_write.c`
- `file64.c`
- `avi2.h`

These files are the actual library. You can include them as is in your project, or you can combine them into a library file of your choice (`avi2.lib`, `avi2.a` or `avi2.so`).

Now, back up in your working directory, make sure the following files are there:

- `audio2.c`
- `avi2.c`
- `gui.c`
- `jpg2raw.c`
- `jconfig.hh`
- `jerror.hh`
- `jinclude.hh`
- `jmorecfg.hh`
- `jpeglib.hh`
- `jpeg6lib.lib`

You might notice that there are several `.HH` files. These are standard include files for the JpegLib.lib that is compiled for Borland. They have been renamed because of a version incompatibility with the newer versions used by Linux. If you don't plan on using Borland, you can just delete the `.HH` and `.LIB` files here.

### Compiling with Borland C++ v5.02

To compile with Borland C++ v5.02, just open the IDE and add all of the above `.C` and `.LIB` files to the project. Hit F9 and the compiler will spit out `avi2.exe`. Borland doesn't work with 64 bit targets. Borland supports 16 bit Windows, but this library will not work in 16 bit Windows.

If you have Borland, and you prefer the command line, use this:

```bash
bcc32.exe -4 -Isource avi2.c audio2.c gui.c source/avi2_common.c source/avi2_read.c source/avi2_write.c source/file64.c jpg2raw.c jpeg6lib.lib
```

### Compiling on Linux for Linux

```bash
# 32-bit
gcc -m32 avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm

# 64-bit
gcc -m64 avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm
```

### Cross-Compiling on Linux for Windows

```bash
# 32-bit
i686-w64-mingw32-gcc avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c winjpeg.c -o avi2.exe -I. -I./source -O2 -lgdi32 -luser32 -lole32 -loleaut32 -lwinmm

# 64-bit
x86_64-w64-mingw32-gcc avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c winjpeg.c -o avi2.exe -I. -I./source -O2 -lgdi32 -luser32 -lole32 -loleaut32 -lwinmm
```

### Compiling on Linux with Tiny C

```bash
# 32-bit
tcc -m32 -w avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm

# 64-bit
tcc -m64 -w avi2.c gui.c source/file64.c audio2.c source/avi2_write.c source/avi2_Read.c source/avi2_common.c jpg2raw.c -o avi2.exe -I. -I./source -lX11 -ljpeg -lasound -lm
```

### Notes on Compilation

This code is not tested with Microsoft Visual C++, but it should compile and link with a minimum number of changes. I don't use Microsoft compilers so I can't say for sure. If you get warnings about "multicharacter literals" or "multicharacter constants", you need to change your compiler options to ignore that warning.

I make extensive use of multicharacter literals to make the code efficient. It's not a bug, it's intentional. Using multicharacter literals makes FourCC operations take only one CPU instruction, whereas any other way requires function calls.

### Linux System Libraries

When compiling the sample program for Linux, you will need to preinstall the following Linux system libraries if they are not already on your computer:

```bash
sudo apt update
sudo apt upgrade
sudo apt install libx11-dev
sudo apt install libasound2-dev
sudo apt install libjpeg-turbo8-dev
```

If you have problems installing `libjpeg-turbo8-dev`, then try:

```bash
sudo apt install libjpeg-dev
```

Most Linux developers will already have these installed. However, these are only needed for the sample program and are not required if you only want to use the AVI library by itself.

## Library Functions

### File I/O

#### `AVI_Open()`

```c
AVI2 *AVI_Open(const char *filename, DWORD mode, int *err);
```

Open an AVI file and return a file pointer.

**Parameters:**
- `filename` - null terminated string containing the full path to the file name
- `mode` - either `FOR_READING` or `FOR_WRITING`. May be OR'ed with modifiers
- `err` - pointer to an integer that will receive an error code. May be NULL if the error code is not needed

**Returns:**  
If successfully opened, the function will return a valid AVI2 file pointer. Returns NULL on failure and `err`, if supplied, will have the error code.

**Important:** This function does not check to see if the file already exists when opened in `FOR_WRITING` mode. It is the programmer's responsibility to check to see if the file already exists. Otherwise, the function will overwrite any pre-existing file by the same name.

**Mode Modifiers Available for `FOR_READING` Only:**
- `AUTO_INDEX` - If the legacy AVI file does not have a valid index, then a temporary index will be built based on the order of chunks in the 'MOVI' list. Note that this only works on legacy AVI files because ODML files must have an index. If an index is generated, it can cause a significant delay in opening the file.

**Mode Modifiers Available for `FOR_WRITING` Only:**
- `HYBRID_ODML` - A hybrid file is generated such that a legacy player will be able to play the first RIFF chunk, but modern players will play entire file which can be up to 128GB in size
- `STRICT_LEGACY` - Will only write a single RIFF segment legacy file less than 2GB in size. No ODML indexes will be written. Attempts to write files > 2GB are ignored and such files will be truncated without warning
- `STRICT_ODML` - Writes a pure ODML file. This file can be up to 128 GB in size. No legacy index is written. The file cannot be played on legacy players

#### `AVI_Close()`

```c
int AVI_Close(AVI2 *avi);
```

Close all the files and buffers associated with the AVI2 file pointer.

#### `AVI_SeekStart()`

```c
int AVI_SeekStart(AVI2 *avi);
```

Seek both audio and video in the AVI file to the first frame.

### Writing Files

#### `AVI_SetVideo()`

```c
int AVI_SetVideo(AVI2 *avi, char *name, DWORD width, DWORD height, double fps, FOURCC codec);
```

Initialize the video parameters for writing. This function must be called before writing any frames to the file. A video stream is mandatory.

**Parameters:**
- `name` - Name for the stream, usually "Video Stream"
- `width`, `height` - dimensions in pixels of the video frame
- `fps` - frames per second of the file written. Note that changing this only changes the speed at which the player plays the frames. It does not do any interpolation or make an attempt to add or delete frames
- `codec` - A FourCC code representing the codec - like `'MJPG'`

#### `AVI_SetAudio()`

```c
int AVI_SetAudio(AVI2 *avi, char *name, int NumChannels, long SamplesPerSecond, long BitsPerSample, long codec);
```

Initialize the audio parameters for writing. This function must be called before writing any audio chunks to the file. An audio stream is optional.

**Parameters:**
- `name` - Name for the stream, usually "Audio Stream"
- `NumChannels` - 1 for mono, 2 for stereo
- `SamplesPerSecond` - Sample rate, like 22,050
- `BitsPerSample` - Number of bits in a single sample, like 16
- `codec` - A predefined codec value like `WAVE_FORMAT_PCM`

#### `AVI_WriteVframe()`

```c
int AVI_WriteVframe(AVI2 *avi, BYTE *VidBuf, DWORD Len, int keyframe);
```

Write a video frame to the file.

**Parameters:**
- `VidBuf` - Pointer to the video data
- `Len` - The number of valid bytes in VidBuf
- `keyframe` - TRUE if the codec considers the frame to be a Key Frame

#### `AVI_WriteAframe()`

```c
int AVI_WriteAframe(AVI2 *avi, BYTE *AudBuf, DWORD Len);
```

Write an Audio chunk to the file.

**Parameters:**
- `AudBuf` - Pointer to Audio Data
- `Len` - The number of valid bytes in AudBuf

### Reading Files

#### `AVI_ReadVframe()`

```c
DWORD AVI_ReadVframe(AVI2 *avi, BYTE *VidBuf, DWORD VidBufSize, int *keyframe);
```

Read the current video frame from the file.

**Returns:**  
0 if there was an error and `avi->AVIerr` holds the error code. If `VidBufSize` is NULL, the function will return the size of the buffer required to hold the frame. If read is successful, the current video frame counter is incremented.

**Parameters:**
- `VidBuf` - Buffer that will receive the frame data
- `VidBufSize` - Sizeof(Buffer)
- `keyframe` - TRUE if the frame was recorded as a Key Frame by the codec

#### `AVI_ReadAframe()`

```c
DWORD AVI_ReadAframe(AVI2 *avi, BYTE *AudioBuf, DWORD BufSize);
```

Reads the current audio chunk from the file.

**Returns:**  
0 if there was an error and `avi->AVIerr` holds the error code. If `BufSize` is NULL, the function will return the size of the buffer required to hold the chunk. If read is successful, the current audio frame counter is incremented.

---

## License

Please refer to the original source for licensing information.

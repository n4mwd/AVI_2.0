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

// For Borland C v5.02, make sure the jpeg6lib.lib
// file is included.
// For gcc, you need to first install
//    "sudo apt install libjpeg-dev"

#if defined(__BORLANDC__) || defined(__GNUC__) || defined(__TINYC__)

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

// Use system headers for Linux, local headers for Borland
#if defined(__BORLANDC__)
  #define HAVE_PROTOTYPES
  #define HAVE_BOOLEAN
  #define SIZEOF(object)	((size_t) sizeof(object))
  #include "jpeglib.hh"
  #include "jerror.hh"
#elif defined(__MINGW32__) || defined(__MINGW64__)
  #define HAVE_PROTOTYPES
  #define HAVE_BOOLEAN
  #define SIZEOF(object)	((size_t) sizeof(object))
  #include "jpeglib.hh"
  #include "jerror.hh"
#else
  // For GCC/Linux: use system jpeglib headers
  #include <jpeglib.h>
  #include <jerror.h>
#endif

typedef unsigned char BYTE;

#if defined(__BORLANDC__)
  typedef unsigned __int32  DWORD;
#else
  #include <stdint.h>  // Required for GCC/MinGW
  typedef uint32_t DWORD;
#endif

struct my_error_mgr
{
  struct jpeg_error_mgr pub;	/* "public" fields */

  jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;


// prototype for src setup function
void jpeg_memory_src(j_decompress_ptr cinfo, const JOCTET * buffer, size_t bufsize);


/*
 * Here's the routine that will replace the standard error_exit method:
 */

//METHODDEF(void)
void
my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}

// Decompress JPEG into memory buffer at image x,y offset
// RawBuf buffer of size RawSize.
// JpgPtr is pointer to compressed jpeg to decopmpress.
// JpgLen is the length of compressed jpeg buffer.
// Delta offset position.
// Return zero if success, else -1.

int Jpg2Raw(BYTE *DecodedBuf, DWORD DecodedBufSize, BYTE *JpgPtr, DWORD JpgLen, int ClrSpc)
{
    struct jpeg_decompress_struct incinfo;
    //struct jpeg_error_mgr jinerr;
    JSAMPARRAY buffer;	        /* Output row buffer */
    int row_stride_in;
    BYTE *RowStart;
    DWORD BufSize;
    struct my_error_mgr jerr;
    int DstRow, SrcCol, DstCol, Dx;


    /* Step 1: allocate and initialize JPEG decompression object */

    /* We set up the normal JPEG error routines, then override error_exit. */
    incinfo.err = jpeg_std_error(&jerr.pub);

    /* Now we can initialize the JPEG decompression object. */
    jpeg_create_decompress(&incinfo);

    // Set up for memory reads
    jpeg_memory_src(&incinfo, JpgPtr, JpgLen);   // specify data source

    /* Step 3: read file parameters with jpeg_read_header() */

    jpeg_read_header(&incinfo, TRUE);
    incinfo.output_components = (ClrSpc == JCS_GRAYSCALE) ? 1 : 3;		/* # of color components per pixel */
    incinfo.out_color_space = ClrSpc;
    jpeg_start_decompress(&incinfo);

//    if (size)
//    {
//        size->x = incinfo.output_width;
//        size->y = incinfo.output_height;
//    }


    /* JSAMPLEs per row in output buffer */
    row_stride_in = incinfo.output_width * incinfo.output_components;
    BufSize = incinfo.output_height * row_stride_in;

    /* Make a one-row-high sample array that will go away when done with image */
    buffer = (*incinfo.mem->alloc_sarray)
             ((j_common_ptr) &incinfo, JPOOL_IMAGE, row_stride_in, 1);

    if (DecodedBufSize < BufSize)
    {
        // Abort the read
        DecodedBufSize = 0;
        goto error;
    }

    Dx = 0; //Delta.x * incinfo.output_components;
    RowStart = DecodedBuf;
    DstRow = 0; //Delta.y;
    if (DstRow > 0)
        RowStart += (row_stride_in * DstRow);

    while (incinfo.output_scanline < incinfo.output_height)
    {
        jpeg_read_scanlines(&incinfo, buffer, 1);

        // here we copy to the destination buffer and apply the offset
        if (DstRow >= 0 && DstRow < (int) incinfo.output_height)
        {
            // Row is valid, copy line with offset
            // Calculate the starting byte of src and dst

            SrcCol = DstCol = 0;
            if (Dx < 0)
                SrcCol = -Dx;
            else if (Dx > 0)
                DstCol = Dx;
            // The length is minus the sum of the Src and Dst Col bytes
            // Since only one can be the offset and is a positive number.
            memcpy(RowStart + DstCol, buffer[0] + SrcCol,
                   row_stride_in - SrcCol - DstCol);

            RowStart += row_stride_in;
        }

        DstRow++;
    }

error:
    jpeg_finish_decompress(&incinfo);

    /* This is an important step since it will release a good deal of memory. */
    jpeg_destroy_decompress(&incinfo);

    return(DecodedBufSize ? 0 : -1);
}













/*
A data source manager provides five methods:

init_source (j_decompress_ptr cinfo)
	Initialize source.  This is called by jpeg_read_header() before any
	data is actually read.  Unlike init_destination(), it may leave
	bytes_in_buffer set to 0 (in which case a fill_input_buffer() call
	will occur immediately).

fill_input_buffer (j_decompress_ptr cinfo)
	This is called whenever bytes_in_buffer has reached zero and more
	data is wanted.  In typical applications, it should read fresh data
	into the buffer (ignoring the current state of next_input_byte and
	bytes_in_buffer), reset the pointer & count to the start of the
	buffer, and return TRUE indicating that the buffer has been reloaded.
	It is not necessary to fill the buffer entirely, only to obtain at
	least one more byte.  bytes_in_buffer MUST be set to a positive value
	if TRUE is returned.  A FALSE return should only be used when I/O
	suspension is desired (this mode is discussed in the next section).

skip_input_data (j_decompress_ptr cinfo, long num_bytes)
	Skip num_bytes worth of data.  The buffer pointer and count should
	be advanced over num_bytes input bytes, refilling the buffer as
	needed.  This is used to skip over a potentially large amount of
	uninteresting data (such as an APPn marker).  In some applications
	it may be possible to optimize away the reading of the skipped data,
	but it's not clear that being smart is worth much trouble; large
	skips are uncommon.  bytes_in_buffer may be zero on return.
	A zero or negative skip count should be treated as a no-op.

resync_to_restart (j_decompress_ptr cinfo, int desired)
	This routine is called only when the decompressor has failed to find
	a restart (RSTn) marker where one is expected.  Its mission is to
	find a suitable point for resuming decompression.  For most
	applications, we recommend that you just use the default resync
	procedure, jpeg_resync_to_restart().  However, if you are able to back
	up in the input data stream, or if you have a-priori knowledge about
	the likely location of restart markers, you may be able to do better.
	Read the read_restart_marker() and jpeg_resync_to_restart() routines
	in jdmarker.c if you think you'd like to implement your own resync
	procedure.

term_source (j_decompress_ptr cinfo)
	Terminate source --- called by jpeg_finish_decompress() after all
	data has been read.  Often a no-op.

For both fill_input_buffer() and skip_input_data(), there is no such thing
as an EOF return.  If the end of the file has been reached, the routine has
a choice of exiting via ERREXIT() or inserting fake data into the buffer.
In most cases, generating a warning message and inserting a fake EOI marker
is the best course of action --- this will allow the decompressor to output
however much of the image is there.  In pathological cases, the decompressor
may swallow the EOI and again demand data ... just keep feeding it fake EOIs.
jdatasrc.c illustrates the recommended error recovery behavior.

term_source() is NOT called by jpeg_abort() or jpeg_destroy().  If you want
the source manager to be cleaned up during an abort, you must do it yourself.

You will also need code to create a jpeg_source_mgr struct, fill in its method
pointers, and insert a pointer to the struct into the "src" field of the JPEG
decompression object.  This can be done in-line in your setup code if you
like, but it's probably cleaner to provide a separate routine similar to the
jpeg_stdio_src() routine of the supplied source manager.

For more information, consult the stdio source and destination managers
in jdatasrc.c and jdatadst.c.

*/


/* Expanded data source object for memory input */

typedef struct {
  struct jpeg_source_mgr pub;	/* public fields */

  JOCTET eoi_buffer[2];		/* a place to put a dummy EOI */
} my_source_mgr;

typedef my_source_mgr * my_src_ptr;


/*
 * Initialize source --- called by jpeg_read_header
 * before any data is actually read.
 */

//METHODDEF(void)
static void
init_source (j_decompress_ptr cinfo)
{
  /* No work, since jpeg_memory_src set up the buffer pointer and count.
   * Indeed, if we want to read multiple JPEG images from one buffer,
   * this *must* not do anything to the pointer.
   */
}


/*
 * Fill the input buffer --- called whenever buffer is emptied.
 *
 * In this application, this routine should never be called; if it is called,
 * the decompressor has overrun the end of the input buffer, implying we
 * supplied an incomplete or corrupt JPEG datastream.  A simple error exit
 * might be the most appropriate response.
 *
 * But what we choose to do in this code is to supply dummy EOI markers
 * in order to force the decompressor to finish processing and supply
 * some sort of output image, no matter how corrupted.
 */

//METHODDEF(boolean)
static boolean
fill_input_buffer (j_decompress_ptr cinfo)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;

  WARNMS(cinfo, JWRN_JPEG_EOF);

  /* Create a fake EOI marker */
  src->eoi_buffer[0] = (JOCTET) 0xFF;
  src->eoi_buffer[1] = (JOCTET) JPEG_EOI;
  src->pub.next_input_byte = src->eoi_buffer;
  src->pub.bytes_in_buffer = 2;

  return TRUE;
}


/*
 * Skip data --- used to skip over a potentially large amount of
 * uninteresting data (such as an APPn marker).
 *
 * If we overrun the end of the buffer, we let fill_input_buffer deal with
 * it.  An extremely large skip could cause some time-wasting here, but
 * it really isn't supposed to happen ... and the decompressor will never
 * skip more than 64K anyway.
 */

//METHODDEF(void)
static void
skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;

  if (num_bytes > 0) {
    while (num_bytes > (long) src->pub.bytes_in_buffer) {
      num_bytes -= (long) src->pub.bytes_in_buffer;
      (void) fill_input_buffer(cinfo);
      /* note we assume that fill_input_buffer will never return FALSE,
       * so suspension need not be handled.
       */
    }
    src->pub.next_input_byte += (size_t) num_bytes;
    src->pub.bytes_in_buffer -= (size_t) num_bytes;
  }
}


/*
 * An additional method that can be provided by data source modules is the
 * resync_to_restart method for error recovery in the presence of RST markers.
 * For the moment, this source module just uses the default resync method
 * provided by the JPEG library.  That method assumes that no backtracking
 * is possible.
 */


/*
 * Terminate source --- called by jpeg_finish_decompress
 * after all data has been read.  Often a no-op.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

//METHODDEF(void)
static void
term_source (j_decompress_ptr cinfo)
{
  /* no work necessary here */
}


/*
 * Prepare for input from a memory buffer.
 */

void
jpeg_memory_src (j_decompress_ptr cinfo, const JOCTET * buffer, size_t bufsize)
{
  my_src_ptr src;

  /* The source object is made permanent so that a series of JPEG images
   * can be read from a single buffer by calling jpeg_memory_src
   * only before the first one.
   * This makes it unsafe to use this manager and a different source
   * manager serially with the same JPEG object.  Caveat programmer.
   */
  if (cinfo->src == NULL) {	/* first time for this JPEG object? */
    cinfo->src = (struct jpeg_source_mgr *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  sizeof(my_source_mgr));
  }

  src = (my_src_ptr) cinfo->src;
  src->pub.init_source = init_source;
  src->pub.fill_input_buffer = fill_input_buffer;
  src->pub.skip_input_data = skip_input_data;
  src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
  src->pub.term_source = term_source;

  src->pub.next_input_byte = buffer;
  src->pub.bytes_in_buffer = bufsize;
}

#endif



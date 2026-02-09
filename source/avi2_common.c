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

// avi2_common.c
// Common functions used by both reading and writing AVI files

#include "avi2.h"



// Open an AVI file for reading and parse its structure
// if OpenMode is FOR_READING, else if FOR_WRITING
// just create the file and AVI2 structure and return -
// overwriting any previously existing file of the same name.
// It is the caller's responsibility to determine if the
// file already exists.  Returns NULL if there was an error
// either with the file open, malloc() or parsing.  If err is
// not NULL, it will contain an error code.  If opening for
// writing, OpenMode can be OR'ed with HYBRID_ODML, STRICT_LEGACY,
// or STRICT_ODML. This defaults to HYBRID_ODML if not supplied.
// If opening for reading, OpenMode can be OR'ed with AUTO_INDEX
// which will cause a temporary index to be generated on the fly
// if the AVI file didn't actually have one.  If not supplied, and
// no index is in the file, an error will be generated.

AVI2 *AVI_Open(const char *filename, DWORD OpenMode, int *err)
{
    AVI2 *avi;
    MFILE *fp;
    WORD OdmlMode = (WORD)(OpenMode & 0xFF00);

    OpenMode &= 0x00FF;

    if (err) *err = AVIERR_NO_ERROR;

    if (OpenMode == FOR_READING)
    {
        // Open file for reading
        fp = File64Open((char *)filename, "rb");
        if (!fp)
        {
            if (err) *err = AVIERR_FILE_NOT_EXIST;  // File does not exist or is unreadable
            return NULL;
        }

        // Allocate and initialize AVI2 structure
        avi = (AVI2 *)malloc(sizeof(AVI2));
        if (!avi)
        {
            File64Close(fp);
            if (err) *err = AVIERR_MALLOC;  // Out of memory
            return NULL;
        }

        memset(avi, 0, sizeof(AVI2));
        avi->fp = fp;
        avi->filemode = FOR_READING;
        avi->ODMLmode = OdmlMode;

        // Parse the file
        if (ParseAVIFile(avi) != 0)
        {
            // Error occurred during parsing
            // Close everything down and free memory
            // Don't alter AVIerr here.  Let error pass through.
            if (err) *err = avi->AVIerr;
            if (avi->VidRt.Idx)   free(avi->VidRt.Idx);
            if (avi->AudRt.Idx)   free(avi->AudRt.Idx);
            free(avi);
            File64Close(fp);
            return NULL;
        }

        // Everything looks good at this point.
    }
    else    // Open for writng
    {
        BYTE filler[2048];

        // Create file for writing
        fp = File64Open((char *)filename, "wb");
        if (!fp)
        {
            if (err) *err = AVIERR_CANT_CREATE_FILE;  // Can't create file
            return NULL;
        }

        // Allocate and initialize AVI2 structure
        avi = (AVI2 *)malloc(sizeof(AVI2));
        if (!avi)
        {
            File64Close(fp);
            if (err) *err = AVIERR_MALLOC;  // Out of memory
            return(NULL);
        }

        memset(avi, 0, sizeof(AVI2));  // clear structure
        avi->fp = fp;
        avi->filemode = FOR_WRITING;
        avi->ODMLmode = OdmlMode;

        // Write the first 2K of zeros to reserve for basic headers
        memset(filler, 0, sizeof(filler));
        File64Write(fp, filler, sizeof(filler));
    }
    File64SetBase(fp, 0);   // Start out at beginning

    return(avi);
}



// Close an AVI file opened for reading or writing
// Returns an error code.

int AVI_Close(AVI2 *avi)
{
    int err, err2;

    if (avi)
    {
        // If in write mode, we need to finish writing buffers
        // and do final file cleanup.
        if (avi->filemode == FOR_WRITING)
            err = FinalizeWrite(avi);
        if (avi->AudRt.Idx) free(avi->AudRt.Idx);
        if (avi->VidRt.Idx) free(avi->VidRt.Idx);
        err2 = File64Close(avi->fp);
        free(avi);
        if (err == AVIERR_NO_ERROR) err = err2;
    }
    else err = AVIERR_AVI_STRUCT_BAD;

    return(err);
}


// Return AVIerr as a pointer to a static string
char *AVI_StrError(int errnum)
{
    static char *errors[AVIERR_COUNT] =
    {
        "avi2 - No Error",
        "avi2 - Could not create AVI file",
        "avi2 - File does not exist or is unreadable",
        "avi2 - File is corrupted",
        "avi2 - Unable to write AVI header",
        "avi2 - Function incompatible with mode the file was opened",
        "avi2 - Frame not in file",
        "avi2 - No Index found",
        "avi2 - Buffer too small",
        "avi2 - No more frames",
        "avi2 - Too many audio channels",
        "avi2 - AVI file missing video or MOVI list",
        "avi2 - Error closing AVI file",
        "avi2 - Out of memory",
        "avi2 - AVI Structure Bad",
        "avi2 - Unsupported Feature",
        "avi2 - The file could not be created",
        "avi2 - A function parameter is invalid",
        "avi2 - The Stream is invalid",
        "avi2 - Function called out of order",
        "svi2 - Overflow",
        "avi2 - File too large",
        "avi2 - Unknown Error"
    };

    if (errnum < 0 || errnum >= AVIERR_COUNT)
        errnum = AVIERR_UNKNOWN;  // Unknown Error

    return(errors[errnum]);
}

// Read four bytes from the current position in the file and convert
// to a Little Endian integer.  If StreamNum is not NULL, and the FCC
// chars are '##db', '##dc', '##wb', '##tx', or 'ix##' (where ##
// represents a stream number like '00' or '02'), the stream number
// portion is returned in StreamNum as an integer.  Also, the FCC is
// converted to a standard form like '##dc' (actual '#' characters) and
// returned instead of the original like '00dc' or '01dc'.  This is so
// the program can compare against a consistent value.  The ## stream
// number portion of the fcc is a 2 digit decimal number represented by
// two ascii characters with a leading '0' if the stream number is less
// than 10.  If StreamNum is not null, and no '##' digits were found,
// StreamNum is set to -1 to indicate that the stream number is invalid.
// If there was a problem reading the file, the function returns -1.

FOURCC ReadFCC(MFILE *in, int *StreamNum)
{
    char Buf[15];
    FOURCC val;
    int  ret;

    memset(Buf, 0, sizeof(Buf));
    if (!in)return(-1);  // no file to read
    if (StreamNum) *StreamNum = -1;

    ret = File64Read(in, Buf, 4);
    if (ret != 4) return(-1);    // EOF

    val = *((FOURCC *)Buf);  // val is LE when CPU is LE


    // Note that both '##ix' and 'ix##' can exist
    if (StreamNum)   // attempt to get stream number
    {
        Buf[4] = ',';  // add comma for search
        *StreamNum = -1;

        // Check if 'ix##'
        if (Buf[0] == 'i' && Buf[1] == 'x')  // special case for 'ix##'
        {
            if (isdigit(Buf[2]) && isdigit(Buf[3]))
                *StreamNum = (Buf[2] - '0') * 10 + (Buf[3] - '0');
            Buf[2] = Buf[3] = '#';           // standardize FourCC
        }

        // check if '##db', '##dc', '##wb', '##tx', '##ix'
        else if (strstr("dc,db,wb,ix,tx,pc,", Buf + 2))  // '##dc' etc
        {
            if (isdigit(Buf[0]) && isdigit(Buf[1]))
                *StreamNum = (Buf[0] - '0') * 10 + (Buf[1] - '0');
            Buf[0] = Buf[1] = '#';           // standardize FourCC
        }

        if (*StreamNum == -1) return(-1);
        val = *((FOURCC *)Buf);
    }

    val = FIX_LIT(val);      // FOURCC should now be in correct endian order

    return(val);   // return FOURCC

}


// Write a FOURCC to the file at its current location.
// If the characters '##' are found in fccval, they are substituted with a
// stream number designated by StreamNum.  StreamNum must be a value
// from 0 to 99.  If out of bounds, it is converted to zero with no error.
// StreamNum is not used when there is no '##' in the fccval.  The function
// will internally convert multi-character literals, like 'movi', to the
// proper endian order before writing to the file.  The function will
// return the number of bytes written (should be 4), or less on error.

int WriteFCC(MFILE *out, FOURCC fccval, int StreamNum)
{
    char Buf[4];
    FOURCC fixedval;

    if (!out) return 0;  // no file to write

    // Ensure StreamNum is in valid range
    if (StreamNum < 0 || StreamNum > 99) StreamNum = 0;

    // Apply FIX_LIT to ensure proper endian order
    fixedval = FIX_LIT(fccval);

    // Copy FOURCC to buffer
    memcpy(Buf, &fixedval, 4);

    // Check if we need to substitute stream number
    if (Buf[0] == '#' && Buf[1] == '#')
    {
        // Convert stream number to two digit decimal string
        Buf[0] = (char)('0' + (StreamNum / 10));
        Buf[1] = (char)('0' + (StreamNum % 10));
    }
    else if (Buf[2] == '#' && Buf[3] == '#')
    {
        // Handle 'ix##' case
        Buf[2] = (char)('0' + (StreamNum / 10));
        Buf[3] = (char)('0' + (StreamNum % 10));
    }
    
    // Write the 4 bytes
    return(File64Write(out, Buf, 4));
}



// This function is only used if the compiler treats multi-character literals
// as BIG ENDIAN order.
// It reverses the order of a literal into Little Endian order and is called
// by FIX_LIT() macro

#if !defined(__BORLANDC__)
// Make a compatible string out of a FourCC
// There is no zero terminator.
// Cannot be called more than once in a printf().
char *Fcc2Str(FOURCC val)
{
    static DWORD tval;  // NOT THREAD SAFE
    tval = FIX_LIT(val);
    return((char *)&tval);
}

#endif




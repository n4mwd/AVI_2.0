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

#if defined(__WIN32__) && !defined(__BORLANDC__)

#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include <windows.h>
#include <objbase.h> 
#include <ole2.h>    
#include <olectl.h>  
#include <urlmon.h>
#include <ocidl.h>

/**
 * WinJpg2Raw
 * Decodes a JPEG buffer to a raw 24-bit BGR buffer using Windows Imaging.
 * Returns: 0 on success, 1 on failure.
 */
int WinJpg2Raw(unsigned char* OutBuf, DWORD OutBufSize, unsigned char* InBuf, DWORD InBufSize)
{
    IStream* pStream = NULL;
    IPicture* pPicture = NULL;
    HBITMAP hSrcBmp = NULL;
    HDC hdc = NULL;
    int success = 0;
    void *pDest = NULL;
    HGLOBAL hGlobal = NULL;

    // 1. Allocate global memory for the encoded buffer
    hGlobal = GlobalAlloc(GMEM_MOVEABLE, InBufSize);
    if (!hGlobal) return 1;

    // 2. Copy input data to the allocated memory
    pDest = GlobalLock(hGlobal);
    if (pDest) {
        memcpy(pDest, InBuf, InBufSize);
        GlobalUnlock(hGlobal);
    } else {
        GlobalFree(hGlobal);
        return 1;
    }

    // 3. Create a stream. TRUE means pStream->Release() will call GlobalFree(hGlobal).
    if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) == S_OK)
    {
        // 4. Decode JPEG to an IPicture object
        if (OleLoadPicture(pStream, InBufSize, FALSE, &IID_IPicture, (void**)&pPicture) == S_OK)
        {
            long hmWidth, hmHeight;
            BITMAPINFO bmi;

            memset(&bmi, 0, sizeof(bmi));
            
            // Get the internal bitmap handle and dimensions (in HiMetric units)
            pPicture->lpVtbl->get_Handle(pPicture, (OLE_HANDLE*)&hSrcBmp);
            pPicture->lpVtbl->get_Width(pPicture, &hmWidth);
            pPicture->lpVtbl->get_Height(pPicture, &hmHeight);

            // 5. Get Device Context to calculate pixels and extract bits
            hdc = GetDC(NULL);
            if (hdc) 
            {
                int pixWidth = MulDiv(hmWidth, GetDeviceCaps(hdc, LOGPIXELSX), 2540);
                int pixHeight = MulDiv(hmHeight, GetDeviceCaps(hdc, LOGPIXELSY), 2540);

                // Configure for 24-bit Top-Down DIB (BGR order)
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = pixWidth;
                bmi.bmiHeader.biHeight = -pixHeight; 
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 24;
                bmi.bmiHeader.biCompression = BI_RGB;

                // 6. Verify output buffer size and extract raw pixels
                if (OutBufSize >= (DWORD)(pixWidth * pixHeight * 3))
                {
                    if (GetDIBits(hdc, hSrcBmp, 0, pixHeight, OutBuf, &bmi, DIB_RGB_COLORS))
                    {
                        success = 1; 
                    }
                }
                // Fix: Always release the DC if it was acquired
                ReleaseDC(NULL, hdc);
            }
            pPicture->lpVtbl->Release(pPicture);
        }
        // This call handles the cleanup of hGlobal automatically due to the TRUE flag used above
        pStream->lpVtbl->Release(pStream);
    }
    else 
    {
        // Fail-safe: If the stream was never created, we must free hGlobal manually
        GlobalFree(hGlobal);
    }

    // Returns 0 for success per your implementation
    return (success ? 0 : 1);
}

#endif





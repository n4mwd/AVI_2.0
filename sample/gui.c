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


#if defined(__WIN32__)
#include <windows.h>

static HBITMAP hDIB = NULL;
static HDC hMemDC = NULL;
static HWND G_hwnd = NULL;
static int vWidth, vHeight;


LRESULT CALLBACK
WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

    if (uMsg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (uMsg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        /* Fast bit-block transfer from memory to screen */
        BitBlt(hdc, 0, 0, vWidth, vHeight, hMemDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void *InitWindows(int Width, int Height)
{
    BITMAPINFO bmi;
    WNDCLASS wc = {0};
    HWND hwnd;
    HINSTANCE hInstance = GetModuleHandle(NULL);
    HDC hdcScreen;
    int screenW, screenH;
    void *pPixels;

    vWidth = Width;
    vHeight = Height;

    /* 1. Register Window */
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "VideoTestClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&wc);

    /* 2. Setup Bitmap Info for 32-bit RGB (8:8:8) */
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = vWidth;
    bmi.bmiHeader.biHeight = -vHeight; /* Negative for Top-Down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    /* 3. Create the DIB Section and Memory DC */
    hdcScreen = GetDC(NULL);
    hDIB = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pPixels, NULL, 0);
    hMemDC = CreateCompatibleDC(hdcScreen);
    SelectObject(hMemDC, hDIB);
    ReleaseDC(NULL, hdcScreen);

    /* 4. Center and Create Window */
    screenW = GetSystemMetrics(SM_CXSCREEN);
    screenH = GetSystemMetrics(SM_CYSCREEN);
    hwnd =
        CreateWindowEx(0, "VideoTestClass", "Video Library Test",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            (screenW - vWidth) / 2, (screenH - vHeight) / 2,
            vWidth + 16, vHeight + 39, NULL, NULL, hInstance, NULL);

    G_hwnd = hwnd;
    return(pPixels);
}


int ProcessGuiMessages(void)
{
    MSG msg = {0};
//    HWND hwnd;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT) return(WM_QUIT);
        if (msg.message == WM_LBUTTONDOWN) return(WM_LBUTTONDOWN);

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return(0);   // got out of loop without mouse
}


void GuiShowFrame(void)
{
    InvalidateRect(G_hwnd, NULL, FALSE);
    UpdateWindow(G_hwnd);
}

void CloseGui(void)
{
    if (hMemDC) DeleteDC(hMemDC);
    if (hDIB) DeleteObject(hDIB);
}

DWORD ticks(void)
{
    return(GetTickCount());
}

void GuiSleep(DWORD ms)
{
    Sleep(ms);
}


#else    // linux implementation using X11

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

typedef unsigned int DWORD;

// Fake windows defines
#define WM_QUIT         0x0012
#define WM_LBUTTONDOWN  0x0201

static Display *display = NULL;
static Window window;
static XImage *ximage = NULL;
static GC gc;
static int vWidth, vHeight;
static Atom wmDeleteMessage;

void GuiShowFrame(void);

void *InitWindows(int Width, int Height)
{
    vWidth = Width;
    vHeight = Height;
    int screen, depth, bitmap_pad;
    Visual *visual;
    unsigned char *pPixels;
    XEvent event;

    display = XOpenDisplay(NULL);
    if (!display)
    {
//AVI_DBG("Unable to open X display\n");
        return NULL;
    }

    screen = DefaultScreen(display);
    visual = DefaultVisual(display, screen);
    depth  = DefaultDepth(display, screen);

    // Ensure 32-bit alignment for modern X11 servers
    bitmap_pad = (depth >= 24) ? 32 : 8;
    int bytes_per_pixel = (bitmap_pad == 32) ? 4 : 3;

    pPixels = (unsigned char *)malloc(Width * Height * bytes_per_pixel);
    if (!pPixels)
    {
//AVI_DBG("Failed to allocate pixel buffer\n");
        return NULL;
    }
    memset(pPixels, 0, Width * Height * bytes_per_pixel);

    ximage = XCreateImage(display, visual, depth, ZPixmap, 0,
                          (char *)pPixels, Width, Height, bitmap_pad, 0);

    if (!ximage)
    {
//AVI_DBG("Failed to create XImage\n");
        free(pPixels);
        return NULL;
    }

    window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                 10, 10, Width, Height, 1,
                                 BlackPixel(display, screen),
                                 BlackPixel(display, screen));

    gc = XCreateGC(display, window, 0, NULL);

    wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);
    XStoreName(display, window, "Video Library Test");

    // Add StructureNotifyMask to detect when the window is actually mapped
    XSelectInput(display, window, ExposureMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(display, window);

    /* * SYNC POINT: This mirrors the synchronous nature of Win32 Init.
     * We wait until the X Server actually displays the window.
     */
    while (1)
    {
        XNextEvent(display, &event);
        if (event.type == MapNotify) break;
    }

    return (void *)pPixels;
}

int ProcessGuiMessages(void)
{
    XEvent event;
    // XPending is the equivalent of PeekMessage
    while (XPending(display))
    {
        XNextEvent(display, &event);

        if (event.type == ClientMessage)
        {
            if (event.xclient.data.l[0] == wmDeleteMessage)
                return WM_QUIT;
        }

        if (event.type == ButtonPress)
        {
            if (event.xbutton.button == Button1)
                return WM_LBUTTONDOWN;
        }

        if (event.type == Expose)
        {
            // Redraw when requested by the OS
            if (event.xexpose.count == 0) GuiShowFrame();
        }
    }
    return 0;
}

void GuiShowFrame(void)
{
    if (display && ximage && gc)
    {
        // XPutImage is our BitBlt equivalent
        XPutImage(display, window, gc, ximage, 0, 0, 0, 0, vWidth, vHeight);
        // Ensure the command is sent to the server immediately
        XFlush(display);
    }
}

void CloseGui(void)
{
    if (display)
    {
        if (gc) XFreeGC(display, gc);
        // Note: XDestroyImage automatically frees the pPixels buffer
        if (ximage) XDestroyImage(ximage);
        XCloseDisplay(display);
    }
}

DWORD ticks(void)
{
    static time_t start_sec = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);

    // Initialize start_sec once so we return small, manageable numbers
    if (start_sec == 0) start_sec = ts.tv_sec;

    DWORD ms = (DWORD)((ts.tv_sec - start_sec) * 1000) +
                       (DWORD)(ts.tv_nsec / 1000000);

    return ms;
}

void GuiSleep(unsigned int ms)
{
    // usleep takes microseconds
    usleep(ms * 1000);
}
#endif




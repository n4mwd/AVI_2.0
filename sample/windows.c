
#include "avi2.h"
#include "stuff.h"


HINSTANCE   G_hInst = NULL;
BOOL        G_Playing = FALSE, G_Loaded = FALSE, G_Saved = FALSE, G_Preview = FALSE;
HWND        G_hwnd = NULL, G_hProgress = NULL, G_VidWnd = NULL, G_hTT = NULL;
HWND        G_hTrack = NULL;
RECT        VidRect;
char        inFile[MAX_PATH] = {0};
char        outFile[MAX_PATH] = {0};
extern      SURFACE SfcIn;
extern      int G_FrameBoost;
static      int S_fnum;


#define DEFBUTSIZE 32



LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
int OpenVideoFile(HWND hWnd);
int SaveVideoFile(HWND hWnd);
BOOL RegisterWindows(void);
BOOL InitializeApplication(void);
void AddPlaybar(HWND Parent, RECT VidWin, int ButSize);
void SetVideoPos(int *fnum, int msg);







#pragma argsused

WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;

//    getAvailableMemory();
    G_hInst = hInst;
	if (hPrev) return 1;


    LoadLibrary(TEXT("RICHED32.DLL"));

	//	Register main window class
	if (!RegisterWindows())
        return 1;

	//	Initialize main window, and application
	if (!InitializeApplication())
		return 1;


    if (InitBuffers())
    {
        return(1);
    }

	ShowWindow(G_hwnd, nCmdShow);
    UpdateWindow(G_hwnd);

	/*
	**	Main Message Loop
	*/
 	while (GetMessage(&msg, NULL, 0, 0))
	{
        if (!G_hProgress || !IsDialogMessage(G_hProgress, &msg))
        {
    	    TranslateMessage(&msg);
		    DispatchMessage(&msg);
        }
	}

    FreeBuffers();
    return msg.wParam;
}


BOOL RegisterWindows(void)
{
	WNDCLASSEX	wclass;
    int rt;

    wclass.cbSize = sizeof(wclass);
	wclass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	wclass.lpfnWndProc = (WNDPROC)MainWndProc;
	wclass.cbClsExtra = 0;
    wclass.cbWndExtra = 0;
	wclass.hInstance = G_hInst;
	wclass.hIcon = NULL;
	wclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wclass.lpszMenuName = "SprocketMenu";
	wclass.lpszClassName = (LPSTR) "Sprocket";
    wclass.hIconSm = NULL;

    rt = RegisterClassEx(&wclass);
	if (!rt)
	{
		return(FALSE);
	}


    wclass.lpfnWndProc = (WNDPROC)VideoWndProc;
    wclass.hIcon = NULL;
    wclass.hbrBackground = (HBRUSH) GetStockObject(GRAY_BRUSH);
    wclass.lpszMenuName = NULL;
    wclass.lpszClassName = (LPSTR) "VidWindow";

    rt = RegisterClassEx(&wclass);
	if (!rt)
	{
		return(FALSE);
	}


	return(TRUE);
}


/*
** BOOL
** InitializeApplication()
**
**    Initialize this application.  This involves loading the menu for the
**	main window, creating the main window, and loading the accelerator table
**	from the resource file.
*/

BOOL InitializeApplication(void)
{
    RECT rc;

    InitCommonControls();
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0 );
//    GetWindowRect(GetDesktopWindow(), &rc);

	//	Create the main window
	G_hwnd = CreateWindow("Sprocket", "8mm Film Sprocket Stabilizer",
		WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_SYSMENU,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
//        CW_USEDEFAULT, CW_USEDEFAULT, 800, 700,  //rc.right, rc.bottom,
//        (rc.right - 800) / 2, (rc.bottom - 700) / 2, 800, 700,  //rc.right, rc.bottom,
        NULL, NULL, G_hInst, NULL);

	if (G_hwnd == NULL)
	{
		return(FALSE);
	}

	return(TRUE);
}


// Calculate window size
// RC.bottom and RC.right are the client height and width of window
void CalcWinSize(HWND hwnd, RECT *rc, int aspect)
{
    int  px, py;    // vid window width and height
    int  wx, wy;

    // Calculate optimal VidRect
    GetClientRect(hwnd, rc);
    wx = rc->right;       // main window width
    wy = rc->bottom;      // main window height
    py = wy - (DEFBUTSIZE * 7) / 2;   // reserve room for buttons and controls
    if (aspect == 43)     // 4:3 aspect ratio
    {
        px = py * 4 / 3;
        if (px >= wx - DEFBUTSIZE)
        {
            px = wx - DEFBUTSIZE;
            py = px * 3 / 4;
        }
    }
    else   // 16:9
    {
        px = py * 16 / 9;
        if (px >= wx - DEFBUTSIZE)
        {
            px = wx - DEFBUTSIZE;
            py = px * 9 / 16;
        }
    }

    // Calculate vidrect
    VidRect.top = DEFBUTSIZE;
    VidRect.bottom = VidRect.top + py;
    VidRect.left = (wx - px) / 2;
    VidRect.right = VidRect.left + px;

    // calculate size of main window
    rc->left = rc->top = 0;
    rc->right = VidRect.right + DEFBUTSIZE;
    rc->bottom = VidRect.bottom  + (DEFBUTSIZE * 5) / 2;
    AdjustWindowRect(rc, (DWORD) GetWindowLong(hwnd, GWL_STYLE), TRUE);
    rc->bottom -= rc->top - 1;
    rc->right -= rc->left - 1;
}


LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC			hdc;
    HBRUSH      brush;
	PAINTSTRUCT	ps;
    RECT        rc = {0};
    char        WaitStr[128];
    int         px, py;
//    int         cyVScroll;
//    static HWND hwndPB = NULL;

 //   static int Activated = FALSE;

	switch (message)
	{
        case WM_TIMER:
            if (wParam == IDT_TIMER1)
            {
                SetVideoPos(&S_fnum, IDT_TIMER1);
            }
            break;

        case WM_CREATE:
        {
            int xP, yP;

            // Move window to center of screen
            GetWindowRect (hwnd, &rc) ;

            xP = (GetSystemMetrics(SM_CXSCREEN) - rc.right)/2;
            yP = (GetSystemMetrics(SM_CYSCREEN) - rc.bottom)/2;

            SetWindowPos(hwnd, 0, xP, yP, 0, 0, SWP_NOZORDER | SWP_NOSIZE );
            CalcWinSize(hwnd, &rc, 43);   // creat initially with a 4:3 aspect ratio
            SetWindowPos(hwnd, 0, 0, 0, rc.right, rc.bottom, SWP_NOZORDER | SWP_NOMOVE);
            GetWindowRect(hwnd, &rc);
            px = VidRect.right - VidRect.left + 1;  // width
            py = VidRect.bottom - VidRect.top + 1;  // height

            G_VidWnd = CreateWindow("VidWindow", NULL,
                            WS_CHILDWINDOW | WS_VISIBLE,
                            VidRect.left, VidRect.top, px, py,
                            hwnd, NULL, G_hInst, NULL);

	        if (G_VidWnd == NULL)
	        {
		        return(-1);
	        }
            AddPlaybar(hwnd, VidRect, DEFBUTSIZE);
            SetButtonState();
//            LoadScreen(G_VidWnd, "resource/SprocketTitle.bmp");
//            bmp = LoadImage(G_hInst, MAKEINTRESOURCE(IDB_TITLE), IMAGE_BITMAP, 0, 0, 0/*LR_CREATEDIBSECTION*/);
//            bmp = LoadImage(0, "resource/SprocketTitle.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
//    bmp = LoadBitmap(G_hInst, MAKEINTRESOURCE(IDB_TITLE));

//ERROR_RESOURCE_TYPE_NOT_FOUND
//    1813 (0x715)
//    The specified resource type cannot be found in the image file.


//            SendMessage(, BM_SETIMAGE, (WPARAM) IMAGE_BITMAP, (LPARAM) bmp);
//            DeleteObject(bmp);
            break;
        }

        case WM_GETMINMAXINFO:
        {
            LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
            lpMMI->ptMinTrackSize.x = 600;
            lpMMI->ptMinTrackSize.y = 500;
            break;
        }


//      case WM_NCCALCSIZE:
        case WM_SIZE:
        {
            int aspect;

 //           rc.right = LOWORD(lParam);  // width of client area
 //           rc.bottom = HIWORD(lParam); // height of client area

            aspect = 43;
            if (G_Loaded) aspect = SfcIn.aspect;
            CalcWinSize(hwnd, &rc, aspect);   // creat initially with a 4:3 aspect ratio
//            SetWindowPos(hwnd, 0, 0, 0, rc->right, rc->bottom, SWP_NOZORDER | SWP_NOMOVE);
            px = VidRect.right - VidRect.left + 1;  // width
            py = VidRect.bottom - VidRect.top + 1;  // height
            UpdateWindow(hwnd);

            ResizePlayBar(VidRect, DEFBUTSIZE);

            MoveWindow(G_VidWnd, VidRect.left, VidRect.top, px, py, TRUE);

            break;
        }

		case WM_PAINT:
		{
            GetClientRect(hwnd, &rc);
			hdc = BeginPaint(hwnd, &ps);

            brush = CreateSolidBrush(RGB(230, 230, 230));
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            // Main Frame Text
            if (G_Loaded)
            {
                strcpy(WaitStr, inFile);
            }
            else   // not loaded
            {
          //    LoadScreen(G_VidWnd, hdc, "resource/SprocketTitle.bmp");
//              brush = CreateSolidBrush(RGB(50, 151, 151));
 //               FillRect(hdc, &VidRect, brush);
//                DeleteObject(brush);

                strcpy(WaitStr, "No Video Loaded.");
            }
            rc = VidRect;
            rc.top = rc.top - 20;
            rc.bottom = rc.top + 20;
            SetBkMode(hdc, TRANSPARENT);
            DrawText(hdc, WaitStr, -1, &rc, DT_CENTER | DT_NOCLIP);

			EndPaint(hwnd, &ps);
			break;
		}


        case WM_COMMAND:
        // Button notifications: the low-order word of the wParam parameter
        // contains the control identifier, the high-order word of wParam
        // contains the notification code, and the lParam parameter contains
        // the control window handle.

            switch(LOWORD(wParam))
            {
                case IDM_OPEN:
                    if (OpenVideoFile(hwnd)) break;  // load cancelled
//                    if (SfcIn.aspect == 43)
//                    else
                    InvalidateRect(hwnd, NULL, TRUE);
                    S_fnum = 0;
                    G_Preview = FALSE;
                    GetFrame(0);
                    SetButtonState();
                    SendMessage(hwnd, WM_SIZE, 0, 0);
                    G_Saved = FALSE;
                    InvalidateRect(G_VidWnd, NULL, TRUE);
                    break;

                case IDM_CLOSE:
                    if (!G_Loaded) break;      // nothing to save
                    if (!G_Saved)
                    {
                        int rt = MessageBox(hwnd,
                            "Video not saved.\nOK to discard changes?",
                            "Warning",
                            MB_OKCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON2 | MB_TOPMOST);
                        if (rt == IDCANCEL) break;   // abort close
                    }

                    G_Preview = FALSE;
                    if (G_Playing) SetVideoPos(&S_fnum, IDB_STOPBUT);
                    AbortInput();
                    G_Loaded = FALSE;
                    SetButtonState();
                    InvalidateRect(hwnd, NULL, TRUE);
                    CalcWinSize(hwnd, &rc, 43);   // creat initially with a 4:3 aspect ratio
                    SetWindowPos(hwnd, 0, 0, 0, rc.right, rc.bottom, SWP_NOZORDER | SWP_NOMOVE);
                    break;


                case IDM_INSTRUCTIONS:
                    ShowHelp(IDT_INSTRUCTIONS);
                    break;

                case IDM_LICENSE:
                    ShowHelp(IDT_LICENSETEXT);
                    break;

                case IDM_ABOUT:
                  //  MessageBox(hwnd,
                  //      "Sprocket hole 8mm film stabilizer.\n"
                  //      "Writtten by Dennis Hawkins\n"
                  //      "             Version 1.0", "About", MB_OK);
                  ShowHelp(IDT_ABOUTTEXT);
                    break;

                case IDM_SAVE:
                    if (SaveVideoFile(hwnd))
                        break;   // video not saved
                    G_Saved = TRUE;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;

                case IDM_EXIT:
                    SendMessage(hwnd, WM_CLOSE, 0, 0);
                    break;

                case IDB_PAUSEBUT:
                    G_Playing = FALSE;
                    SetButtonState();
                    KillTimer(hwnd, IDT_TIMER1);
                    break;

                case IDB_PLAYBUT:
                    G_Playing = TRUE;
                    SetButtonState();
                    SetTimer(hwnd, IDT_TIMER1, 63, NULL);  // Set Timer 16 FPS
                    break;

                case IDB_PREVIEWBUT:
                    G_Preview = !G_Preview;    // toggle preview
                case IDB_STOPBUT:
                case IDB_PREVBUT:
                case IDB_BACKBUT:
                case IDB_FORWARDBUT:
                case IDB_NEXTBUT:
                    SetVideoPos(&S_fnum, LOWORD(wParam));
                    break;

                case IDB_RESETBUT:
                {
                    FRAMELINES *frm;
                    int rt = MessageBox(G_hwnd,
                                 "This will reset all frames to computed "
                                 "defaults.", "Question", MB_OKCANCEL);
                    if (rt == IDOK)
                    {
                        G_FrameBoost = 0;
                        for (rt = 0; rt < SfcIn.TotalFrames; rt++)
                        {
                            frm = &FrameArray[rt];
                            frm->AdjLeftFrame = 0;
                            frm->AdjVPos = 0;
                        }
                        SetVideoPos(&S_fnum, IDB_STOPBUT);
                    }
                    break;
                }
            }
            break;

        case WM_HSCROLL:
        {
           // WORD loWord = LOWORD(wParam);

            // The low-order word of the wParam parameter contains the notification code, and
            // the high-order word specifies the position of the slider.
            // The lParam parameter is the handle of the trackbar.
            if (G_Loaded )
            {
                S_fnum = SendMessage((HWND) lParam, TBM_GETPOS, 0, 0); // get frame position as percentage
                SetVideoPos(&S_fnum, TBM_SETPOS);
            }
            break;
        }

		case WM_DESTROY:
            AbortInput();
			PostQuitMessage(0);
			break;

		default:
			return DefWindowProc(hwnd, message, wParam, lParam);
	}

	return(FALSE);
}



// Get the name and path of the mjpg file
// Retrun TRUE on fail.

int OpenVideoFile(HWND hWnd)
{
    OPENFILENAME ofn;
//    char tmpstr[MAX_PATH + MAX_FRAMENAME_LEN];
    BOOL ret;


	// Fill in the OPENFILENAME structure.
    memset(&ofn, 0, sizeof(OPENFILENAME));
	ofn.lStructSize       = sizeof(OPENFILENAME);
    ofn.hwndOwner         = hWnd;
    ofn.hInstance         = G_hInst;
    ofn.lpstrFilter       = "MJPEG Files (*.avi)\0*.avi\0"
                            "All Files (*.*)\0*.*\0";
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxCustFilter    = 0;
    ofn.nFilterIndex      = 1;
    ofn.lpstrFile         = inFile;
    ofn.nMaxFile          = sizeof(inFile);
    ofn.lpstrFileTitle    = NULL;
    ofn.nMaxFileTitle     = 0;
    ofn.lpstrInitialDir   = NULL; // (char *) DefaultPath;
    ofn.lpstrTitle        = "Open Overscanned Video File";
    ofn.nFileOffset       = 0;
    ofn.nFileExtension    = 0;
    ofn.lpstrDefExt       = "avi";
    ofn.Flags             = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;

    ret = GetOpenFileName(&ofn);    // FileDialog

    if (!ret) return(TRUE);    // return if cancel is clicked
    // OK clicked - Input file path is in SrcDir[]


    G_Loaded = FALSE;
    if (LoadVideo1(inFile))
    {
        PrintError("Could Not Load Video.\n");
        return(TRUE);
    }
    else
    {
        G_Loaded = TRUE;
        SfcIn.Splash = FALSE;
        SendMessage(G_hTrack, TBM_SETRANGE, (WPARAM) TRUE,
                    (LPARAM) MAKELONG(0, SfcIn.TotalFrames - 1));  // min. & max. positions
    }

    return(FALSE);  // success
}


// Get the name and path of the output mjpg file
// Return TRUE if file was not saved.

int SaveVideoFile(HWND hWnd)
{
    OPENFILENAME sfn;
//    char tmpstr[MAX_PATH + MAX_FRAMENAME_LEN];
    BOOL ret;

    if (!G_Loaded)
    {
        PrintError("File is not loaded.");
        return(TRUE);
    }


    if (outFile[0] == '\0')
    {
        char *ptr;

        strcpy(outFile, inFile);
        ptr = strrchr(outFile, '.');
        if (ptr) *ptr = '\0';
        strcat(outFile, "Out.avi");
    }


	// Fill in the OPENFILENAME structure.
    memset(&sfn, 0, sizeof(OPENFILENAME));
	sfn.lStructSize       = sizeof(OPENFILENAME);
    sfn.hwndOwner         = hWnd;
    sfn.hInstance         = G_hInst;
    sfn.lpstrFilter       = "MJPEG Files (*.avi)\0*.avi\0"
                            "All Files (*.*)\0*.*\0";
    sfn.lpstrCustomFilter = NULL;
    sfn.nMaxCustFilter    = 0;
    sfn.nFilterIndex      = 1;
    sfn.lpstrFile         = outFile;
    sfn.nMaxFile          = sizeof(outFile);
    sfn.lpstrFileTitle    = NULL;
    sfn.nMaxFileTitle     = 0;
    sfn.lpstrInitialDir   = NULL; // (char *) DefaultPath;
    sfn.lpstrTitle        = "Save Video File";
    sfn.nFileOffset       = 0;
    sfn.nFileExtension    = 0;
    sfn.lpstrDefExt       = "avi";
    sfn.Flags             = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    ret = GetSaveFileName(&sfn);    // FileDialog

    if (!ret) return(TRUE);    // return if cancel is clicked
    // OK clicked - Input file path is in SrcDir[]


    if (SaveVideo(outFile))
    {
        PrintError("Could Not Save Video.\n");
        return(TRUE);
    }
    else
    {
        MessageBox(hWnd, "Video Successfully Saved.", "Complete", MB_OK);
        return(FALSE);
    }

}


// Scale the screen distance to the image

void ScaleScr2Img(int fnum, POINT *dist)
{
    int    sW, sH, w, h;

    if (FrameArray && dist && fnum >= 0)
    {
        // scale point
        sW = VidRect.right - VidRect.left + 1;
        sH = VidRect.bottom - VidRect.top + 1;
        w = SfcIn.width;
        h = SfcIn.height;

        dist->x = dist->x  * w / sW;
        dist->y = dist->y * h / sH;
    }
}


// Get Adjusted Rectangle
// Apply adjustments and return calculated new rectangle
void GetAdjRect(int fnum, MYRECT *pRect)
{
    FRAMELINES *frm;
    int oC, nH;
    MYRECT rect;

    // Add frameboost to image rectangle, rect.left stays the same
    frm = &FrameArray[fnum];
    rect = frm->FrameRect;

    rect.right += G_FrameBoost;
    nH = (rect.right - rect.left + 1) * 3 / 4;  // new height
    oC = (rect.top + rect.bottom) / 2;          // center
    rect.top = oC - (nH / 2);
    rect.bottom = rect.top + nH;

    // Set main rect (left, top, right, bottom)
    rect.left += frm->AdjLeftFrame;
    rect.top += frm->AdjVPos;
    rect.right += frm->AdjLeftFrame;
    rect.bottom += frm->AdjVPos;

    *pRect = rect;
}



// Scale the rectangle of the image to the screen coordinates
// Add in G_FrameBoost, frame->AdjLeftFrame, frame->AdjVPos
// Return screen rectangle in sRect.

void ScaleImg2Scr(int fnum, MYRECT *sRect)
{
    int    sW, sH, w, h; //, nH, oC;
    MYRECT   rect;
//    FRAMELINES *frm;

    memset(&rect, 0, sizeof(RECT));
    if (FrameArray && sRect && fnum >= 0)
    {
        GetAdjRect(fnum, &rect);

        // scale rect
        sW = VidRect.right - VidRect.left + 1;
        sH = VidRect.bottom - VidRect.top + 1;
        w = SfcIn.width;
        h = SfcIn.height;

        // Set main rect (left, top, right, bottom)
        SetRect((RECT *) sRect,
                rect.left   * sW / w,
                rect.top    * sH / h,
                rect.right  * sW / w,
                rect.bottom * sH / h);
    }
}


void LoadScreen(HWND hWnd, HDC hdc, DWORD BmpRes)
{
    HDC hdcMem;
    RECT rect;
    BITMAP bm;
    HBITMAP bmp = LoadImage(G_hInst, MAKEINTRESOURCE(BmpRes), IMAGE_BITMAP, 0, 0, 0);

    GetClientRect(hWnd,&rect);
    GetObject(bmp, sizeof(BITMAP), &bm);
    hdcMem = CreateCompatibleDC(hdc);
    SelectObject(hdcMem, bmp);

    SetStretchBltMode(hdc, STRETCH_HALFTONE);
    StretchBlt(hdc,0,0,rect.right,rect.bottom,hdcMem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);

    DeleteObject(bmp);
    DeleteDC(hdcMem);

}



LRESULT CALLBACK
VideoWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static BOOL  fBlocking; // , fValidBox;
    static POINT ptBeg, ptEnd, pDist;   //   , ptBoxBeg, ptBoxEnd
    static RECT  RectTbl[5] = {{0}};
    static int   RectBut = -1;
    static int   OrigALF, OrigAVP, OrigBoost;
   	HDC			hdc;
	PAINTSTRUCT	ps;
    FRAMELINES *frm = NULL;

    if (FrameArray) frm = &FrameArray[S_fnum];

	switch (message)
	{

        case WM_LBUTTONDOWN:
            if (!G_Loaded || G_Preview) break;
            ptBeg.x = ptEnd.x = LOWORD(lParam);
            ptBeg.y = ptEnd.y = HIWORD(lParam);

            fBlocking = TRUE;
            RectBut = -1;
            if (PtInRect(&RectTbl[IDX_LEFTFRAME], ptBeg)) RectBut = IDX_LEFTFRAME;
            else if (PtInRect(&RectTbl[IDX_TOPFRAME], ptBeg)) RectBut = IDX_TOPFRAME;
            else if (PtInRect(&RectTbl[IDX_BOTFRAME], ptBeg)) RectBut = IDX_BOTFRAME;
            else if (PtInRect(&RectTbl[IDX_RIGHTFRAME], ptBeg)) RectBut = IDX_RIGHTFRAME;
            else fBlocking = FALSE;

            if (fBlocking)   // mouse button down in a button rectangle
            {
                OrigALF = frm->AdjLeftFrame;
                OrigAVP = frm->AdjVPos;
                OrigBoost = G_FrameBoost;
                SetCapture(hwnd);
                SetCursor(LoadCursor (NULL, IDC_CROSS));
            }
            break;

        case WM_MOUSEMOVE:
            if (fBlocking)   // Dragging from button rectangle
            {
                SetCursor(LoadCursor (NULL, IDC_CROSS));

                ptEnd.x = LOWORD(lParam);
                ptEnd.y = HIWORD(lParam);

                // Calc screen distance from begin point
                pDist.x = ptEnd.x - ptBeg.x;
                pDist.y = ptEnd.y - ptBeg.y;
                ScaleScr2Img(S_fnum, &pDist);  // get image scaled distance

                // determine which frame button it appplies to and set variable
                if (RectBut == IDX_LEFTFRAME)
                {
                    frm->AdjLeftFrame = OrigALF + pDist.x;   // this frame only
                }
                else if (RectBut == IDX_TOPFRAME || RectBut == IDX_BOTFRAME)
                {
                    frm->AdjVPos = OrigAVP + pDist.y;   // applies to all future frames as well
                }
                else if (RectBut == IDX_RIGHTFRAME)
                {
                    G_FrameBoost = OrigBoost + pDist.x;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;

        case WM_LBUTTONUP:
            if (fBlocking)
            {
                int j = frm->AdjVPos;

                if (j != OrigAVP)   // must change remaining frames
                {
                    int i;

                    for (i = S_fnum + 1; i < SfcIn.TotalFrames; i++)
                    {
                        FrameArray[i].AdjVPos = j; //pDist.y;
                    }
                }

                pDist.x = pDist.y = 0;
                OrigBoost = OrigALF = OrigAVP = 0;

                ReleaseCapture();
                SetCursor(LoadCursor (NULL, IDC_ARROW));

                fBlocking = FALSE;
                RectBut = -1;

                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;

        case WM_CHAR:
            if (fBlocking && wParam == '\x1B')       // i.e., Escape
            {
                ReleaseCapture();
                SetCursor(LoadCursor (NULL, IDC_ARROW));
                fBlocking = FALSE;
                RectBut = -1;
                frm->AdjLeftFrame = OrigALF;
                frm->AdjVPos = OrigAVP;
                G_FrameBoost = OrigBoost;
            }
            break;

//        case WM_SIZE:
//        InvalidateRect(hwnd, NULL, FALSE);
//            break;

//        case WM_ERASEBKGND:
//            return(TRUE);

		case WM_PAINT:
        {
            HPEN   pen;

            hdc = BeginPaint(hwnd, &ps);
            if (!FrameArray)   // Not loaded
            {
//                LoadScreen(G_VidWnd, hdc, "resource/SprocketTitle.bmp");
//                LoadScreen(G_VidWnd, hdc, IDB_TITLE);
                  ShowSplash(hwnd, hdc, IDG_TITLE);
            }
            else
            {
                int inWidth = SfcIn.width;

                if (G_Preview)
                {
                    MYRECT rect;

                    GetAdjRect(S_fnum, &rect);

                    ScaleImage(&SfcIn, rect);
//abs(SfcIn.width + SfcIn.aspect);
//SfcIn.aspect = 43;
                    AddFrameNumber(&SfcIn, NUMXPOS, NUMYPOS, S_fnum);
                }

                ShowBitmap(hdc, &VidRect);
                SfcIn.width = inWidth;

                if (!G_Preview)
                {
                    GetFrameRects(S_fnum, RectTbl);

                    SetROP2(hdc, R2_COPYPEN);
                    DrawRectTabs(hdc, S_fnum, RectTbl);
                    pen = CreatePen(PS_SOLID, 2, RGB(0,255,0));
                    SelectObject(hdc, pen);
                    SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, RectTbl[IDX_MAINFRAME].left, RectTbl[IDX_MAINFRAME].top, RectTbl[IDX_MAINFRAME].right, RectTbl[IDX_MAINFRAME].bottom);
                    DeleteObject(pen);
                }
            }
			EndPaint(hwnd, &ps);
			break;
        }

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		default:
 //       def:
			return DefWindowProc(hwnd, message, wParam, lParam);
	}

	return FALSE;
}



// Rturn the number of processors in the system

int GetNumProcessors(void)
{
    int numCPU;
    SYSTEM_INFO sysinfo;

    GetSystemInfo(&sysinfo);
    numCPU = sysinfo.dwNumberOfProcessors;
//    printf("There are %d processors in this computer.\n", numCPU);

    return(numCPU);
}



// Get available memory in MB

DWORD getAvailableMemory(void)
{
    MEMORYSTATUS status;

    status.dwLength = sizeof(status);

    GlobalMemoryStatus(&status);

    return ((DWORD) (status.dwAvailPhys / 1048576));
}






////////////////////////////////////////////////////////////




// Return the file size in bytes, -1 on error

DWORD CalcFileSize(FILE *fp)
{
    struct stat statbuf;

    if (fp == NULL || fstat(fileno(fp), &statbuf) == -1)
        return(-1);

    return(statbuf.st_size);
}








// Displays error in a dialog box (Windows) or prints a text
// string to STDOUT if not.

void PrintError(char *eTxt)
{
#if defined(BUILDWIN)
    MessageBox(NULL, eTxt, "Problem", MB_APPLMODAL | MB_OK | MB_ICONSTOP);
#else
    printf(eTxt);
#endif
}

void PrintErrorNum(char *eTxt, int pNum)
{
    char tmpstr[256];

    sprintf(tmpstr, eTxt, pNum);
    PrintError(tmpstr);
}


void PrintAviError(void)
{
    char *estr;

    estr = AVI_strerror();
    PrintError(estr);
}



// Get the calculated frame rectangle and scale it to the display size
// Calculate the main rectangle and the 4 mouse target rectangles.
// [0] = main rect, [1]=left rect, [2]=top rect, [3]=bot rect, [4]=right rect
#define HALFBUT (DEFBUTSIZE / 2)
#define BUT4X   (DEFBUTSIZE * 4)

void GetFrameRects(int fnum, RECT RTbl[5])
{
    int xH, yV;
    MYRECT *rect;

    // clear rect table
    memset(RTbl, 0, sizeof(RTbl));
    if (FrameArray && RTbl && fnum >= 0)
    {
        // scale rect including adjustments
        ScaleImg2Scr(fnum, (MYRECT *) &RTbl[IDX_MAINFRAME]);

        rect = (MYRECT *) &RTbl[IDX_MAINFRAME];
        xH = (rect->right + rect->left) / 2 - (DEFBUTSIZE * 2);  // Left position for horizontal tabs
        yV = (rect->bottom + rect->top) / 2 - (DEFBUTSIZE * 2);  // Top position for vertical tabs

        SetRect(&RTbl[IDX_LEFTFRAME], rect->left, yV, rect->left + HALFBUT, yV + BUT4X);
        SetRect(&RTbl[IDX_RIGHTFRAME], rect->right - HALFBUT, yV, rect->right, yV + BUT4X);
        SetRect(&RTbl[IDX_TOPFRAME], xH, rect->top, xH + BUT4X, rect->top + HALFBUT);
        SetRect(&RTbl[IDX_BOTFRAME], xH, rect->bottom - HALFBUT, xH + BUT4X, rect->bottom);

    }
}





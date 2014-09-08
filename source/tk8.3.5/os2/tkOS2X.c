/* 
 * tkOS2X.c --
 *
 *	This file contains OS/2 PM emulation procedures for X routines. 
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 * Copyright (c) 1994 Software Research Associates, Inc.
 * Copyright (c) 1998-2000 by Scriptics Corporation.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkOS2Int.h"

/*
 * The zmouse.h file includes the definition for WM_MOUSEWHEEL.

#include <zmouse.h>
 */
#define WM_MOUSEWHEEL 0x0470 /* ten below WM_PENFIRST */

/*
 * Declarations of static variables used in this file.
 */

static char os2ScreenName[] = "PM:0";	/* Default name of OS2 display. */
static int childClassInitialized = 0;	/* Registered child class? */

/*
 * Thread local storage.  Notice that now each thread must have its
 * own TkDisplay structure, since this structure contains most of
 * the thread-specific date for threads.
 */
typedef struct ThreadSpecificData {
    TkDisplay *os2Display;       /* TkDisplay structure that *
                                  *  represents Windows screen. */
    int updatingClipboard;       /* If 1, we are updating the clipboard */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Forward declarations of procedures used in this file.
 */

static void             GenerateXEvent _ANSI_ARGS_((HWND hwnd, ULONG message,
                            MPARAM param1, MPARAM param2));
static unsigned int     GetState _ANSI_ARGS_((ULONG message, MPARAM param1,
                            MPARAM param2));

/*
 *----------------------------------------------------------------------
 *
 * TkGetServerInfo --
 *
 *	Given a window, this procedure returns information about
 *	the window server for that window.  This procedure provides
 *	the guts of the "winfo server" command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkGetServerInfo(interp, tkwin)
    Tcl_Interp *interp;		/* The server information is returned in
				 * this interpreter's result. */
    Tk_Window tkwin;		/* Token for window;  this selects a
				 * particular display and server. */
{
    char buffer[50];
    ULONG info[QSV_MAX]= {0};	/* System Information Data Buffer */
    APIRET rc;

    /* Request all available system information */
    rc= DosQuerySysInfo (1L, QSV_MAX, (PVOID)info, sizeof(info));
    /*
     * Hack for LX-versions above 2.11
     *  OS/2 version    MAJOR MINOR
     *  2.0             20    0
     *  2.1             20    10
     *  2.11            20    11
     *  3.0             20    30
     *  4.0             20    40
     */
    if (info[QSV_VERSION_MAJOR - 1] == 20 && info[QSV_VERSION_MINOR - 1] > 11) {
        int major = (int) (info[QSV_VERSION_MINOR - 1] / 10);
        if (info[QSV_VERSION_REVISION - 1] != 0) {
            sprintf(buffer, "OS/2 PM %d.%d revision %d", major,
                    (int) (info[QSV_VERSION_MINOR - 1] - major * 10),
                    (int) info[QSV_VERSION_REVISION - 1]);
        } else {
            sprintf(buffer, "OS/2 PM %d.%d", major,
                    (int) (info[QSV_VERSION_MINOR - 1] - major * 10));
        }
    } else {
        if (info[QSV_VERSION_REVISION - 1] != 0) {
            sprintf(buffer, "OS/2 PM %d.%d revision %d",
                    (int) (info[QSV_VERSION_MAJOR - 1] / 10),
                    (int) info[QSV_VERSION_MINOR - 1],
                    (int) info[QSV_VERSION_REVISION - 1]);
        } else {
            sprintf(buffer, "OS/2 PM %d.%d",
                    (int) (info[QSV_VERSION_MAJOR - 1] / 10),
                    (int) info[QSV_VERSION_MINOR - 1]);
        }
    }
#ifdef VERBOSE
    printf("TkGetServerInfo [%s]\n", buffer);
#endif
    Tcl_SetResult(interp, buffer, TCL_VOLATILE);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetHMODULE --
 *
 *      Retrieves the global instance handle used by the Tk library.
 *
 * Results:
 *      Returns the library module handle.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

HMODULE
Tk_GetHMODULE()
{
#ifdef VERBOSE
    printf("Tk_GetHMODULE\n");
#endif
    return dllHandle;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2XInit --
 *
 *	Initialize Xlib emulation layer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up various data structures.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2XInit(modHandle)
    unsigned long modHandle;
{
    BOOL success;

#ifdef VERBOSE
    printf("TkOS2XInit\n");
#endif
    if (TkOS2InitPM() == (HAB)NULLHANDLE) {
        panic("Unable to initialize Presentation Manager");
    }

    if (childClassInitialized != 0) {
        return;
    }
    childClassInitialized = 1;

    /*
     * Register the Child window class.
     */

    /*
     * Don't use CS_SIZEREDRAW for the child, this will make vertical resizing
     * work incorrectly (keeping the same distance from the bottom instead of
     * from the top when using Tk's "pack ... -side top").
     */
    success = WinRegisterClass(TclOS2GetHAB(), TOC_CHILD, TkOS2ChildProc, 0,
                               sizeof(ULONG));
    if (!success) {
        panic("Unable to register TkChild class");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2XCleanup --
 *
 *      Removes the registered classes for Tk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes window classes from the system.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2XCleanup(hModule)
    HMODULE hModule;
{
    /*
     * Clean up our own class.
     */

#ifdef VERBOSE
    printf("TkOS2XCleanup\n");
#endif
    if (childClassInitialized) {
        childClassInitialized = 0;
    }

    /*
     * And let the window manager clean up its own class(es).
     */

    TkOS2WmCleanup(hModule);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetDefaultScreenName --
 *
 *      Returns the name of the screen that Tk should use during
 *      initialization.
 *
 * Results:
 *      Returns a statically allocated string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
TkGetDefaultScreenName(interp, screenName)
    Tcl_Interp *interp;         /* Not used. */
    char *screenName;           /* If NULL, use default string. */
{
    char *DISPLAY = NULL;

#ifdef VERBOSE
    printf("TkGetDefaultScreenName [%s] ", screenName);
#endif
    if ((screenName == NULL) || (screenName[0] == '\0')) {
        DISPLAY = getenv("DISPLAY");
        if (DISPLAY != NULL) {
            screenName = DISPLAY;
        } else {
            screenName = os2ScreenName;
        }
    }
#ifdef VERBOSE
    printf("returns [%s]\n", screenName);
#endif
    return screenName;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpOpenDisplay --
 *
 *	Create the Display structure and fill it with device
 *	specific information.
 *
 * Results:
 *	Returns a TkDisplay structure on success or NULL on failure.
 *
 * Side effects:
 *	Allocates a new TkDisplay structure.
 *
 *----------------------------------------------------------------------
 */

TkDisplay *
TkpOpenDisplay(display_name)
    char *display_name;
{
    Screen *screen;
    TkOS2Drawable *todPtr;
    Display *display;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("TkpOpenDisplay(\"%s\"), os2Display %x\n", display_name,
           tsdPtr->os2Display);
#endif

    if (tsdPtr->os2Display != NULL) {
	if (strcmp(tsdPtr->os2Display->display->display_name,
                   display_name) == 0) {
	    return tsdPtr->os2Display;
	} else {
	    return NULL;
	}
    }

    display = (Display *) ckalloc(sizeof(Display));
    if (!display) {
        return NULL;
    }
#ifdef VERBOSE
    printf("ckalloc Display returned %x\n", display);
#endif
    display->display_name = (char *) ckalloc(strlen(display_name)+1);
    if (!display->display_name) {
	ckfree((char *)display);
        return NULL;
    }
    strcpy(display->display_name, display_name);

    display->cursor_font = 1;
    display->nscreens = 1;
    display->request = 1;
    display->qlen = 0;

    screen = (Screen *) ckalloc(sizeof(Screen));
    if (!screen) {
	ckfree((char *)display->display_name);
	ckfree((char *)display);
	return NULL;
    }
#ifdef VERBOSE
    printf("ckalloc Screen returned %x\n", screen);
#endif
    screen->display = display;

    screen->width = aDevCaps[CAPS_WIDTH];
    screen->height = yScreen;
    screen->mwidth = (screen->width*1000)/ aDevCaps[CAPS_HORIZONTAL_RESOLUTION];
    screen->mheight = (screen->height*1000)/ aDevCaps[CAPS_VERTICAL_RESOLUTION];
#ifdef VERBOSE
    printf("screen: %dx%d (%dmmx%dmm) (resolution %dx%d)\n",
           screen->width, screen->height, screen->mwidth, screen->mheight,
           aDevCaps[CAPS_HORIZONTAL_RESOLUTION],
           aDevCaps[CAPS_VERTICAL_RESOLUTION]);
#endif

    /*
     * Set up the root window.
     */

    todPtr = (TkOS2Drawable*) ckalloc(sizeof(TkOS2Drawable));
    if (!todPtr) {
	ckfree((char *)display->display_name);
	ckfree((char *)display);
	ckfree((char *)screen);
	return NULL;
    }
#ifdef VERBOSE
    printf("    new todPtr (drawable) %x\n", todPtr);
#endif
    todPtr->type = TOD_WINDOW;
    todPtr->window.winPtr = NULL;
    todPtr->window.handle = HWND_DESKTOP;
    screen->root = (Window)todPtr;

    /*
     * On OS/2, when creating a color bitmap, need two pieces of
     * information: the number of color planes and the number of
     * pixels per plane.  Need to remember both quantities so that
     * when constructing an HBITMAP for offscreen rendering, we can
     * specify the correct value for the number of planes.  Otherwise
     * the HBITMAP won't be compatible with the HWND and we'll just
     * get blank spots copied onto the screen.
     */

    screen->ext_data = (XExtData *) aDevCaps[CAPS_COLOR_PLANES];
    screen->root_depth = aDevCaps[CAPS_COLOR_BITCOUNT] * (int) screen->ext_data;

    screen->root_visual = (Visual *) ckalloc(sizeof(Visual));
    if (!screen->root_visual) {
	ckfree((char *)display->display_name);
	ckfree((char *)display);
	ckfree((char *)screen);
	ckfree((char *)todPtr);
	return NULL;
    }
    screen->root_visual->visualid = 0;
    if ( aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER ) {
	screen->root_visual->map_entries = aDevCaps[CAPS_COLORS];
	screen->root_visual->class = PseudoColor;
        screen->root_visual->red_mask = 0x0;
        screen->root_visual->green_mask = 0x0;
        screen->root_visual->blue_mask = 0x0;
    } else {
	if (screen->root_depth == 4) {
	    screen->root_visual->class = StaticColor;
	    screen->root_visual->map_entries = 16;
screen->root_visual->map_entries = aDevCaps[CAPS_COLOR_INDEX]+1;
        } else if (screen->root_depth == 8) {
            screen->root_visual->class = StaticColor;
            screen->root_visual->map_entries = 256;
        } else if (screen->root_depth == 12) {
            screen->root_visual->class = TrueColor;
            screen->root_visual->map_entries = 32;
screen->root_visual->map_entries = aDevCaps[CAPS_COLOR_INDEX]+1;
            screen->root_visual->red_mask = 0xf0;
            screen->root_visual->green_mask = 0xf000;
            screen->root_visual->blue_mask = 0xf00000;
	} else if (screen->root_depth == 16) {
	    screen->root_visual->class = TrueColor;
	    screen->root_visual->map_entries = 64;
screen->root_visual->map_entries = aDevCaps[CAPS_COLOR_INDEX]+1;
            screen->root_visual->red_mask = 0xf8;
            screen->root_visual->green_mask = 0xfc00;
            screen->root_visual->blue_mask = 0xf80000;
	    /*
	    screen->root_visual->map_entries = aDevCaps[CAPS_COLOR_INDEX]+1;
	    screen->root_visual->red_mask = 0xff;
	    screen->root_visual->green_mask = 0xff00;
	    screen->root_visual->blue_mask = 0xff0000;
	    */
	} else if (screen->root_depth >= 24) {
	    screen->root_visual->class = TrueColor;
	    screen->root_visual->map_entries = 256;
	    screen->root_visual->red_mask = 0xff;
	    screen->root_visual->green_mask = 0xff00;
	    screen->root_visual->blue_mask = 0xff0000;
	}
    }
    screen->root_visual->bits_per_rgb = screen->root_depth;

    /*
     * Note that these pixel values are not palette relative.
     */

    screen->white_pixel = RGB(255, 255, 255);
    screen->black_pixel = RGB(0, 0, 0);

    display->screens        = screen;
    display->nscreens       = 1;
    display->default_screen = 0;
    screen->cmap = XCreateColormap(display, None, screen->root_visual,
	    AllocNone);
    tsdPtr->os2Display = (TkDisplay *) ckalloc(sizeof(TkDisplay));
    if (!tsdPtr->os2Display) {
        ckfree((char *)screen->root_visual);
	ckfree((char *)display->display_name);
	ckfree((char *)display);
	ckfree((char *)screen);
	ckfree((char *)todPtr);
        return NULL;
    }
    tsdPtr->os2Display->display = display;
#ifdef VERBOSE
    printf("    os2Display %x, display %x, screens %x\n", tsdPtr->os2Display,
           tsdPtr->os2Display->display, tsdPtr->os2Display->display->screens);
#endif
    tsdPtr->updatingClipboard = FALSE;
    return tsdPtr->os2Display;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCloseDisplay --
 *
 *      Closes and deallocates a Display structure created with the
 *      TkpOpenDisplay function.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees up memory.
 *
 *----------------------------------------------------------------------
 */

void
TkpCloseDisplay(dispPtr)
    TkDisplay *dispPtr;
{
    Display *display = dispPtr->display;
    HWND hwnd;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("TkpCloseDisplay TkDisplay %x, Display %x\n", dispPtr, display);
#endif
    if (dispPtr != tsdPtr->os2Display) {
        panic("TkpCloseDisplay: tried to call TkpCloseDisplay on another display
");
        return;
    }

    /*
     * Force the clipboard to be rendered if we are the clipboard owner.
     */

    if (dispPtr->clipWindow) {
        hwnd = Tk_GetHWND(Tk_WindowId(dispPtr->clipWindow));
        if (WinQueryClipbrdOwner(TclOS2GetHAB()) == hwnd) {
            WinOpenClipbrd(TclOS2GetHAB());
            WinEmptyClipbrd(TclOS2GetHAB());
            TkOS2ClipboardRender(dispPtr, CF_TEXT);
            WinCloseClipbrd(TclOS2GetHAB());
        }
    }

    /* Memory gets freed via dispPtr */
    tsdPtr->os2Display = NULL;

    if (display->display_name != (char *) NULL) {
        ckfree(display->display_name);
    }
    if (display->screens != (Screen *) NULL) {
        if (display->screens->root_visual != NULL) {
            ckfree((char *) display->screens->root_visual);
        }
        if (display->screens->root != None) {
            ckfree((char *) display->screens->root);
        }
        if (display->screens->cmap != None) {
            XFreeColormap(display, display->screens->cmap);
        }
        ckfree((char *) display->screens);
    }
    ckfree((char *) display);
    ckfree((char *) dispPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * XBell --
 *
 *	Generate a beep.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Plays a sound.
 *
 *----------------------------------------------------------------------
 */

void
XBell(display, percent)
    Display* display;
    int percent;
{
    if (!WinAlarm(HWND_DESKTOP, WA_NOTE)) {
        DosBeep (770L, 300L);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ChildProc --
 *
 *	Callback from Presentation Manager whenever an event occurs on
 *	a child window.
 *
 * Results:
 *	Standard OS/2 PM return value.
 *
 * Side effects:
 *	Default window behavior.
 *
 *----------------------------------------------------------------------
 */

MRESULT EXPENTRY
TkOS2ChildProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    MRESULT result;
#ifdef VERBOSE
    printf("TkOS2ChildProc hwnd %x, msg %x, p1 %x, p2 %x\n", hwnd, message,
           param1, param2);
    switch (message) {
        case WM_ACTIVATE: printf("   WM_ACTIVATE Child %x\n", hwnd); break;
        case WM_ADJUSTFRAMEPOS: printf("   WM_ADJUSTFRAMEPOS Child %x\n", hwnd); break;
        case WM_ADJUSTWINDOWPOS:
            printf("  WM_ADJUSTWINDOWPOS Child %x (%d,%d) %dx%d fl %x\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
                   ((PSWP)param1)->cy, ((PSWP)param1)->fl);
            break;
        case WM_BUTTON1DOWN: printf("   WM_BUTTON1DOWN Child %x\n", hwnd); break;
        case WM_BUTTON1UP: printf("   WM_BUTTON1UP Child %x\n", hwnd); break;
        case WM_BUTTON1DBLCLK: printf("   WM_BUTTON1DBLCLK Child %x\n", hwnd); break;
        case WM_BUTTON2DOWN: printf("   WM_BUTTON2DOWN Child %x\n", hwnd); break;
        case WM_BUTTON2UP: printf("   WM_BUTTON2UP Child %x\n", hwnd); break;
        case WM_BUTTON2DBLCLK: printf("   WM_BUTTON2DBLCLK Child %x\n", hwnd); break;
        case WM_BUTTON3DOWN: printf("   WM_BUTTON3DOWN Child %x\n", hwnd); break;
        case WM_BUTTON3UP: printf("   WM_BUTTON3UP Child %x\n", hwnd); break;
        case WM_BUTTON3DBLCLK: printf("   WM_BUTTON3DBLCLK Child %x\n", hwnd); break;
        case WM_CALCFRAMERECT: printf("   WM_CALCFRAMERECT Child %x\n", hwnd); break;
        case WM_CALCVALIDRECTS: printf("   WM_CALCVALIDRECTS Child %x\n", hwnd); break;
        case WM_CLOSE: printf("   WM_CLOSE Child %x\n", hwnd); break;
        case WM_COMMAND: printf("   WM_COMMAND Child %x cmd %d s %d p %d\n",
                                hwnd, SHORT1FROMMP(param1),
                                SHORT1FROMMP(param2), SHORT2FROMMP(param2));
             break;
        case WM_CREATE: printf("   WM_CREATE Child %x\n", hwnd); break;
        case WM_DESTROY: printf("   WM_DESTROY Child %x\n", hwnd); break;
        case WM_ERASEBACKGROUND: printf("   WM_ERASEBACKGROUND Child %x\n", hwnd); break;
        case WM_FOCUSCHANGE: printf("   WM_FOCUSCHANGE Child %x\n", hwnd); break;
        case WM_FORMATFRAME: printf("   WM_FORMATFRAME Child %x\n", hwnd); break;
        case WM_HSCROLL: printf("   WM_HSCROLL Child %x\n", hwnd); break;
        case WM_MINMAXFRAME: printf("   WM_MINMAXFRAME Child %x\n", hwnd); break;
        case WM_MOUSEMOVE: printf("   WM_MOUSEMOVE Child %x p1 %x p2 %x\n",
                                  hwnd, param1, param2); break;
        case WM_MOVE: printf("   WM_MOVE Child %x\n", hwnd); break;
        case WM_OWNERPOSCHANGE: printf("   WM_OWNERPOSCHANGE Child %x\n", hwnd); break;
        case WM_PAINT: printf("   WM_PAINT Child %x\n", hwnd); break;
        case WM_QUERYBORDERSIZE: printf("   WM_QUERYBORDERSIZE Child %x\n", hwnd); break;
        case WM_QUERYDLGCODE: printf("   WM_QUERYDLGCODE Child %x\n", hwnd); break;
        case WM_QUERYFRAMECTLCOUNT: printf("   WM_QUERYFRAMECTLCOUNT Child %x\n", hwnd); break;
        case WM_QUERYFOCUSCHAIN: printf("   WM_QUERYFOCUSCHAIN Child %x\n", hwnd); break;
        case WM_QUERYICON: printf("   WM_QUERYICON Child %x\n", hwnd); break;
        case WM_QUERYTRACKINFO: printf("   WM_QUERYTRACKINFO Child %x\n", hwnd); break;
        case WM_REALIZEPALETTE: printf("   WM_REALIZEPALETTE Child %x\n", hwnd); break;
        case WM_SETFOCUS: printf("   WM_SETFOCUS Child %x\n", hwnd); break;
        case WM_SETSELECTION: printf("   WM_SETSELECTION Child %x\n", hwnd); break;
        case WM_TRANSLATEACCEL: printf("   WM_TRANSLATEACCEL Child %x\n", hwnd); break;
        case WM_UPDATEFRAME: printf("   WM_UPDATEFRAME Child %x\n", hwnd); break;
        case WM_VSCROLL: printf("   WM_VSCROLL Child %x\n", hwnd); break;
        case WM_WINDOWPOSCHANGED:
            printf("  WM_WINDOWPOSCHANGED Child %x (%d,%d) %dx%d fl %x\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
                   ((PSWP)param1)->cy, ((PSWP)param1)->fl);
            break;
    }
#endif

    switch (message) {
        case WM_CONTROLPOINTER:
            /*
             * Short circuit the WM_CONTROLPOINTER message since we set
             * the cursor elsewhere.
             */

            result = param2;
            break;

        case WM_CREATE:
        case WM_UPDATEFRAME:
        case WM_ERASEBACKGROUND:
            result = 0;
            break;

        case WM_PAINT:
            GenerateXEvent(hwnd, message, param1, param2);
            result = WinDefWindowProc(hwnd, message, param1, param2);
            break;

        case TK_CLAIMFOCUS:
        case TK_GEOMETRYREQ:
        case TK_ATTACHWINDOW:
        case TK_DETACHWINDOW:
#ifdef VERBOSE
            printf("TK_CLAIMFOCUS/GEOMETRYREQ/ATTACHWIN/DETACHWIN");
	    printf(" h %x, p1 %x\n", hwnd, param1);
	    fflush(stdout);
#endif
            result = TkOS2EmbeddedEventProc(hwnd, message, param1, param2);
            break;

        default:
#ifdef VERBOSE
            printf("default, trying Tk_TranslateOS2Event\n");
	    fflush(stdout);
#endif
            if (!Tk_TranslateOS2Event(hwnd, message, param1, param2,
                    &result)) {
#ifdef VERBOSE
                printf("Tk_TranslateOS2Event FALSE => WinDefWindowProc\n");
	        fflush(stdout);
#endif
                result = WinDefWindowProc(hwnd, message, param1, param2);
#ifdef VERBOSE
            } else {
                printf("Tk_TranslateOS2Event TRUE\n");
	        fflush(stdout);
#endif
            }
            break;
    }
#ifdef VERBOSE
    switch (message) {
        case WM_WINDOWPOSCHANGED:
            printf("   WM_WINDOWPOSCHANGED Child %x now (%d,%d) %dx%d\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
                   ((PSWP)param1)->cy);
            break;
    }
#endif

    /*
     * Handle any newly queued events before returning control to PM.
     */

    Tcl_ServiceAll();
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_TranslateOS2Event --
 *
 *	This function is called by the window procedures to handle
 *	the translation from OS/2 PM events to Tk events.
 *
 * Results:
 *	Returns 1 if the event was handled, else 0.
 *
 * Side effects:
 *	Depends on the event.
 *
 *----------------------------------------------------------------------
 */

int
Tk_TranslateOS2Event(hwnd, message, param1, param2, resultPtr)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
    MRESULT *resultPtr;
{
#ifdef VERBOSE
    printf("Tk_TranslateOS2Event hwnd %x msg %x p1 %x p2 %x resultPtr %x\n",
           hwnd, message, param1, param2, resultPtr);
    fflush(stdout);
#endif
    *resultPtr = (MPARAM)0;
    switch (message) {
        case WM_RENDERFMT: {
            TkWindow *winPtr = (TkWindow *) Tk_HWNDToWindow(hwnd);
#ifdef VERBOSE
            printf("Tk_HWNDToWindow(%x) winPtr %x\n", hwnd, winPtr);
#endif
            if (winPtr) {
                TkOS2ClipboardRender(winPtr->dispPtr,
                                     (ULONG)(SHORT1FROMMP(param1)));
            }
            return 1;
        }

        case WM_CONTROL:
        case WM_VSCROLL:
        case WM_HSCROLL: {
            /*
             * Reflect these messages back to the sender so that they
             * can be handled by the window proc for the control.  Note
             * that we need to be careful not to reflect a message that
             * is targeted to this window, or we will loop.
             */

            HWND target = WinWindowFromID(hwnd, (ULONG)(SHORT1FROMMP(param1)));
            if (target && target != hwnd) {
                *resultPtr = WinSendMsg(target, message, param1, param2);
                return 1;
            }
            break;
            return 0;
        }

        case WM_BUTTON1DOWN:
        case WM_BUTTON1UP:
        case WM_BUTTON2DOWN:
        case WM_BUTTON2UP:
        case WM_BUTTON3DOWN:
        case WM_BUTTON3UP:
#ifdef VERBOSE
             printf("calling Tk_PointerEvent(%hd,%hd) for WM_BUTTON%s\n",
                    SHORT1FROMMP(param1), SHORT2FROMMP(param1),
                    message == WM_BUTTON1DOWN ? "1DOWN" :
                    (message == WM_BUTTON2DOWN ? "2DOWN" :
                    (message == WM_BUTTON3DOWN ? "3DOWN" :
                    (message == WM_BUTTON1UP ? "1UP" :
                    (message == WM_BUTTON2UP ? "2UP" :
                    (message == WM_BUTTON3UP ? "3UP" : "???"))))));
#endif
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
            *resultPtr = (MPARAM)1;
            return 1;

        case WM_BUTTON1DBLCLK:
	    message = WM_BUTTON1DOWN;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON1UP;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON1DOWN;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON1UP;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
            *resultPtr = (MPARAM)1;
            return 1;

        case WM_BUTTON2DBLCLK:
	    message = WM_BUTTON2DOWN;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON2UP;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON2DOWN;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON2UP;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
            *resultPtr = (MPARAM)1;
            return 1;

        case WM_BUTTON3DBLCLK:
	    message = WM_BUTTON3DOWN;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON3UP;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON3DOWN;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
	    message = WM_BUTTON3UP;
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
            *resultPtr = (MPARAM)1;
            return 1;

        case WM_MOUSEMOVE:
#ifdef VERBOSE
            printf("Child %x: WM_MOUSEMOVE (%hd,%hd) HIT %x, fl %x\n", hwnd,
                   SHORT1FROMMP(param1), SHORT2FROMMP(param1),
                   SHORT1FROMMP(param2), SHORT2FROMMP(param2));
#endif
            Tk_PointerEvent(hwnd, SHORT1FROMMP(param1), SHORT2FROMMP(param1));
            *resultPtr = (MPARAM)1;
            return 1;

        case WM_CLOSE:
#ifdef VERBOSE
            printf("Child: WM_CLOSE\n");
#endif
        case WM_SETFOCUS:
#ifdef VERBOSE
            printf("Child: WM_SETFOCUS\n");
#endif
        case WM_DESTROYCLIPBOARD:
#ifdef VERBOSE
            printf("Child: WM_DESTROYCLIPBOARD\n");
#endif
        case WM_CHAR:
#ifdef VERBOSE
            printf("Child: WM_CHAR\n");
            fflush(stdout);
#endif
            GenerateXEvent(hwnd, message, param1, param2);
            return 1;

        /* Added by/for Ilya Zakharevich: */
        case WM_TRANSLATEACCEL: {
            PQMSG pqmsg = PVOIDFROMMP(param1);
            USHORT flags = (USHORT) SHORT1FROMMP(pqmsg->mp1);
            USHORT charcode = (USHORT) SHORT1FROMMP(pqmsg->mp2);
            USHORT vkeycode = (USHORT) SHORT2FROMMP(pqmsg->mp2);
#ifdef VERBOSE
            printf("Child: WM_TRANSLATEACCEL vk %x char %x fl %x\n", vkeycode,
                   charcode, flags);
#endif

            if (flags & KC_VIRTUALKEY) {
                switch (vkeycode) {
                    case VK_F1:
                    case VK_F10:
                    case VK_SPACE:
                        /* Do not consider as a shortcut */

                        return 1;
                }
            } else if ( charcode == ' ') { /* Alt-Space */
                /* Do not consider as a shortcut */
                return 1;
            }
            break;
        }
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateXEvent --
 *
 *      This routine generates an X event from the corresponding
 *      OS/2 PM event.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues one or more X events.
 *
 *----------------------------------------------------------------------
 */

static void
GenerateXEvent(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    XEvent event;
    TkWindow *winPtr = (TkWindow *)Tk_HWNDToWindow(hwnd);
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    LONG windowHeight = TkOS2HwndHeight(WinQueryWindow(hwnd, QW_PARENT));
    printf("GenerateXEvent hwnd %x (winPtr %x) msg %x p1 %x p2 %x, height %d\n",
           hwnd, winPtr, message, param1, param2, windowHeight);
#endif

    if (!winPtr || winPtr->window == None) {
#ifdef VERBOSE
        printf("null winPtr or no winPtr->window, returning\n");
#endif
        return;
    }

    event.xany.serial = winPtr->display->request++;
    event.xany.send_event = False;
    event.xany.display = winPtr->display;
    event.xany.window = winPtr->window;

    switch (message) {
        case WM_PAINT: {
            HPS hps;
            RECTL rectl;

            event.type = Expose;
            hps= WinBeginPaint(hwnd, NULLHANDLE, &rectl);
#ifdef VERBOSE
            WinFillRect(hps, &rectl, CLR_RED);
            if (hps==NULLHANDLE) {
                printf("WinBeginPaint hwnd %x ERROR %x\n", hwnd,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("WinBeginPaint hwnd %x is %x\n", hwnd, hps);
            }
#endif
            event.xexpose.x = rectl.xLeft;
            /* PM coordinates reversed */
            event.xexpose.y = TkOS2TranslateY(hwnd, rectl.yTop, 0);
            event.xexpose.width = rectl.xRight - rectl.xLeft;
            event.xexpose.height = rectl.yTop - rectl.yBottom;
            WinEndPaint(hps);
#ifdef VERBOSE
            printf("EVENT hwnd %x Expose (%d,%d) %dx%d WM_PAINT %d,%d->%d,%d\n",
	           hwnd, event.xexpose.x, event.xexpose.y, event.xexpose.width,
		   event.xexpose.height, rectl.xLeft, rectl.yBottom,
		   rectl.xRight, rectl.yTop);
#endif
            event.xexpose.count = 0;
            break;
        }

        case WM_CLOSE:
            event.type = ClientMessage;
            event.xclient.message_type =
                Tk_InternAtom((Tk_Window) winPtr, "WM_PROTOCOLS");
            event.xclient.format = 32;
            event.xclient.data.l[0] =
                Tk_InternAtom((Tk_Window) winPtr, "WM_DELETE_WINDOW");
            break;

        case WM_SETFOCUS: {
            TkWindow *otherWinPtr = (TkWindow *)Tk_HWNDToWindow((HWND) param1);
#ifdef VERBOSE
            printf("Tk_HWNDToWindow(%x) otherWinPtr %x\n", param1, otherWinPtr);
#endif

            /*
             * Compare toplevel windows to avoid reporting focus
             * changes within the same toplevel.
             */

            while (!(winPtr->flags & TK_TOP_LEVEL)) {
                winPtr = winPtr->parentPtr;
                if (winPtr == NULL) {
#ifdef VERBOSE
                    printf("WM_SETFOCUS, NULL winPtr\n");
#endif
                    return;
                }
            }
            while (otherWinPtr && !(otherWinPtr->flags & TK_TOP_LEVEL)) {
                otherWinPtr = otherWinPtr->parentPtr;
            }
            if (otherWinPtr == winPtr) {
#ifdef VERBOSE
                printf("WM_SETFOCUS, otherWinptr == winPtr (%x)\n", winPtr);
#endif
                return;
            }

            event.xany.window = winPtr->window;
            event.type = SHORT1FROMMP(param2) == TRUE ? FocusIn : FocusOut;
            event.xfocus.mode = NotifyNormal;
            event.xfocus.detail = NotifyNonlinear;
#ifdef VERBOSE
            printf("EVENT hwnd %x %s\n", hwnd,
	           SHORT1FROMMP(param2) == TRUE ? "FocusIn" : "FocusOut");
#endif
            break;
        }

        case WM_DESTROYCLIPBOARD:
            if (tsdPtr->updatingClipboard == TRUE) {
                /*
                 * We want to avoid this event if we are the ones that caused
                 * this event.
                 */
                return;
            }
            event.type = SelectionClear;
            event.xselectionclear.selection =
                Tk_InternAtom((Tk_Window)winPtr, "CLIPBOARD");
            event.xselectionclear.time = TkpGetMS();
            break;

/*
 * In case one we get a version supporting WM_MOUSEWHEEL (then remove
 * the #define at the start of this file):
 */
        case WM_MOUSEWHEEL:
            /*
             * The mouse wheel event is closer to a key event than a
             * mouse event in that the message is sent to the window
             * that has focus.
             */
        case WM_CHAR: {
            /*
             * Careful: for each keypress, two of these messages are
             * generated, one when pressed and one when released.
             * When the keyboard-repeat is triggered, multiple key-
             * down messages can be generated. If this goes faster
             * than they are retrieved from the queue, they can be
             * combined in one message.
             */
            USHORT flags= CHARMSG(&message)->fs;
            UCHAR krepeat= CHARMSG(&message)->cRepeat;
            USHORT charcode= CHARMSG(&message)->chr;
            USHORT vkeycode= CHARMSG(&message)->vkey;
            unsigned int state = GetState(message, param1, param2);
            Time time = TkpGetMS();
            POINTL point;
#ifdef VERBOSE
            printf("WM_CHAR flags %x krepeat %d charcode %d vkeycode %x\n",
                   flags, krepeat, charcode, vkeycode);
#endif

            /*
             * Compute the screen and window coordinates of the event.
             * Set up the common event fields.
             */

            WinQueryMsgPos(TclOS2GetHAB(), &point);
            /* point now in screen coordinates */
            event.xbutton.x_root = point.x;
            event.xbutton.y_root = yScreen - point.y;	/* PM y reversed */
            event.xbutton.root = RootWindow(winPtr->display,
                    winPtr->screenNum);

            WinMapWindowPoints(HWND_DESKTOP, hwnd, &point, 1);
            /* point now in window coordinates */
            event.xbutton.subwindow = None;
            event.xbutton.x = point.x;
            event.xbutton.y = TkOS2TranslateY(hwnd, point.y, 0);
#ifdef VERBOSE
            printf("EVENT hwnd %x Key: (%d,%d) PM: (%d,%d) state %x flags %x\n",
                   hwnd, event.xbutton.x, event.xbutton.y, point.x, point.y,
                   state, flags);
#endif
            event.xbutton.state = state;
            event.xbutton.time = time;
            event.xbutton.same_screen = True;

            /*
             * Now set up event specific fields.
             */

            switch (message) {
                case WM_MOUSEWHEEL:
                    /*
                     * We have invented a new X event type to handle
                     * this event.  It still uses the KeyPress struct.
                     * However, the keycode field has been overloaded
                     * to hold the zDelta of the wheel.
                     */

                    event.type = MouseWheelEvent;
                    event.xany.send_event = -1;
                    event.xkey.keycode = charcode;
                    event.xkey.nbytes = 0;
                    break;

                case WM_CHAR:
#ifdef VERBOSE
                    if ( (flags & KC_ALT) && !(flags & KC_KEYUP)
                         && !(flags & KC_CTRL) && (vkeycode != VK_CTRL)
                         && (vkeycode != VK_F10) ) {
                        /* Equivalent to Windows' WM_SYSKEYDOWN */
                        printf("Equivalent of WM_SYSKEYDOWN...\n");
	            }
                    if ( (flags & KC_ALT) && (flags & KC_KEYUP)
                         && !(flags & KC_CTRL) && (vkeycode != VK_CTRL)
                         && (vkeycode != VK_F10) ) {
                        /* Equivalent to Windows' WM_SYSKEYUP */
                        printf("Equivalent of WM_SYSKEYUP...\n");
                    }
#endif
                    if ( flags & KC_KEYUP ) {
                        /* Key Up */
#ifdef VERBOSE
                        printf("KeyUp\n");
#endif
                        event.type = KeyRelease;
                        /* Added by/for Ilya Zakharevich */
                        /* KeyRelease for alphanums gets ORed with 0x*00 */
                        if ( !(flags & KC_VIRTUALKEY) ) {
                            charcode = charcode & 0xFF;
                            if (charcode && charcode <= 0x1F &&
                                (flags & KC_CTRL)) {
#ifdef VERBOSE
                                printf("KeyUp ADDING\n");
#endif
                                charcode += ((flags & KC_SHIFT) ? 'A'-1
                                                                : 'a'-1);
                            }
                        }
                    } else {
                        /* Key Down */
#ifdef VERBOSE
                        printf("KeyDown\n");
#endif
                        event.type = KeyPress;
                    }
                    if ( flags & KC_VIRTUALKEY ) {
                       /* Next if() added by/for Ilya Zakharevich */
                        if ( (flags & KC_SHIFT) && vkeycode == VK_BACKTAB ) {
                            vkeycode = VK_TAB;
                        }
                        /* vkeycode is valid, should be given precedence */
                        event.xkey.keycode = vkeycode;
#ifdef VERBOSE
                        printf("virtual keycode %x (%s)\n", vkeycode,
                               vkeycode == VK_ENTER ? "VK_ENTER" :
                               (vkeycode == VK_NEWLINE ? "VK_NEWLINE" :
                               (vkeycode == VK_ESC ? "VK_ESC" :
                               (vkeycode == VK_UP ? "VK_UP" :
                               (vkeycode == VK_DOWN ? "VK_DOWN" :
                               (vkeycode == VK_LEFT ? "VK_LEFT" :
                               (vkeycode == VK_RIGHT ? "VK_RIGHT" :
                               (vkeycode == VK_ALT ? "VK_ALT" :
                               (vkeycode == VK_TAB ? "VK_TAB" :
                               (vkeycode == VK_F1 ? "VK_F1" :
                               (vkeycode == VK_F10 ? "VK_F10" : "OTHER"
                               )))))))))));
#endif
                    } else {
                        /* Move to VK_USER range */
                        event.xkey.keycode = (charcode & 0xFF) + 0x100;
                    }
                    if ( flags & KC_CHAR ) {
                        int loop;
                        /* charcode is valid */
                        event.xkey.nbytes = krepeat;
                        for ( loop=0; loop < krepeat; loop++ ) {
                                event.xkey.trans_chars[loop] = charcode;
#ifdef VERBOSE
                        printf("charcode 0x%x=%d (%c)\n", charcode, charcode,
                               charcode);
#endif
                        }
                    } else {
                        /*
                         * No KC_CHAR, but char can be valid with KC_ALT,
                         * KC_CTRL when it is not 0.
                         */
#ifdef VERBOSE
                        printf("no KC_CHAR, charcode %d (0x%x)\n", charcode,
                               charcode);
                        fflush(stdout);
#endif
                        /* KC_VIRTUALKEY criterion added by Ilya Zakharevich */
                        if ( (flags & KC_ALT || flags & KC_CTRL)
                             && charcode != 0 && !(flags & KC_VIRTUALKEY)) {
                            int loop;
#ifdef VERBOSE
                            printf("KC_ALT/KC_CTRL and non-0 char %c (%x)\n",
                                   charcode, charcode);
                            fflush(stdout);
#endif
                            event.xkey.keycode = charcode;
                            event.xkey.nbytes = krepeat;
                            for ( loop=0; loop < krepeat; loop++ ) {
                                event.xkey.trans_chars[loop] = charcode;
#ifdef VERBOSE
                                printf("charcode 0x%x=%d (%c)\n", charcode,
                                       charcode, charcode);
#endif
                            }
                        } else {
#ifdef VERBOSE
                            printf("KC_ALT %x KC_CTRL %x charcode %x KC_VIRTUALKEY %x\n",
                                   flags & KC_ALT, flags & KC_CTRL, charcode,
                                   flags & KC_VIRTUALKEY);
                            fflush(stdout);
#endif
                            event.xkey.nbytes = 0;
                        }
                        event.xkey.trans_chars[0] = 0;
                    }

                    /*
                     * Setting xany.send_event to -1 indicates to the
                     * OS/2 implementation of TkpGetString() that this
                     * event was generated by OS/2 and that the OS/2
                     * extension xkey.trans_chars is filled with
                     * multibyte characters.
                     */

                    event.xany.send_event = -1;
                    break;
            }
            break;
        }

        default:
            return;
    }
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * GetState --
 *
 *	This function constructs a state mask for the mouse buttons 
 *	and modifier keys as they were before the event occured.
 *
 * Results:
 *	Returns a composite value of all the modifier and button state
 *	flags that were set at the time the event occurred.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned int
GetState(message, param1, param2)
    ULONG message;		/* OS/2 PM message type */
    MPARAM param1;		/* param1 of message, used if key message */
    MPARAM param2;		/* param2 of message, used if key message */
{
    int mask;
    int prevState = 0;		/* 1 if key was previously down */
    unsigned int state = TkOS2GetModifierState();
#ifdef VERBOSE
    printf("GetState mp1 %x mp2 %x\n", param1, param2);
#endif

    /*
     * If the event is a key press or release, we check for modifier
     * keys so we can report the state of the world before the event.
     */

    if (message == WM_CHAR) {
        USHORT flags = SHORT1FROMMP(param1);
        USHORT vkeycode = SHORT2FROMMP(param2); /* function key */
        mask = 0;
        prevState = flags & KC_PREVDOWN;
        switch (vkeycode) {
            case VK_SHIFT:
                mask = ShiftMask;
                break;
            case VK_CTRL:
                mask = ControlMask;
                break;
            case VK_ALT:
            case VK_ALTGRAF:
            case VK_MENU:
                mask = ALT_MASK;
                break;
            case VK_CAPSLOCK:
                if (!(flags & KC_KEYUP)) {
                    mask = LockMask;
                    prevState = ((state & mask) ^ prevState) ? 0 : 1;
                }
            case VK_NUMLOCK:
                if (!(flags & KC_KEYUP)) {
                    mask = Mod1Mask;
                    prevState = ((state & mask) ^ prevState) ? 0 : 1;
                }
                break;
            case VK_SCRLLOCK:
                if (!(flags & KC_KEYUP)) {
                    mask = Mod3Mask;
                    prevState = ((state & mask) ^ prevState) ? 0 : 1;
                }
                break;
        }
        if (prevState) {
            state |= mask;
        } else {
            state &= ~mask;
        }
    }
    return state;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreeXId --
 *
 *      This inteface is not needed under OS/2.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreeXId(display, xid)
    Display *display;
    XID xid;
{
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ResendEvent --
 *
 *      This function converts an X event into a PM event and
 *      invokes the specified window procedure.
 *
 * Results:
 *      A standard OS/2 PM result.
 *
 * Side effects:
 *      Invokes the window procedure
 *
 *----------------------------------------------------------------------
 */

MRESULT
TkOS2ResendEvent(wndproc, hwnd, eventPtr)
    PFNWP wndproc;
    HWND hwnd;
    XEvent *eventPtr;
{
    ULONG msg;
    MPARAM param1;
    MPARAM param2;
#ifdef VERBOSE
    printf("TkOS2ResendEvent (%d,%d)\n", eventPtr->xbutton.x,
           eventPtr->xbutton.y);
#endif

    if (eventPtr->type == ButtonPress) {
        switch (eventPtr->xbutton.button) {
            case Button1:
                msg = WM_BUTTON1DOWN;
                param2 = (MPARAM) VK_BUTTON1;
                break;
            case Button2:
                msg = WM_BUTTON3DOWN;
                param2 = (MPARAM) VK_BUTTON3;
                break;
            case Button3:
                msg = WM_BUTTON2DOWN;
                param2 = (MPARAM) VK_BUTTON2;
                break;
            default:
                return 0;
        }
        if (eventPtr->xbutton.state & Button1Mask) {
            param2 = (MPARAM) (((LONG) param2) | VK_BUTTON1);
        }
        if (eventPtr->xbutton.state & Button2Mask) {
            param2 = (MPARAM) (((LONG) param2) | VK_BUTTON3);
        }
        if (eventPtr->xbutton.state & Button3Mask) {
            param2 = (MPARAM) (((LONG) param2) | VK_BUTTON2);
        }
        if (eventPtr->xbutton.state & ShiftMask) {
            param2 = (MPARAM) (((LONG) param2) | VK_SHIFT);
        }
        if (eventPtr->xbutton.state & ControlMask) {
            param2 = (MPARAM) (((LONG) param2) | VK_CTRL);
        }
        param1 = MPFROM2SHORT((SHORT) eventPtr->xbutton.x,
                              (SHORT) TkOS2TranslateY(hwnd,
                                                      eventPtr->xbutton.y, 0));
    } else {
        return 0;
    }
    return (*wndproc)(hwnd, msg, param1, param2);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetMS --
 *
 *      Return a relative time in milliseconds.  It doesn't matter
 *      when the epoch was.
 *
 * Results:
 *      Number of milliseconds.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

unsigned long
TkpGetMS()
{
#ifdef VERBOSE
    printf("TkpGetMS\n");
#endif
    return WinGetCurrentTime(TclOS2GetHAB());
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2UpdatingClipboard --
 *
*       Set a flag to show whether or not the clipboard is being updated.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2UpdatingClipboard(int mode)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    tsdPtr->updatingClipboard = mode;
}

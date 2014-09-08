/* 
 * tkOS2Window.c --
 *
 *	Xlib emulation routines for OS/2 Presentation Manager related to
 *	creating, displaying and destroying windows.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"

typedef struct ThreadSpecificData {
    int initialized;            /* 0 means table below needs initializing. */
    Tcl_HashTable windowTable;  /* The windowTable maps from HWND to
                                 * Tk_Window handles. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Forward declarations for procedures defined in this file:
 */

static void             NotifyVisibility _ANSI_ARGS_((XEvent *eventPtr,
                            TkWindow *winPtr));

/*
 *----------------------------------------------------------------------
 *
 * Tk_AttachHWND --
 *
 *      This function binds an HWND and a reflection procedure to
 *      the specified Tk_Window.
 *
 * Results:
 *      Returns an X Window that encapsulates the HWND.
 *
 * Side effects:
 *      May allocate a new X Window.  Also enters the HWND into the
 *      global window table.
 *
 *----------------------------------------------------------------------
 */

Window
Tk_AttachHWND(tkwin, hwnd)
    Tk_Window tkwin;
    HWND hwnd;
{
    int new;
    Tcl_HashEntry *entryPtr;
    TkOS2Drawable *todPtr = (TkOS2Drawable *) Tk_WindowId(tkwin);
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("Tk_AttachHWND tkwin %x, hwnd %x\n", tkwin, hwnd);
#endif

    if (!tsdPtr->initialized) {
        Tcl_InitHashTable(&tsdPtr->windowTable, TCL_ONE_WORD_KEYS);
        tsdPtr->initialized = 1;
    }

    /*
     * Allocate a new drawable if necessary.  Otherwise, remove the
     * previous HWND from the window table.
     */

    if (todPtr == NULL) {
        todPtr = (TkOS2Drawable*) ckalloc(sizeof(TkOS2Drawable));
#ifdef VERBOSE
        printf("    new todPtr (drawable) %x\n", todPtr);
#endif
        todPtr->type = TOD_WINDOW;
        todPtr->window.winPtr = (TkWindow *) tkwin;
    } else if (todPtr->window.handle != NULLHANDLE) {
        entryPtr = Tcl_FindHashEntry(&tsdPtr->windowTable,
                (char *)todPtr->window.handle);
        Tcl_DeleteHashEntry(entryPtr);
    }

    /*
     * Insert the new HWND into the window table.
     */

    todPtr->window.handle = hwnd;
    entryPtr = Tcl_CreateHashEntry(&tsdPtr->windowTable, (char *)hwnd, &new);
#ifdef VERBOSE
    printf("inserting hwnd %x (tkwin %x) into windowTable, entryPtr %x\n", hwnd,
           tkwin, entryPtr);
#endif
    Tcl_SetHashValue(entryPtr, (ClientData)tkwin);

    return (Window)todPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_HWNDToWindow --
 *
 *      This function retrieves a Tk_Window from the window table
 *      given an HWND.
 *
 * Results:
 *      Returns the matching Tk_Window.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_HWNDToWindow(hwnd)
    HWND hwnd;
{
    Tcl_HashEntry *entryPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (!tsdPtr->initialized) {
        Tcl_InitHashTable(&tsdPtr->windowTable, TCL_ONE_WORD_KEYS);
        tsdPtr->initialized = 1;
    }
    entryPtr = Tcl_FindHashEntry(&tsdPtr->windowTable, (char*)hwnd);
    if (entryPtr != NULL) {
#ifdef VERBOSE
    printf("Tk_HWNDToWindow hwnd %x => %x\n", hwnd, Tcl_GetHashValue(entryPtr));
#endif
        return (Tk_Window) Tcl_GetHashValue(entryPtr);
    }
#ifdef VERBOSE
    printf("Tk_HWNDToWindow hwnd %x => NULL\n", hwnd);
#endif
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetHWND --
 *
 *      This function extracts the HWND from an X Window.
 *
 * Results:
 *      Returns the HWND associated with the Window.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

HWND
Tk_GetHWND(window)
    Window window;
{
#ifdef VERBOSE
    printf("Tk_GetHWND window %x => hwnd %x\n", window,
           ((TkOS2Drawable *) window)->window.handle);
#endif
    return ((TkOS2Drawable *) window)->window.handle;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpPrintWindowId --
 *
 *      This routine stores the string representation of the
 *      platform dependent window handle for an X Window in the
 *      given buffer.
 *
 * Results:
 *      Returns the result in the specified buffer.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkpPrintWindowId(buf, window)
    char *buf;                  /* Pointer to string large enough to hold
                                 * the hex representation of a pointer. */
    Window window;              /* Window to be printed into buffer. */
{
    HWND hwnd = (window) ? Tk_GetHWND(window) : 0;
#ifdef VERBOSE
    printf("TkpPrintWindowID window %x => 0x%x\n", window, hwnd);
#endif
    sprintf(buf, "0x%x", (unsigned int) hwnd);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpScanWindowId --
 *
 *      Given a string which represents the platform dependent window
 *      handle, produce the X Window id for the window.
 *
 * Results:
 *      The return value is normally TCL_OK;  in this case *idPtr
 *      will be set to the X Window id equivalent to string.  If
 *      string is improperly formed then TCL_ERROR is returned and
 *      an error message will be left in the interp's result.  If the
 *      number does not correspond to a Tk Window, then *idPtr will
 *      be set to None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TkpScanWindowId(interp, string, idPtr)
    Tcl_Interp *interp;         /* Interpreter to use for error reporting. */
    CONST char *string;         /* String containing a (possibly signed)
                                 * integer in a form acceptable to strtol. */
    Window *idPtr;              /* Place to store converted result. */
{
    Tk_Window tkwin;
    Window number;

#ifdef VERBOSE
    printf("TkpScanWindowId [%s]\n", string);
#endif
    if (Tcl_GetInt(interp, (char *) string, (int *)&number) != TCL_OK) {
        return TCL_ERROR;
    }

    tkwin = Tk_HWNDToWindow((HWND)number);
#ifdef VERBOSE
    printf("Tk_HWNDToWindow(%x) tkwin %x\n", number, tkwin);
#endif
    if (tkwin) {
        *idPtr = Tk_WindowId(tkwin);
    } else {
        *idPtr = None;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMakeWindow --
 *
 *	Creates an OS/2 PM window object based on the current attributes
 *	of the specified TkWindow.
 *
 * Results:
 *	Returns a pointer to a new TkOS2Drawable cast to a Window.
 *
 * Side effects:
 *	Creates a new window.
 *
 *----------------------------------------------------------------------
 */

Window
TkpMakeWindow(winPtr, parent)
    TkWindow *winPtr;
    Window parent;
{
    HWND parentWin;
    LONG yPos;
    int style;
    HWND hwnd;
    
#ifdef VERBOSE
    printf("TkpMakeWindow winPtr %x, parent %x; (%d,%d) %dx%d\n", winPtr,
           parent, Tk_X(winPtr), Tk_Y(winPtr), Tk_Width(winPtr),
	   Tk_Height(winPtr));
    if (Tk_IsEmbedded(winPtr)) {
        printf("Embedded!\n");
    }
#endif

    /* Translate Y coordinates to PM */
    if (parent != None) {
        SWP parPos;
	parentWin = Tk_GetHWND(parent);
	style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        rc = WinQueryWindowPos(parentWin, &parPos);
        yPos = parPos.cy - Tk_Y(winPtr) - Tk_Height(winPtr);
    } else {
	parentWin = HWND_DESKTOP;
	style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        yPos = yScreen -  Tk_Y(winPtr) - Tk_Height(winPtr);
    }

    /*
     * Create the window, then ensure that it is at the top of the
     * stacking order.
     * Use FID_CLIENT in order to get activation right later!
     */
    hwnd = WinCreateWindow(parentWin, TOC_CHILD, "", style, Tk_X(winPtr),
                           yPos, Tk_Width(winPtr), Tk_Height(winPtr),
                           NULLHANDLE, HWND_TOP, FID_CLIENT, (PVOID)winPtr,
                           NULL);
#ifdef VERBOSE
    if (hwnd == NULLHANDLE) {
        printf("TkpMakeWindow: WinCreateWindow (parent %x) ERROR %x, thrd %d\n",
	       parentWin, WinGetLastError(TclOS2GetHAB()),
               (int)Tcl_GetCurrentThread());
    } else {
        printf("TkpMakeWindow: WinCreateWindow: %x (parent %x) (%d,%d) %dx%d\n",
               hwnd, parentWin, Tk_X(winPtr), yPos, Tk_Width(winPtr),
               Tk_Height(winPtr));
    }
#endif

    return Tk_AttachHWND((Tk_Window)winPtr, hwnd);
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyWindow --
 *
 *	Destroys the given window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sends the WM_DESTROY message to the window and then destroys
 *	the resources associated with the window.
 *
 *----------------------------------------------------------------------
 */

void
XDestroyWindow(display, w)
    Display* display;
    Window w;
{
    Tcl_HashEntry *entryPtr;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)w;
    TkWindow *winPtr = TkOS2GetWinPtr(w);
    HWND hwnd = Tk_GetHWND(w);
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("XDestroyWindow handle %x, winPtr->flags %x, winPtr %x\n", hwnd,
           winPtr->flags, winPtr);
#endif

    display->request++;

    /*
     * Remove references to the window in the pointer module then
     * release the drawable.
     */

    TkPointerDeadWindow(winPtr);

    entryPtr = Tcl_FindHashEntry(&tsdPtr->windowTable, (char*)hwnd);
    if (entryPtr != NULL) {
#ifdef VERBOSE
        printf("removing hwnd %x from windowTable\n", hwnd);
#endif
        Tcl_DeleteHashEntry(entryPtr);
    }

    ckfree((char *)todPtr);

    /*
     * Don't bother destroying the window if we are going to destroy
     * the parent later.
     * Due to difference in destroying order, this function can be
     * called for an already destroyed hwnd, so check that.
     */

    if (hwnd != NULLHANDLE && !(winPtr->flags & TK_DONT_DESTROY_WINDOW)
        && WinIsWindow(TclOS2GetHAB(), hwnd)) {
        rc = WinDestroyWindow(hwnd);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("WinDestroyWindow hwnd %x ERROR %x\n", hwnd,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("WinDestroyWindow hwnd %x OK\n", hwnd);
        }
#endif
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XMapWindow --
 *
 *	Cause the given window to become visible.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Causes the window state to change, and generates a MapNotify
 *	event.
 *
 *----------------------------------------------------------------------
 */

void
XMapWindow(display, w)
    Display* display;
    Window w;
{
    XEvent event;
    TkWindow *parentPtr;
    TkWindow *winPtr = TkOS2GetWinPtr(w);

    display->request++;

    rc = WinShowWindow(Tk_GetHWND(w), TRUE);
#ifdef VERBOSE
    if (rc != TRUE) {
        printf("XMapWindow: WinShowWindow %x ERROR %x\n", Tk_GetHWND(w),
	       WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("XMapWindow: WinShowWindow %x OK\n", Tk_GetHWND(w));
    }
#endif
    winPtr->flags |= TK_MAPPED;

   /*
    * Check to see if this window is visible now.  If all of the parent
    * windows up to the first toplevel are mapped, then this window and
    * its mapped children have just become visible.
    */

   if (!(winPtr->flags & TK_TOP_LEVEL)) {
       for (parentPtr = winPtr->parentPtr; ;
               parentPtr = parentPtr->parentPtr) {
           if ((parentPtr == NULL) || !(parentPtr->flags & TK_MAPPED)) {
               return;
           }
           if (parentPtr->flags & TK_TOP_LEVEL) {
               break;
           }
       }
    } else {
        event.type = MapNotify;
        event.xmap.serial = display->request;
        event.xmap.send_event = False;
        event.xmap.display = display;
        event.xmap.event = winPtr->window;
        event.xmap.window = winPtr->window;
        event.xmap.override_redirect = winPtr->atts.override_redirect;
#ifdef VERBOSE
        printf("MapNotify\n");
#endif
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        display->request++;
    }

    /*
     * Generate VisibilityNotify events for this window and its mapped
     * children.
     */

    event.type = VisibilityNotify;
    event.xvisibility.serial = display->request;
    event.xvisibility.send_event = False;
    event.xvisibility.display = display;
    event.xvisibility.window = winPtr->window;
    event.xvisibility.state = VisibilityUnobscured;
    NotifyVisibility(&event, winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * NotifyVisibility --
 *
 *      This function recursively notifies the mapped children of the
 *      specified window of a change in visibility. A VisibilityNotify
 *	event is generated for each child that returns TRUE for
 *	WinIsWindowShowing(), with the state flag set to
 *	VisibilityUnobscured. No account is taken of the previous state
 *	or the extent of viewabilit/obscuredness, since that would cost
 *	much computation (eg. WinQueryUpdateRect) and memory (field for
 *	last viewability).
 *      The eventPtr argument must point to an event
 *      that has been completely initialized except for the window slot.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates lots of events.
 *
 *----------------------------------------------------------------------
 */

static void
NotifyVisibility(eventPtr, winPtr)
    XEvent *eventPtr;           /* Initialized VisibilityNotify event. */
    TkWindow *winPtr;           /* Window to notify. */
{
#ifdef VERBOSE
    printf("NotifyVisibility\n");
#endif
    if (winPtr->atts.event_mask & VisibilityChangeMask) {
        eventPtr->xvisibility.window = winPtr->window;
        Tk_QueueWindowEvent(eventPtr, TCL_QUEUE_TAIL);
    }
    for (winPtr = winPtr->childList; winPtr != NULL;
            winPtr = winPtr->nextPtr) {
        if (winPtr->flags & TK_MAPPED) {
            NotifyVisibility(eventPtr, winPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XUnmapWindow --
 *
 *	Cause the given window to become invisible.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Causes the window state to change, and generates an UnmapNotify
 *	event.
 *
 *----------------------------------------------------------------------
 */

void
XUnmapWindow(display, w)
    Display* display;
    Window w;
{
    XEvent event;
    TkWindow *winPtr = TkOS2GetWinPtr(w);
#ifdef VERBOSE
    printf("XUnmapWindow hwnd %x\n", Tk_GetHWND(w));
#endif

    display->request++;

    /*
     * Bug fix: Don't short circuit this routine based on TK_MAPPED because
     * it will be cleared before XUnmapWindow is called.
     */

    WinShowWindow(Tk_GetHWND(w), FALSE);
    winPtr->flags &= ~TK_MAPPED;

    if (winPtr->flags & TK_TOP_LEVEL) {
        event.type = UnmapNotify;
        event.xunmap.serial = display->request;
        event.xunmap.send_event = False;
#ifdef VERBOSE
        printf("    display %x\n", display);
#endif
        event.xunmap.display = display;
        event.xunmap.event = winPtr->window;
        event.xunmap.window = winPtr->window;
        event.xunmap.from_configure = False;
#ifdef VERBOSE
        printf("UnmapNotify\n");
#endif
        Tk_HandleEvent(&event);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XMoveResizeWindow --
 *
 *	Move and resize a window relative to its parent.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Repositions and resizes the specified window.
 *
 *----------------------------------------------------------------------
 */

void
XMoveResizeWindow(display, w, x, y, width, height)
    Display* display;
    Window w;
    int x;			/* Position relative to parent. */
    int y;
    unsigned int width;
    unsigned int height;
{
#ifdef VERBOSE
    SWP pos;
    WinQueryWindowPos(Tk_GetHWND(w), &pos);
    printf("XMoveResizeWindow %x (%d,%d) %dx%d PM %d, oldpos PM(%d,%d) %dx%d\n",
           Tk_GetHWND(w), x, y, width, height,
           TkOS2TranslateY(Tk_GetHWND(w),y,height), pos.x, pos.y, pos.cx,
           pos.cy);
#endif
    display->request++;
    /*
     * Translate Y coordinates to PM: relative to parent
     */
    WinSetWindowPos(Tk_GetHWND(w), HWND_TOP, x,
                    TkOS2TranslateY(Tk_GetHWND(w), y, height),
                    width, height, SWP_MOVE | SWP_SIZE);
#ifdef VERBOSE
    printf("XMoveResizeWindow hwnd %x, (%d,%d) %dx%d (x11y %d)\n",
           Tk_GetHWND(w), x,
           TkOS2TranslateY(Tk_GetHWND(w), y, height), width, height, y);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * XMoveWindow --
 *
 *	Move a window relative to its parent.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Repositions the specified window.
 *
 *----------------------------------------------------------------------
 */

void
XMoveWindow(display, w, x, y)
    Display* display;
    Window w;
    int x;
    int y;
{
    TkWindow *winPtr = TkOS2GetWinPtr(w);
#ifdef VERBOSE
    SWP pos;
    WinQueryWindowPos(Tk_GetHWND(w), &pos);
    printf("XMoveWindow %x, oldpos (%d,%d;%dx%d)\n", Tk_GetHWND(w),
           pos.x, pos.y, pos.cx, pos.cy);
#endif

    display->request++;

    /* Translate Y coordinates to PM, relative to parent */
    WinSetWindowPos(Tk_GetHWND(w), HWND_TOP, x,
                    TkOS2TranslateY(Tk_GetHWND(w), y, winPtr->changes.height),
                    winPtr->changes.width, winPtr->changes.height,
                    SWP_MOVE /*| SWP_SIZE*/ | SWP_NOADJUST);
#ifdef VERBOSE
    printf("XMoveWindow hwnd %x, x %d, y %d, w %d, h %d\n", Tk_GetHWND(w),
           x, TkOS2TranslateY(Tk_GetHWND(w), y, winPtr->changes.height),
           winPtr->changes.width, winPtr->changes.height);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * XResizeWindow --
 *
 *	Resize a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resizes the specified window.
 *
 *----------------------------------------------------------------------
 */

void
XResizeWindow(display, w, width, height)
    Display* display;
    Window w;
    unsigned int width;
    unsigned int height;
{
/*
    TkWindow *winPtr = TkOS2GetWinPtr(w);
*/
    SWP oldPos;
    WinQueryWindowPos(Tk_GetHWND(w), &oldPos);
#ifdef VERBOSE
    printf("XResizeWindow %x, oldpos (%d,%d;%dx%d)\n", Tk_GetHWND(w),
           oldPos.x, oldPos.y, oldPos.cx, oldPos.cy);
#endif

    display->request++;

    /*
     * Translate Y coordinates to PM; relative to parent
     * The *top* must stay at the same position, so use SWP_MOVE too.
     */
    WinSetWindowPos(Tk_GetHWND(w), HWND_TOP, oldPos.x,
                    oldPos.y - height, width, height,
                    SWP_MOVE | SWP_SIZE | SWP_NOADJUST);
#ifdef VERBOSE
    printf("XResizeWindow hwnd %x, x %d, y %d, w %d, h %d\n", Tk_GetHWND(w),
           oldPos.y + oldPos.cy - height, width, height);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * XRaiseWindow --
 *
 *	Change the stacking order of a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the stacking order of the specified window.
 *
 *----------------------------------------------------------------------
 */

void
XRaiseWindow(display, w)
    Display* display;
    Window w;
{
    HWND window = Tk_GetHWND(w);

#ifdef VERBOSE
    printf("XRaiseWindow hwnd %x\n", window);
#endif

    display->request++;
    rc = WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
#ifdef VERBOSE
    if (rc!=TRUE) {
        printf("    WinSetWindowPos HWND_TOP ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("    WinSetWindowPos HWND_TOP OK\n");
    }
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * XConfigureWindow --
 *
 *	Change the size, position, stacking, or border of the specified
 *	window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the attributes of the specified window.  Note that we
 *	ignore the passed in values and use the values stored in the
 *	TkWindow data structure.
 *
 *----------------------------------------------------------------------
 */

void
XConfigureWindow(display, w, value_mask, values)
    Display* display;
    Window w;
    unsigned int value_mask;
    XWindowChanges* values;
{
    TkWindow *winPtr = TkOS2GetWinPtr(w);
    HWND hwnd = Tk_GetHWND(w);

#ifdef VERBOSE
    SWP pos;
    WinQueryWindowPos(hwnd, &pos);
    printf("XConfigureWindow %x, pos (%d,%d;%dx%d)\n", hwnd, pos.x, pos.y,
           pos.cx, pos.cy);
#endif

    display->request++;

    /*
     * Change the shape and/or position of the window.
     */

    if (value_mask & (CWX|CWY|CWWidth|CWHeight)) {
        /* Translate Y coordinates to PM */
        WinSetWindowPos(hwnd, HWND_TOP, winPtr->changes.x,
                        TkOS2TranslateY(hwnd, winPtr->changes.y,
                                        winPtr->changes.height),
                        winPtr->changes.width, winPtr->changes.height,
                        SWP_MOVE | SWP_SIZE | SWP_NOADJUST);
#ifdef VERBOSE
        printf("    WinSetWindowPos CWX/CWY   hwnd %x, (%d,%d) %dx%d\n", hwnd,
               winPtr->changes.x,
               TkOS2TranslateY(hwnd, winPtr->changes.y, winPtr->changes.height),
               winPtr->changes.width, winPtr->changes.height);
#endif
    }

    /*
     * Change the stacking order of the window.
     */

    if (value_mask & CWStackMode) {
	HWND sibling;
#ifdef VERBOSE
        printf("    CWStackMode\n");
#endif
	if ((value_mask & CWSibling) && (values->sibling != None)) {
	    sibling = Tk_GetHWND(values->sibling);
	} else {
	    sibling = NULLHANDLE;
	}
	TkOS2SetWindowPos(hwnd, sibling, values->stack_mode);
    } 
#ifdef VERBOSE
    WinQueryWindowPos(hwnd, &pos);
    printf("After XConfigureWindow %x, pos (%d,%d;%dx%d)\n", hwnd,
           pos.x, pos.y, pos.cx, pos.cy);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * XClearWindow --
 *
 *	Clears the entire window to the current background color.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Erases the current contents of the window.
 *
 *----------------------------------------------------------------------
 */

void
XClearWindow(display, w)
    Display* display;
    Window w;
{
    RECTL rect;
    LONG oldColor, oldPattern;
    HPAL oldPalette, palette;
    TkWindow *winPtr;
    HWND hwnd = Tk_GetHWND(w);
    HPS hps = WinGetPS(hwnd);

#ifdef VERBOSE
    printf("XClearWindow\n");
#endif

    palette = TkOS2GetPalette(display->screens[0].cmap);
    oldPalette = GpiSelectPalette(hps, palette);

    display->request++;

    winPtr = TkOS2GetWinPtr(w);
    oldColor = GpiQueryColor(hps);
    oldPattern = GpiQueryPattern(hps);
    GpiSetPattern(hps, PATSYM_SOLID);
    WinQueryWindowRect(hwnd, &rect);
    WinFillRect(hps, &rect, winPtr->atts.background_pixel);
#ifdef VERBOSE
    printf("WinFillRect in XClearWindow\n");
#endif
    GpiSetPattern(hps, oldPattern);
    GpiSelectPalette(hps, oldPalette);
    WinReleasePS(hps);
}

/*
 *----------------------------------------------------------------------
 *
 * XChangeWindowAttributes --
 *
 *      This function is called when the attributes on a window are
 *      updated.  Since Tk maintains all of the window state, the only
 *      relevant value is the cursor.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May cause the mouse position to be updated.
 *
 *----------------------------------------------------------------------
 */

void
XChangeWindowAttributes(display, w, valueMask, attributes)
    Display* display;
    Window w;
    unsigned long valueMask;
    XSetWindowAttributes* attributes;
{
#ifdef VERBOSE
    printf("XChangeWindowAttributes\n");
#endif
    if (valueMask & CWCursor) {
        XDefineCursor(display, w, attributes->cursor);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2SetWindowPos --
 *
 *      Adjust the stacking order of a window relative to a second
 *      window (or NULLHANDLE).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Moves the specified window in the stacking order.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2SetWindowPos(hwnd, siblingHwnd, pos)
    HWND hwnd;                  /* Window to restack. */
    HWND siblingHwnd;           /* Sibling window. */
    int pos;                    /* One of Above or Below. */
{
    HWND temp;

#ifdef VERBOSE
    printf("TkOS2SetWindowPos hwnd %x sibling %x pos %s\n", hwnd, siblingHwnd,
           pos == Above ? "Above" : "Below");
#endif

    /*
     * Since OS/2 does not support Above mode, we place the
     * specified window below the sibling and then swap them.
     */

    if (siblingHwnd != NULLHANDLE) {
        if (pos == Above) {
            WinSetWindowPos(hwnd, siblingHwnd, 0, 0, 0, 0, SWP_ZORDER);
            temp = hwnd;
            hwnd = siblingHwnd;
            siblingHwnd = temp;
        }
    } else {
        siblingHwnd = (pos == Above) ? HWND_TOP : HWND_BOTTOM;
    }

    WinSetWindowPos(hwnd, siblingHwnd, 0, 0, 0, 0, SWP_ZORDER);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWindowWasRecentlyDeleted --
 *
 *      Determines whether we know if the window given as argument was
 *      recently deleted. Called by the generic code error handler to
 *      handle BadWindow events.
 *
 * Results:
 *      Always 0. We do not keep this information on OS/2.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TkpWindowWasRecentlyDeleted(win, dispPtr)
    Window win;
    TkDisplay *dispPtr;
{
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2WindowHeight --
 *
 *      Determine the height of an OS/2 drawable (or parent for bitmaps).
 *
 * Results:
 *      Height of drawable.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

LONG
TkOS2WindowHeight(todPtr)
    TkOS2Drawable *todPtr;
{
    SWP pos;
    BOOL rc;

#ifdef VERBOSE
    printf("TkOS2WindowHeight todPtr %x\n", todPtr);
#endif

    if (todPtr->type == TOD_BITMAP) {
        BITMAPINFOHEADER2 info;
        /* Bitmap */
        info.cbFix = sizeof(BITMAPINFOHEADER2);
        rc = GpiQueryBitmapInfoHeader(todPtr->bitmap.handle, &info);
#ifdef VERBOSE
        printf("TkOS2WindowHeight todPtr %x (bitmap %x) returning %d\n", todPtr,
	       todPtr->bitmap.handle, info.cy);
#endif
        return info.cy;
    } else if (todPtr->type == TOD_OS2PS) {
#ifdef VERBOSE
        printf("TkOS2WindowHeight todPtr %x hps %x hwnd %x\n",
               todPtr, todPtr->os2PS.hps, todPtr->os2PS.hwnd);
#endif
        rc = WinQueryWindowPos(todPtr->os2PS.hwnd, &pos);
        if (rc != TRUE) {
#ifdef VERBOSE
            printf("    WinQueryWindowPos ERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
#endif
            return 0;
        }
#ifdef VERBOSE
        printf("TkOS2WindowHeight hwnd %x (os2PS) returning %d\n",
               todPtr->os2PS.hwnd, pos.cy);
#endif
        return pos.cy;
    }
    return TkOS2HwndHeight(todPtr->window.handle);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2HwndHeight --
 *
 *      Determine the height of a window.
 *
 * Results:
 *      Height of drawable.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

LONG
TkOS2HwndHeight(hwnd)
    HWND hwnd;
{
    SWP pos;
    HWND parent;
    HWND desktop;
    BOOL rc;

#ifdef VERBOSE
    printf("TkOS2HwndHeight hwnd %x\n", hwnd);
#endif

    rc = WinQueryWindowPos(hwnd, &pos);
    if (rc != TRUE) {
#ifdef VERBOSE
        printf(" WinQueryWindowPos hwnd %x ERROR %x\n", hwnd,
               WinGetLastError(TclOS2GetHAB()));
#endif
        return 0;
    }
#ifdef VERBOSE
    printf("    pos hwnd %x (%d,%d) %dx%d\n", hwnd, pos.x, pos.y, pos.cx,
           pos.cy);
#endif

    /* Take toplevel frames' decorations (borders etc.) into account */
    desktop = WinQueryDesktopWindow(TclOS2GetHAB(), NULLHANDLE);
    parent = WinQueryWindow(hwnd, QW_PARENT);
#ifdef VERBOSE
    printf("    parent %x, desktop %x\n", parent, desktop);
#endif
    if (hwnd != desktop && parent == desktop) {
	RECTL rectl;
        rectl.xLeft = pos.x;
        rectl.xRight = pos.x + pos.cx;
        rectl.yBottom = pos.y;
	rectl.yTop = pos.y + pos.cy;
#ifdef VERBOSE
        printf("    rectl before WinCalcFrameRect (%d,%d) (%d,%d)\n",
	       rectl.xLeft, rectl.yBottom, rectl.xRight, rectl.yTop);
#endif
	rc = WinCalcFrameRect(hwnd, &rectl, TRUE);
#ifdef VERBOSE
        printf("    rectl after WinCalcFrameRect (%d,%d) (%d,%d)\n",
	       rectl.xLeft, rectl.yBottom, rectl.xRight, rectl.yTop);
#endif
        if (rc != TRUE) {
#ifdef VERBOSE
            printf("TkOS2HwndHeight: WinCalcFrameRect hwnd %x ERROR %x\n",
                   hwnd, WinGetLastError(TclOS2GetHAB()));
#endif
            return pos.cy;
        }
#ifdef VERBOSE
        printf("TkOS2HwndHeight hwnd %x (frame) returning %d\n", hwnd,
	       rectl.yTop - rectl.yBottom);
#endif
        return rectl.yTop - rectl.yBottom;
    } else {
#ifdef VERBOSE
        printf("TkOS2HwndHeight hwnd %x (parent %x) returning %d\n",
               hwnd, parent, pos.cy);
#endif
        return pos.cy;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2TranslateY --
 *
 *      Translate PM y coordinate (from bottom of screen) into X Window
 *      System y coordinate (from top of screen) or the other way around.
 *      The height argument is needed for the window position of a window,
 *      it should be 0 for translating just a coordinate instead of a
 *      window position.
 *      The y position of a window in PM coordinates is the height of the
 *      parent window minus the y position in X coordinates and the height
 *      of the window (and vice versa).
 *      Since X Window System coordinates are always ints, use that instead
 *      of LONG.
 *
 * Results:
 *      Translated y coordinate.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TkOS2TranslateY(hwnd, y, height)
    HWND hwnd;	/* Window for which the translation is meant */
    int y;	/* y coordinate to be translated */
    int height; /*  height the window is going to have, 0 for position */
{
    LONG parHeight = TkOS2HwndHeight(WinQueryWindow(hwnd, QW_PARENT));
#ifdef VERBOSE
    printf("TkOS2TranslateY hwnd %x y %d height %d (%d-%d-%d = %d)\n", hwnd, y,
           height, parHeight, y, height, parHeight - y - height);
#endif

    return (int) parHeight - y - height;
}

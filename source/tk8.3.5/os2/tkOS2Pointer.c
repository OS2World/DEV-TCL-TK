/* 
 * tkOS2Pointer.c --
 *
 *	OS/2 PM specific mouse tracking code.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 by Scriptics Corporation.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"

/*
 * Check for enter/leave events every MOUSE_TIMER_INTERVAL milliseconds.
 */

#define MOUSE_TIMER_INTERVAL 250

/*
 * Declarations of static variables used in this file.
 */

static int captured = 0;		/* 1 if mouse is currently captured. */
static TkWindow *keyboardWinPtr = NULL;	/* Current keyboard grab window. */
static Tcl_TimerToken mouseTimer;       /* Handle to the latest mouse timer. */
static int mouseTimerSet = 0;           /* 1 if the mouse timer is active. */
static POINTL prevMousePos = {0,0};	/* previous position of the mouse. */

/*
 * Forward declarations of procedures used in this file.
 */

static void		MouseTimerProc (ClientData clientData);

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetModifierState --
 *
 *      Return the modifier state as of the last message.
 *
 * Results:
 *      Returns the X modifier mask.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TkOS2GetModifierState()
{
    int state = 0;

    if (WinGetKeyState(HWND_DESKTOP, VK_SHIFT) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState ShiftMask\n");
#endif
        state |= ShiftMask;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_CTRL) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState ControlMask\n");
#endif
        state |= ControlMask;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_MENU) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState VK_MENU ALT_MASK\n");
#endif
        state |= ALT_MASK;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_ALT) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState VK_ALT ALT_MASK\n");
#endif
        state |= ALT_MASK;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_ALTGRAF) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState VK_ALTGRAF ALT_MASK\n");
#endif
        state |= ALT_MASK;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_CAPSLOCK) & 0x0001) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState LockMask\n");
#endif
        state |= LockMask;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_NUMLOCK) & 0x0001) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState Mod1Mask\n");
#endif
        state |= Mod1Mask;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_SCRLLOCK) & 0x0001) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState Mod3Mask\n");
#endif
        state |= Mod3Mask;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON1) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState Button1Mask\n");
#endif
        state |= Button1Mask;
    }
    /* OS/2 and X buttons 2 and 3 are reversed, 3 not necessarily available */
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON3) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState Button2Mask\n");
#endif
        state |= Button2Mask;
    }
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON2) & 0x8000) {
#ifdef VERBOSE
        printf("TkOS2GetModifierState Button3Mask\n");
#endif
        state |= Button3Mask;
    }
#ifdef VERBOSE
    printf("TkOS2GetModifierState: %x\n", state);
#endif
    return state;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PointerEvent --
 *
 *      This procedure is called for each pointer-related event.
 *      It converts the position to root coords and updates the
 *      global pointer state machine.  It also ensures that the
 *      mouse timer is scheduled.
 *      NB: PM y coordinates!
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May queue events and change the grab state.
 *
 *----------------------------------------------------------------------
 */

void
Tk_PointerEvent(hwnd, x, y)
    HWND hwnd;                          /* Window for coords, or NULLHANDLE for
                                         * the root window. */
    int x, y;                           /* Coords relative to hwnd, or screen
                                         * if hwnd is NULLHANDLE. */
{
    POINTL pos;
    int state;
    Tk_Window tkwin;

#ifdef VERBOSE
    printf("Tk_PointerEvent hwnd %x (%hd,%hd)\n", hwnd, x, y);
    fflush(stdout);
#endif
    pos.x = (LONG) x;
    pos.y = (LONG) y;

    /*
     * Convert client coords to root coords if we were given a window.
     */

    if (hwnd != NULLHANDLE) {
        WinMapWindowPoints(hwnd, HWND_DESKTOP, &pos, 1);
#ifdef VERBOSE
        printf("mapped => (%hd,%hd)\n", pos.x, pos.y);
        fflush(stdout);
#endif
    }

    /*
     * If the mouse is captured, OS/2 will report all pointer
     * events to the capture window.  So, we need to determine which
     * window the mouse is really over and change the event.  Note
     * that the computed hwnd may point to a window not owned by Tk,
     * or a toplevel decorative frame, so tkwin can be NULL.
     */

    if (captured || hwnd == NULLHANDLE) {
#ifdef VERBOSE
        printf("Tk_PointerEvent captured, hwnd %x => \n", hwnd);
        fflush(stdout);
#endif
        hwnd = WinWindowFromPoint(HWND_DESKTOP, &pos, TRUE);
#ifdef VERBOSE
        printf("Tk_PointerEvent captured => %x\n", hwnd);
        fflush(stdout);
#endif
    }
    tkwin = Tk_HWNDToWindow(hwnd);

    state = TkOS2GetModifierState();
#ifdef VERBOSE
    printf("Tk_PointerEvent => tkwin %x (%hd,%hd) x11y %hd st %x hwnd %x\n",
           tkwin, pos.x, pos.y, yScreen - pos.y, state, hwnd);
    fflush(stdout);
#endif

    /* Tk_UpdatePointer needs X Window System y coordinates */
#ifdef VERBOSE
    printf("calling Tk_UpdatePointer with %x, (%hd,%hd), %x\n", tkwin, pos.x,
           yScreen - pos.y, state);
    fflush(stdout);
    printf("winPtr->display %x (screens %x), winPtr->screenNum %d, screen %x\n",
           tkwin ? ((TkWindow *)tkwin)->display : 0,
           tkwin ? (((TkWindow *)tkwin)->display)->screens : 0,
           tkwin ? ((TkWindow *)tkwin)->screenNum : 0,
           tkwin && (((TkWindow *)tkwin)->display)->screens != NULL ?
        RootWindow(((TkWindow *)tkwin)->display, ((TkWindow *)tkwin)->screenNum)
        : 0L);
    fflush(stdout);
#endif
    Tk_UpdatePointer(tkwin, (int)pos.x, (int)(yScreen - pos.y), state);
#ifdef VERBOSE
    printf("After Tk_UpdatePointer\n");
#endif

    if ((captured || tkwin) && !mouseTimerSet) {
        mouseTimerSet = 1;
#ifdef VERBOSE
        printf("Creating mouseTimer %d\n", MOUSE_TIMER_INTERVAL);
#endif
        mouseTimer = Tcl_CreateTimerHandler(MOUSE_TIMER_INTERVAL,
                MouseTimerProc, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XGrabKeyboard --
 *
 *	Simulates a keyboard grab by setting the focus.
 *
 * Results:
 *	Always returns GrabSuccess.
 *
 * Side effects:
 *	Sets the keyboard focus to the specified window.
 *
 *----------------------------------------------------------------------
 */

int
XGrabKeyboard(display, grab_window, owner_events, pointer_mode,
	keyboard_mode, time)
    Display* display;
    Window grab_window;
    Bool owner_events;
    int pointer_mode;
    int keyboard_mode;
    Time time;
{
#ifdef VERBOSE
    printf("XGrabKeyboard\n");
#endif

    keyboardWinPtr = TkOS2GetWinPtr(grab_window);
    return GrabSuccess;
}

/*
 *----------------------------------------------------------------------
 *
 * XUngrabKeyboard --
 *
 *	Releases the simulated keyboard grab.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the keyboard focus back to the value before the grab.
 *
 *----------------------------------------------------------------------
 */

void
XUngrabKeyboard(display, time)
    Display* display;
    Time time;
{
#ifdef VERBOSE
    printf("XUngrabKeyboard\n");
#endif

    keyboardWinPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * MouseTimerProc --
 *
 *	Check the current mouse position and look for enter/leave 
 *	events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May schedule a new timer and/or generate enter/leave events.
 *
 *----------------------------------------------------------------------
 */

void
MouseTimerProc(clientData)
    ClientData clientData;
{
    POINTL pos;

    mouseTimerSet = 0;

    /*
     * Get the current mouse position and window.  Don't do anything
     * if the mouse hasn't moved since the last time we looked.
     */

    rc = WinQueryPointerPos(HWND_DESKTOP, &pos);

#ifdef VERBOSE
    printf("MouseTimerProc, WinQueryPointerPos returns %d (%d,%d)\n", rc,
           pos.x, pos.y);
#endif
    if (pos.x != prevMousePos.x || pos.y != prevMousePos.y) {
        /* Tk_PointerEvent handles PM Y coordinates */
        Tk_PointerEvent(NULLHANDLE, pos.x, pos.y);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetPointerCoords --
 *
 *	Fetch the position of the mouse pointer.
 *
 * Results:
 *	*xPtr and *yPtr are filled in with the root coordinates
 *	of the mouse pointer for the display.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkGetPointerCoords(tkwin, xPtr, yPtr)
    Tk_Window tkwin;		/* Window that identifies screen on which
				 * lookup is to be done. */
    int *xPtr, *yPtr;		/* Store pointer coordinates here. */
{
    POINTL pos;

#ifdef VERBOSE
    printf("TkGetPointerCoords\n");
#endif

    WinQueryPointerPos(HWND_DESKTOP, &pos);
    *xPtr = pos.x;
    /* Translate from PM to X coordinates */
    *yPtr = yScreen - pos.y;
}

/*
 *----------------------------------------------------------------------
 *
 * XQueryPointer --
 *
 *	Check the current state of the mouse.  This is not a complete
 *	implementation of this function.  It only computes the root
 *	coordinates and the current mask.
 *
 * Results:
 *	Sets root_x_return, root_y_return, and mask_return.  Returns
 *	true on success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
XQueryPointer(display, w, root_return, child_return, root_x_return,
	root_y_return, win_x_return, win_y_return, mask_return)
    Display* display;
    Window w;
    Window* root_return;
    Window* child_return;
    int* root_x_return;
    int* root_y_return;
    int* win_x_return;
    int* win_y_return;
    unsigned int* mask_return;
{
#ifdef VERBOSE
    printf("XQueryPointer\n");
#endif
    TkGetPointerCoords(NULL, root_x_return, root_y_return);
    *mask_return = TkOS2GetModifierState();    
    return True;
}

/*
 *----------------------------------------------------------------------
 *
 * XWarpPointer --
 *
 *      Move pointer to new location.  This is not a complete
 *      implementation of this function.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Mouse pointer changes position on screen.
 *
 *----------------------------------------------------------------------
 */

void
XWarpPointer(display, src_w, dest_w, src_x, src_y, src_width,
        src_height, dest_x, dest_y)
    Display* display;
    Window src_w;
    Window dest_w;
    int src_x;
    int src_y;
    unsigned int src_width;
    unsigned int src_height;
    int dest_x;
    int dest_y;
{
    RECTL rectl;
    HWND hwnd = Tk_GetHWND(dest_w);

    rc = WinQueryWindowRect(hwnd, &rectl);
    rc = WinMapWindowPoints(hwnd, HWND_DESKTOP, (PPOINTL)&rectl, 2);
    /* PM y pos is reversed! */
    WinSetPointerPos(HWND_DESKTOP, rectl.xLeft+dest_x, rectl.yBottom-dest_y);
}

/*
 *----------------------------------------------------------------------
 *
 * XGetInputFocus --
 *
 *	Retrieves the current keyboard focus window.
 *
 * Results:
 *	Returns the current focus window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
XGetInputFocus(display, focus_return, revert_to_return)
    Display *display;
    Window *focus_return;
    int *revert_to_return;
{
    Tk_Window tkwin = Tk_HWNDToWindow(WinQueryFocus(HWND_DESKTOP));

#ifdef VERBOSE
    printf("XGetInputFocus tkwin %x\n", tkwin);
#endif

    *focus_return = tkwin ? Tk_WindowId(tkwin) : None;
    *revert_to_return = RevertToParent;
    display->request++;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetInputFocus --
 *
 *	Set the current focus window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the keyboard focus and causes the selected window to
 *	be activated.
 *
 *----------------------------------------------------------------------
 */

void
XSetInputFocus(display, focus, revert_to, time)
    Display* display;
    Window focus;
    int revert_to;
    Time time;
{
#ifdef VERBOSE
    printf("XSetInputFocus\n");
#endif
    display->request++;
    if (focus != None) {
        WinSetFocus(HWND_DESKTOP, Tk_GetHWND(focus));
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpChangeFocus --
 *
 *      This procedure is invoked to move the system focus from
 *      one window to another.
 *
 * Results:
 *      The return value is the serial number of the command that
 *      changed the focus.  It may be needed by the caller to filter
 *      out focus change events that were queued before the command.
 *      If the procedure doesn't actually change the focus then
 *      it returns 0.
 *
 * Side effects:
 *      The official OS/2 focus window changes;  the application's focus
 *      window isn't changed by this procedure.
 *
 *----------------------------------------------------------------------
 */

int
TkpChangeFocus(winPtr, force)
    TkWindow *winPtr;           /* Window that is to receive the X focus. */
    int force;                  /* Non-zero means claim the focus even
                                 * if it didn't originally belong to
                                 * topLevelPtr's application. */
{
    TkDisplay *dispPtr = winPtr->dispPtr;
    Window focusWindow;
    int dummy, serial;
    TkWindow *winPtr2;

    if (!force) {
        XGetInputFocus(dispPtr->display, &focusWindow, &dummy);
        winPtr2 = (TkWindow *) Tk_IdToWindow(dispPtr->display, focusWindow);
        if ((winPtr2 == NULL) || (winPtr2->mainPtr != winPtr->mainPtr)) {
            return 0;
        }
    }

    if (winPtr->window == None) {
        panic("ChangeXFocus got null X window");
    }

    /*
     * Change the foreground window so the focus window is raised to the top of
     * the system stacking order and gets the keyboard focus.
     */

    if (force) {
        TkOS2SetForegroundWindow(winPtr);
    }
    XSetInputFocus(dispPtr->display, winPtr->window, RevertToParent,
            CurrentTime);

    /*
     * Remember the current serial number for the X server and issue
     * a dummy server request.  This marks the position at which we
     * changed the focus, so we can distinguish FocusIn and FocusOut
     * events on either side of the mark.
     */

    serial = NextRequest(winPtr->display);
    XNoOp(winPtr->display);
    return serial;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCapture --
 *
 *      This function captures the mouse so that all future events
 *      will be reported to this window, even if the mouse is outside
 *      the window.  If the specified window is NULL, then the mouse
 *      is released.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets the capture flag and captures the mouse.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetCapture(winPtr)
    TkWindow *winPtr;                   /* Capture window, or NULL. */
{
    if (winPtr) {
#ifdef VERBOSE
        printf("TkpSetCapture winPtr %x hwnd %x\n", winPtr,
               Tk_GetHWND(Tk_WindowId(winPtr)));
#endif
        WinSetCapture(HWND_DESKTOP, Tk_GetHWND(Tk_WindowId(winPtr)));
        captured = 1;
    } else {
        if (captured) {
            captured = 0;
#ifdef VERBOSE
            printf("TkpSetCapture winPtr %x NULLHANDLE\n", winPtr);
#endif
            WinSetCapture(HWND_DESKTOP, NULLHANDLE);
        }
    }
}

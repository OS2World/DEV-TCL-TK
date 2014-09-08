/* 
 * tkOS2Scrollbar.c --
 *
 *	This file implements the OS/2 specific portion of the scrollbar
 *	widget.
 *
 * Copyright (c) 1996 by Sun Microsystems, Inc.
 * Copyright (c) 1999-2003 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tkOS2Int.h"
#include "tkScrollbar.h"


/*
 * The following constant is used to specify the maximum scroll position.
 * This value is limited by the PM API to a SHORT, which is 2 bytes and
 * signed -> from -32768 through 32767.
 * However, for compatibility/uniformity of behaviour with the Windows
 * version, we use the same number here as the one there, which is chosen
 * as "a value small enough to fit in 16-bits, but which gives us 4-digits
 * of precision".
 */

#define MAX_SCROLL 10000

/*
 * Declaration of OS/2 specific scrollbar structure.
 */

typedef struct OS2Scrollbar {
    TkScrollbar info;		/* Generic scrollbar info. */
    PFNWP oldProc;		/* Old window procedure. */
    int lastVertical;		/* 1 if was vertical at last refresh. */
    HWND hwnd;			/* Current window handle. */
    int os2Flags;		/* Various flags; see below. */
} OS2Scrollbar;

/*
 * Flag bits for native scrollbars:
 * 
 * IN_MODAL_LOOP:		Non-zero means this scrollbar is in the middle
 *				of a modal loop.
 * ALREADY_DEAD:		Non-zero means this scrollbar has been
 *				destroyed, but has not been cleaned up.
 */

#define IN_MODAL_LOOP	1
#define ALREADY_DEAD	2

/*
 * Cached system metrics used to determine scrollbar geometry.
 */

static int initialized = 0;
static int hArrowWidth, hThumb; /* Horizontal control metrics. */
static int vArrowWidth, vArrowHeight, vThumb; /* Vertical control metrics. */

TCL_DECLARE_MUTEX(os2ScrlbrMutex)

/*
 * This variable holds the default width for a scrollbar in string
 * form for use in a Tk_ConfigSpec.
 */

static char defWidth[TCL_INTEGER_SPACE];

/*
 * Declarations for functions defined in this file.
 */

static Window		CreateProc _ANSI_ARGS_((Tk_Window tkwin,
			    Window parent, ClientData instanceData));
static void		ModalLoopProc _ANSI_ARGS_((Tk_Window tkwin,
			    XEvent *eventPtr));
static int		ScrollbarBindProc _ANSI_ARGS_((ClientData clientData,
			    Tcl_Interp *interp, XEvent *eventPtr,
			    Tk_Window tkwin, KeySym keySym));
static MRESULT EXPENTRY	ScrollbarProc _ANSI_ARGS_((HWND hwnd, ULONG message,
			    MPARAM param1, MPARAM param2));
static void		UpdateScrollbar _ANSI_ARGS_((
    			    OS2Scrollbar *scrollPtr));
static void		UpdateScrollbarMetrics _ANSI_ARGS_((void));

/*
 * The class procedure table for the scrollbar widget.
 */

TkClassProcs tkpScrollbarProcs = {
    CreateProc,			/* createProc */
    NULL,			/* geometryProc */
    ModalLoopProc,		/* modalProc */
};


/*
 *----------------------------------------------------------------------
 *
 * TkpCreateScrollbar --
 *
 *	Allocate a new TkScrollbar structure.
 *
 * Results:
 *	Returns a newly allocated TkScrollbar structure.
 *
 * Side effects:
 *	Registers an event handler for the widget.
 *
 *----------------------------------------------------------------------
 */

TkScrollbar *
TkpCreateScrollbar(tkwin)
    Tk_Window tkwin;
{
    OS2Scrollbar *scrollPtr;
    TkWindow *winPtr = (TkWindow *)tkwin;

#ifdef VERBOSE
    printf("TkpCreateScrollbar\n");
#endif
    
    if (!initialized) {
        Tcl_MutexLock(&os2ScrlbrMutex);
	UpdateScrollbarMetrics();
	initialized = 1;
        Tcl_MutexUnlock(&os2ScrlbrMutex);
    }

    scrollPtr = (OS2Scrollbar *) ckalloc(sizeof(OS2Scrollbar));
    scrollPtr->os2Flags = 0;
    scrollPtr->hwnd = NULLHANDLE;

    Tk_CreateEventHandler(tkwin,
	    ExposureMask|StructureNotifyMask|FocusChangeMask,
	    TkScrollbarEventProc, (ClientData) scrollPtr);

    if (!Tcl_GetAssocData(winPtr->mainPtr->interp, "TkScrollbar", NULL)) {
	Tcl_SetAssocData(winPtr->mainPtr->interp, "TkScrollbar", NULL,
		(ClientData)1);
	TkCreateBindingProcedure(winPtr->mainPtr->interp,
		winPtr->mainPtr->bindingTable,
		(ClientData)Tk_GetUid("Scrollbar"), "<ButtonPress>",
		ScrollbarBindProc, NULL, NULL);
    }

    return (TkScrollbar*) scrollPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateScrollbar --
 *
 *	This function updates the position and size of the scrollbar
 *	thumb based on the current settings.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Moves the thumb.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateScrollbar(scrollPtr)
    OS2Scrollbar *scrollPtr;
{
    SHORT posFirst, posLast, posThumb, cVisible, cTotal;
    double thumbSize;

#ifdef VERBOSE
    printf("UpdateScrollbar, firstFraction %f, lastFraction %f\n",
           scrollPtr->info.firstFraction, scrollPtr->info.lastFraction);
#endif

    /*
     * Update the current scrollbar position and shape.
     */

    posFirst = 0;
    posLast = MAX_SCROLL;
    /*
     * cVisible and cTotal determine OS/2's thumbsize,
     * use MAX_SCROLL for cTotal
     */
    thumbSize = (scrollPtr->info.lastFraction - scrollPtr->info.firstFraction);
    cVisible = (SHORT) (thumbSize * (double) MAX_SCROLL) + 1;
    cTotal = MAX_SCROLL;
    if (thumbSize < 1.0) {
	posThumb = (SHORT) ((scrollPtr->info.firstFraction / (1.0-thumbSize))
		    * (double)cTotal);
    } else {
	posThumb = 0;
    }
#ifdef VERBOSE
    printf("UpdateScrollbar sPtr %x, posThumb %d, thumbSize %f, cVisible %d\n",
           scrollPtr, posThumb, thumbSize, cVisible);
#endif
    /* Set scrollbar range (param2) and slider position (param1) */
    rc = (LONG) WinSendMsg(scrollPtr->hwnd, SBM_SETSCROLLBAR,
                           MPFROMSHORT(posThumb),
                           MPFROM2SHORT(posFirst, posLast));
#ifdef VERBOSE
    printf("    SBM_SETSCROLLBAR %s (th %d, f %d, l %d), now pos %d\n",
           rc==TRUE ? "OK" : "ERROR", posThumb, posFirst, posLast,
           WinSendMsg(scrollPtr->hwnd, SBM_QUERYPOS, MPVOID, MPVOID));
#endif
    /* Set scrollbar thumb size */
    rc = (LONG) WinSendMsg(scrollPtr->hwnd, SBM_SETTHUMBSIZE,
                           MPFROM2SHORT(cVisible, cTotal), MPVOID);
#ifdef VERBOSE
    printf("    SBM_SETTHUMBSIZE %s\n", rc==TRUE ? "OK" : "ERROR");
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * CreateProc --
 *
 *	This function creates a new Scrollbar control, subclasses
 *	the instance, and generates a new Window object.
 *
 * Results:
 *	Returns the newly allocated Window object, or None on failure.
 *
 * Side effects:
 *	Causes a new Scrollbar control to come into existence.
 *
 *----------------------------------------------------------------------
 */

static Window
CreateProc(tkwin, parentWin, instanceData)
    Tk_Window tkwin;		/* Token for window. */
    Window parentWin;		/* Parent of new window. */
    ClientData instanceData;	/* Scrollbar instance data. */
{
    ULONG style, id;
    Window window;
    HWND parent;
    TkWindow *winPtr;
    OS2Scrollbar *scrollPtr = (OS2Scrollbar *)instanceData;

#ifdef VERBOSE
    printf("CreateProc\n");
#endif

    parent = Tk_GetHWND(parentWin);

    if (scrollPtr->info.vertical) {
	id = FID_VERTSCROLL;
	style = WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
	        | SBS_AUTOTRACK | SBS_VERT;
    } else {
	id = FID_HORZSCROLL;
	style = WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
	        | SBS_AUTOTRACK | SBS_HORZ;
    }

    scrollPtr->hwnd = WinCreateWindow(parent, WC_SCROLLBAR, "SCROLLBAR", style,
                                      Tk_X(tkwin),
				      TkOS2HwndHeight(parent) -
				          (Tk_Y(tkwin) + Tk_Height(tkwin)),
				      Tk_Width(tkwin), Tk_Height(tkwin),
				      parent, HWND_TOP, id, NULL, NULL);
    if (scrollPtr->hwnd == NULLHANDLE) {
        scrollPtr->oldProc = WinDefWindowProc;
#ifdef VERBOSE
        printf("WinCreateWindow scrlbr %x, par %x: (%d,%d) %dx%d ERROR %x\n",
               scrollPtr->hwnd, parent, Tk_X(tkwin),
	       TkOS2HwndHeight(parent) - (Tk_Y(tkwin) + Tk_Height(tkwin)),
	       Tk_Width(tkwin), Tk_Height(tkwin),
               WinGetLastError(TclOS2GetHAB()));
#endif
        return None;
#ifdef VERBOSE
    } else {
        printf("WinCreateWindow scrlbr %x, par %x: (%d,%d) %dx%d (%d-%d-%d)\n",
               scrollPtr->hwnd, parent, Tk_X(tkwin),
	       TkOS2HwndHeight(parent) - (Tk_Y(tkwin) + Tk_Height(tkwin)),
	       Tk_Width(tkwin), Tk_Height(tkwin), TkOS2HwndHeight(parent),
	       Tk_Y(tkwin), Tk_Height(tkwin));
#endif
    }

    /*
     * Ensure new window is inserted into the stacking order at the correct
     * place. 
     */

    for (winPtr = ((TkWindow*)tkwin)->nextPtr; winPtr != NULL;
	 winPtr = winPtr->nextPtr) {
	if ((winPtr->window != None) && !(winPtr->flags & TK_TOP_LEVEL)) {
	    TkOS2SetWindowPos(scrollPtr->hwnd, Tk_GetHWND(winPtr->window),
		    Below);
	    break;
	}
    }

    scrollPtr->lastVertical = scrollPtr->info.vertical;
    scrollPtr->oldProc = WinSubclassWindow(scrollPtr->hwnd, ScrollbarProc);
#ifdef VERBOSE
    if (scrollPtr->oldProc == 0L) {
        printf("WinSubclassWindow scrlbr %x ScrollbarProc (%x) ERROR %x\n",
               scrollPtr->hwnd, ScrollbarProc, WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("WinSubclassWindow scrlbr %x ScrollbarProc (%x) OK: %x\n",
               scrollPtr->hwnd, ScrollbarProc, scrollPtr->oldProc);
    }
#endif
    window = Tk_AttachHWND(tkwin, scrollPtr->hwnd);

    UpdateScrollbar(scrollPtr);
    return window;
}

/*
 *--------------------------------------------------------------
 *
 * TkpDisplayScrollbar --
 *
 *	This procedure redraws the contents of a scrollbar window.
 *	It is invoked as a do-when-idle handler, so it only runs
 *	when there's nothing else for the application to do.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.
 *
 *--------------------------------------------------------------
 */

void
TkpDisplayScrollbar(clientData)
    ClientData clientData;	/* Information about window. */
{
    OS2Scrollbar *scrollPtr = (OS2Scrollbar *) clientData;
    Tk_Window tkwin = scrollPtr->info.tkwin;

#ifdef VERBOSE
    printf("TkpDisplayScrollbar\n");
#endif

    scrollPtr->info.flags &= ~REDRAW_PENDING;
    if ((tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	return;
    }

    /*
     * Destroy and recreate the scrollbar control if the orientation
     * has changed.
     */

    if (scrollPtr->lastVertical != scrollPtr->info.vertical) {
	HWND hwnd = Tk_GetHWND(Tk_WindowId(tkwin));

	WinDestroyWindow(hwnd);

	CreateProc(tkwin, Tk_WindowId(Tk_Parent(tkwin)),
		(ClientData) scrollPtr);
    } else {
	UpdateScrollbar(scrollPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyScrollbar --
 *
 *	Free data structures associated with the scrollbar control.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the default control state.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyScrollbar(scrollPtr)
    TkScrollbar *scrollPtr;
{
    OS2Scrollbar *winScrollPtr = (OS2Scrollbar *)scrollPtr;
    HWND hwnd = winScrollPtr->hwnd;

#ifdef VERBOSE
    printf("TkpDestroyScrollbar\n");
#endif
    if (hwnd) {
	if (winScrollPtr->os2Flags & IN_MODAL_LOOP) {
	    ((TkWindow *)scrollPtr->tkwin)->flags |= TK_DONT_DESTROY_WINDOW;
	    WinSetParent(hwnd, HWND_DESKTOP, FALSE);
	    WinSetOwner(hwnd, NULLHANDLE);
	}
    }
    winScrollPtr->os2Flags |= ALREADY_DEAD;
    /* Reset lastWinPtr etc. */
    TkPointerDeadWindow((TkWindow *)scrollPtr->tkwin);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateScrollbarMetrics --
 *
 *	This function retrieves the current system metrics for a
 *	scrollbar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the geometry cache info for all scrollbars.
 *
 *----------------------------------------------------------------------
 */

void
UpdateScrollbarMetrics()
{
    Tk_ConfigSpec *specPtr;

#ifdef VERBOSE
    printf("UpdateScrollbarMetrics\n");
#endif

    hArrowWidth = WinQuerySysValue(HWND_DESKTOP, SV_CXHSCROLLARROW);
    hThumb = WinQuerySysValue(HWND_DESKTOP, SV_CXHSLIDER);
    vArrowWidth = WinQuerySysValue(HWND_DESKTOP, SV_CXVSCROLL);
    vArrowHeight = WinQuerySysValue(HWND_DESKTOP, SV_CYVSCROLLARROW);
    vThumb = WinQuerySysValue(HWND_DESKTOP, SV_CYVSLIDER);

    sprintf(defWidth, "%d", vArrowWidth);
    for (specPtr = tkpScrollbarConfigSpecs; specPtr->type != TK_CONFIG_END;
	    specPtr++) {
	if (specPtr->offset == Tk_Offset(TkScrollbar, width)) {
	    specPtr->defValue = defWidth;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpComputeScrollbarGeometry --
 *
 *	After changes in a scrollbar's size or configuration, this
 *	procedure recomputes various geometry information used in
 *	displaying the scrollbar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The scrollbar will be displayed differently.
 *
 *----------------------------------------------------------------------
 */

void
TkpComputeScrollbarGeometry(scrollPtr)
    register TkScrollbar *scrollPtr;	/* Scrollbar whose geometry may
					 * have changed. */
{
    int fieldLength, minThumbSize;

#ifdef VERBOSE
    printf("TkpComputeScrollbarGeometry\n");
#endif

    /*
     * OS/2 doesn't use focus rings on scrollbars, but we still
     * perform basic sanity checks to appease backwards compatibility.
     */

    if (scrollPtr->highlightWidth < 0) {
	scrollPtr->highlightWidth = 0;
    }

    if (scrollPtr->vertical) {
	scrollPtr->arrowLength = vArrowHeight;
	fieldLength = Tk_Height(scrollPtr->tkwin);
	minThumbSize = vThumb;
    } else {
	scrollPtr->arrowLength = hArrowWidth;
	fieldLength = Tk_Width(scrollPtr->tkwin);
	minThumbSize = hThumb;
    }
    fieldLength -= 2*scrollPtr->arrowLength;
    if (fieldLength < 0) {
	fieldLength = 0;
    }
    scrollPtr->sliderFirst = (int) ((double)fieldLength
	    * scrollPtr->firstFraction);
    scrollPtr->sliderLast = (int) ((double)fieldLength
	    * scrollPtr->lastFraction);

    /*
     * Adjust the slider so that some piece of it is always
     * displayed in the scrollbar and so that it has at least
     * a minimal width (so it can be grabbed with the mouse).
     */

    if (scrollPtr->sliderFirst > fieldLength) {
	scrollPtr->sliderFirst = fieldLength;
    }
    if (scrollPtr->sliderFirst < 0) {
	scrollPtr->sliderFirst = 0;
    }
    if (scrollPtr->sliderLast < (scrollPtr->sliderFirst
	    + minThumbSize)) {
	scrollPtr->sliderLast = scrollPtr->sliderFirst + minThumbSize;
    }
    if (scrollPtr->sliderLast > fieldLength) {
	scrollPtr->sliderLast = fieldLength;
    }
    scrollPtr->sliderFirst += scrollPtr->arrowLength;
    scrollPtr->sliderLast += scrollPtr->arrowLength;

    /*
     * Register the desired geometry for the window (leave enough space
     * for the two arrows plus a minimum-size slider, plus border around
     * the whole window, if any).  Then arrange for the window to be
     * redisplayed.
     */

    if (scrollPtr->vertical) {
	Tk_GeometryRequest(scrollPtr->tkwin,
		scrollPtr->width, 2*scrollPtr->arrowLength + minThumbSize);
    } else {
	Tk_GeometryRequest(scrollPtr->tkwin,
		2*scrollPtr->arrowLength + minThumbSize, scrollPtr->width);
    }
    Tk_SetInternalBorder(scrollPtr->tkwin, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * ScrollbarProc --
 *
 *	This function is called by OS/2 PM whenever an event occurs on
 *	a scrollbar control created by Tk.
 *
 * Results:
 *	Standard OS/2 PM return value.
 *
 * Side effects:
 *	May generate events.
 *
 *----------------------------------------------------------------------
 */

static MRESULT EXPENTRY
ScrollbarProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    MRESULT result;
    POINTL point;
    OS2Scrollbar *scrollPtr;
    Tk_Window tkwin = Tk_HWNDToWindow(hwnd);

#ifdef VERBOSE
    printf("ScrollbarProc hwnd %x (tkwin %x), msg %x, p1 %x, p2 %x, owner %x\n",
           hwnd, tkwin, message, param1, param2, WinQueryWindow(hwnd,QW_OWNER));
    fflush(stdout);
#endif

    /*
     * tkwin can be NULL for WM_DESTROY due to the order of messages
     * In that case use WinDefWindowProc.
     */
    if (tkwin == NULL) {
#ifdef VERBOSE
        printf("ScrollbarProc: WM_DESTROY\n");
#endif
        return WinDefWindowProc(hwnd, message, param1, param2);
    }
    if (tkwin == NULL) {
#ifdef VERBOSE
        printf("panicking...\n");
        fflush(stdout);
#endif
	panic("ScrollbarProc called on an invalid HWND");
    }
    scrollPtr = (OS2Scrollbar *)((TkWindow*)tkwin)->instanceData;

    switch(message) {
	case WM_HSCROLL:
	case WM_VSCROLL: {

            /*
             * param1: USHORT control identity
             * param2: SHORT slider position,
             *               0: either operator is not moving slider with
             *                  pointer, or (where uscmd==SB_SLIDERPOSITION)
             *                  pointer outside tracking rectangle when button
             *                  is released.
             *         USHORT command (SB_LINEUP, SB_LINEDOWN, SB_PAGEUP,
             *                         SB_PAGEDOWN, SB_LINELEFT, SB_LINERIGHT,
             *                         SB_PAGELEFT, SB_PAGERIGHT,
             *                         SB_SLIDERPOSITION, SB_SLIDERTRACK,
             *                         SB_ENDSCROLL)
             */

	    Tcl_Interp *interp;
	    Tcl_DString cmdString;
	    USHORT command = SHORT2FROMMP(param2);
	    int code;
#ifdef VERBOSE
            printf("WM_%cSCROLL, cntrl %x, cmd %x (%s)\n",
                   message == WM_HSCROLL ?  'H' : 'V', param1, command,
                   command == SB_LINELEFT ? "SB_LINELEFT/UP" :
                   (command == SB_LINERIGHT ? "SB_LINERIGHT/DOWN" :
                    (command == SB_PAGELEFT ? "SB_PAGELEFT/UP" :
                     (command == SB_PAGERIGHT ? "SB_PAGERIGHT/DOWN" :
                      (command == SB_SLIDERPOSITION ? "SB_SLIDERPOSITION" :
                       (command == SB_SLIDERTRACK ? "SB_SLIDERTRACK" :
                        (command == SB_ENDSCROLL ? "SB_ENDSCROLL" : "???"
                    )))))));
#endif

	    WinQueryPointerPos(HWND_DESKTOP, &point);
	    Tk_TranslateOS2Event(NULLHANDLE, WM_MOUSEMOVE,
		    MPFROM2SHORT(point.x, point.y),
                    MPFROM2SHORT(HT_NORMAL, KC_NONE), &result);

	    if (command == SB_ENDSCROLL) {
        	/*
        	 * Operator has finished scrolling but has not been doing any
        	 * absolute positioning.
        	 */
		return 0;
	    }

	    /*
	     * Bail out immediately if there isn't a command to invoke.
	     */

	    if (scrollPtr->info.commandSize == 0) {
		Tcl_ServiceAll();
		return 0;
	    }
		
	    Tcl_DStringInit(&cmdString);
	    Tcl_DStringAppend(&cmdString, scrollPtr->info.command,
		    scrollPtr->info.commandSize);
		
            /*
             * The commands SB_LINELEFT and SB_LINEUP, SB_LINERIGHT and
             * SB_LINEDOWN, SB_PAGELEFT and SB_PAGEUP and SB_PAGERIGHT and
             * SB_PAGEDOWN are equivalent, but I'm trusting a good compiler
             * will notice the superfluous values and eliminate them, rather
             * than relying on this feature.
             */
	    if (command == SB_LINELEFT || command == SB_LINEUP ||
                command == SB_LINERIGHT || command == SB_LINEDOWN) {
		Tcl_DStringAppendElement(&cmdString, "scroll");
		Tcl_DStringAppendElement(&cmdString,
			(command == SB_LINELEFT || command == SB_LINEUP)
                        ? "-1" : "1");
		Tcl_DStringAppendElement(&cmdString, "units");
	    } else if (command == SB_PAGELEFT || command == SB_PAGEUP ||
                       command == SB_PAGERIGHT || command == SB_PAGEDOWN) {
		Tcl_DStringAppendElement(&cmdString, "scroll");
		Tcl_DStringAppendElement(&cmdString,
			(command == SB_PAGELEFT || command == SB_PAGEUP)
                        ? "-1" : "1");
		Tcl_DStringAppendElement(&cmdString, "pages");
	    } else {
		char valueString[TCL_DOUBLE_SPACE];
		double pos = 0.0;
		switch (command) {
		    case SB_SLIDERPOSITION:
			pos = ((double)SHORT1FROMMP(param2)) / MAX_SCROLL;
#ifdef VERBOSE
                        printf("SB_SLIDERPOSITION, pos %f\n", pos);
#endif
			break;

		    case SB_SLIDERTRACK:
			pos = ((double)SHORT1FROMMP(param2)) / MAX_SCROLL;
#ifdef VERBOSE
                        printf("SB_SLIDERTRACK, pos %f\n", pos);
#endif
			break;
		}
		sprintf(valueString, "%g", pos);
		Tcl_DStringAppendElement(&cmdString, "moveto");
		Tcl_DStringAppendElement(&cmdString, valueString);
	    }

	    interp = scrollPtr->info.interp;
#ifdef VERBOSE
            printf("SCROLL command [%s]\n", cmdString.string);
#endif
	    code = Tcl_GlobalEval(interp, cmdString.string);
	    if (code != TCL_OK && code != TCL_CONTINUE && code != TCL_BREAK) {
		Tcl_AddErrorInfo(interp, "\n    (scrollbar command)");
		Tcl_BackgroundError(interp);
	    }		
	    Tcl_DStringFree(&cmdString);		

	    Tcl_ServiceAll();
	    return 0;
	}

/* The following will generate pointer events for button presses in the
 * scrollbar and then return 1, thereby "hiding" the button presses from
 * the scrollbar control. That in turn prevents it from generating
 * WM_VSCROLL / WM_HSCROLL messages...
	default:
	    if (Tk_TranslateOS2Event(hwnd, message, param1, param2, &result)) {
		return result;
	    }
*/
    }
    return (*scrollPtr->oldProc)(hwnd, message, param1, param2);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpConfigureScrollbar --
 *
 *	This procedure is called after the generic code has finished
 *	processing configuration options, in order to configure
 *	platform specific options.
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
TkpConfigureScrollbar(scrollPtr)
    register TkScrollbar *scrollPtr;	/* Information about widget;  may or
					 * may not already have values for
					 * some fields. */
{

#ifdef VERBOSE
    printf("TkpConfigureScrollbar\n");
#endif
}

/*
 *--------------------------------------------------------------
 *
 * ScrollbarBindProc --
 *
 *	This procedure is invoked when the default <ButtonPress>
 *	binding on the Scrollbar bind tag fires.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The event enters a modal loop.
 *
 *--------------------------------------------------------------
 */

static int
ScrollbarBindProc(clientData, interp, eventPtr, tkwin, keySym)
    ClientData clientData;
    Tcl_Interp *interp;
    XEvent *eventPtr;
    Tk_Window tkwin;
    KeySym keySym;
{
    TkWindow *winPtr = (TkWindow*)tkwin;

#ifdef VERBOSE
    printf("ScrollbarBindProc (%s)\n", eventPtr->type == ButtonPress ?
           "ButtonPress" : "not ButtonPress");
#endif
    if (eventPtr->type == ButtonPress) {
	winPtr->flags |= TK_DEFER_MODAL;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ModalLoopProc --
 *
 *	This function is invoked at the end of the event processing
 *	whenever the ScrollbarBindProc has been invoked for a ButtonPress
 *	event. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Enters a modal loop.
 *
 *----------------------------------------------------------------------
 */

static void
ModalLoopProc(tkwin, eventPtr)
    Tk_Window tkwin;
    XEvent *eventPtr;
{
    TkWindow *winPtr = (TkWindow*)tkwin;
    OS2Scrollbar *scrollPtr = (OS2Scrollbar *) winPtr->instanceData;
    int oldMode;

#ifdef VERBOSE
    printf("ModalLoopProc\n");
#endif

    if (scrollPtr->hwnd != NULLHANDLE) {
        Tcl_Preserve((ClientData)scrollPtr);
        scrollPtr->os2Flags |= IN_MODAL_LOOP;
        oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
        TkOS2ResendEvent(scrollPtr->oldProc, scrollPtr->hwnd, eventPtr);
        (void) Tcl_SetServiceMode(oldMode);
        scrollPtr->os2Flags &= ~IN_MODAL_LOOP;
        if (scrollPtr->hwnd && scrollPtr->os2Flags & ALREADY_DEAD) {
	    WinDestroyWindow(scrollPtr->hwnd);
        }
        Tcl_Release((ClientData)scrollPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkpScrollbarPosition --
 *
 *	Determine the scrollbar element corresponding to a
 *	given position.
 *
 * Results:
 *	One of TOP_ARROW, TOP_GAP, etc., indicating which element
 *	of the scrollbar covers the position given by (x, y).  If
 *	(x,y) is outside the scrollbar entirely, then OUTSIDE is
 *	returned.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkpScrollbarPosition(scrollPtr, x, y)
    register TkScrollbar *scrollPtr;	/* Scrollbar widget record. */
    int x, y;				/* Coordinates within scrollPtr's
					 * window. */
{
    int length, width, tmp;

    if (scrollPtr->vertical) {
	length = Tk_Height(scrollPtr->tkwin);
	width = Tk_Width(scrollPtr->tkwin);
    } else {
	tmp = x;
	x = y;
	y = tmp;
	length = Tk_Width(scrollPtr->tkwin);
	width = Tk_Height(scrollPtr->tkwin);
    }

#ifdef VERBOSE
    printf("TkpScrollbarPosition USING x %d w %d y %d l %d i %d\n",
               x, width, y, length, scrollPtr->inset);
#endif
    if ((x < scrollPtr->inset) || (x >= (width - scrollPtr->inset))
	    || (y < scrollPtr->inset) || (y >= (length - scrollPtr->inset))) {
#ifdef VERBOSE
        printf("TkpScrollbarPosition OUTSIDE x %d w %d y %d l %d i %d\n",
               x, width, y, length, scrollPtr->inset);
#endif
	return OUTSIDE;
    }

    /*
     * All of the calculations in this procedure mirror those in
     * TkpDisplayScrollbar.  Be sure to keep the two consistent.
     */

    if (y < (scrollPtr->inset + scrollPtr->arrowLength)) {
#ifdef VERBOSE
        printf("TkpScrollbarPosition TOP_ARROW\n");
#endif
	return TOP_ARROW;
    }
    if (y < scrollPtr->sliderFirst) {
#ifdef VERBOSE
        printf("TkpScrollbarPosition TOP_GAP\n");
#endif
	return TOP_GAP;
    }
    if (y < scrollPtr->sliderLast) {
#ifdef VERBOSE
        printf("TkpScrollbarPosition SLIDER\n");
#endif
	return SLIDER;
    }
    if (y >= (length - (scrollPtr->arrowLength + scrollPtr->inset))) {
#ifdef VERBOSE
        printf("TkpScrollbarPosition BOTTOM_ARROW\n");
#endif
	return BOTTOM_ARROW;
    }
#ifdef VERBOSE
        printf("TkpScrollbarPosition BOTTOM_GAP\n");
#endif
    return BOTTOM_GAP;
}

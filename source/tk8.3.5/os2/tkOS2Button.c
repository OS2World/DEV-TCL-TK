/* 
 * tkOS2Button.c --
 *
 *	This file implements the OS/2 specific portion of the button
 *	widgets.
 *
 * Copyright (c) 1996-1998 by Sun Microsystems, Inc.
 * Copyright (c) 1999-2003 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tkOS2Int.h"
#include "tkResIDs.h"
#include "tkButton.h"

#ifdef VERBOSE
#include "tk3d.h"
#endif

/*
 * Declaration of OS/2 specific button structure.
 */

typedef struct OS2Button {
    TkButton info;		/* Generic button info. */
    PFNWP oldProc;		/* Old window procedure. */
    HWND hwnd;			/* Current window handle. */
    Pixmap pixmap;		/* Bitmap for rendering the button. */
    ULONG style;		/* Window style flags. */
} OS2Button;


/*
 * The following macro reverses the order of RGB bytes to convert
 * between RGBQUAD and LONG values.
 */

/*
#define FlipColor(rgb) (rgb)
*/
#define FlipColor(rgb) (RGB(GetBValue(rgb),GetGValue(rgb),GetRValue(rgb)))

/*
 * The following enumeration defines the meaning of the palette entries
 * in the "buttons" image used to draw checkbox and radiobutton indicators.
 */

enum {
    PAL_CHECK = 7,
    PAL_TOP_OUTER = 8,
    PAL_BOTTOM_OUTER = 0,
    PAL_BOTTOM_INNER = 14,
    PAL_INTERIOR = 15,
    PAL_TOP_INNER = 13,
    PAL_BACKGROUND = 12
};

/*
 * Cached information about the boxes bitmap, and the default border
 * width for a button in string form for use in Tk_OptionSpec for
 * the various button widget classes.
 */

typedef struct ThreadSpecificData {
    HBITMAP hBoxes;	/* Handle of the bitmap. */
    ULONG *palTable;			/* Color palette. */
    HDC boxesDC;
    HPS boxesPS;
    /*RGB *boxesPalette = NULL;          /* Pointer to color palette. */
    HPAL boxesPalette;
    PBYTE boxesBits;			/* Pointer to bitmap data. */
    ULONG boxHeight;			/* Width of each sub-image. */
    ULONG boxWidth;			/* Width of each sub-image. */
    BITMAPINFO2 *boxesPtr;		/* Information about the bitmap */
    /*
     * This variable holds the default border width for a button in string
     * form for use in a Tk_ConfigSpec.
     */
    char defWidth[TCL_INTEGER_SPACE];
    USHORT lastCommandID;	        /* The last command ID we allocated. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Declarations for functions defined in this file.
 */

static MRESULT EXPENTRY	ButtonProc _ANSI_ARGS_((HWND hwnd, ULONG message,
			    MPARAM param1, MPARAM param2));
static Window		CreateProc _ANSI_ARGS_((Tk_Window tkwin,
			    Window parent, ClientData instanceData));
static void		InitBoxes _ANSI_ARGS_((void));
static void		CleanupBoxes _ANSI_ARGS_((ClientData clientData));

/*
 * The class procedure table for the button widgets.
 */

TkClassProcs tkpButtonProcs = { 
    CreateProc,			/* createProc. */
    TkButtonWorldChanged,	/* geometryProc. */
    NULL			/* modalProc. */ 
};


/*
 *----------------------------------------------------------------------
 *
 * InitBoxes --
 *
 *	This function loads the Tk 3d button bitmap.  "buttons" is a 16 
 *	color bitmap that is laid out such that the top row contains 
 *	the 4 checkbox images, and the bottom row contains the radio 
 *	button images. Note that the bitmap is stored in bottom-up 
 *	format.  Also, the first seven palette entries are used to 
 *	identify the different parts of the bitmaps so we can do the 
 *	appropriate color mappings based on the current button colors.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Loads the "buttons" resource.
 *
 *----------------------------------------------------------------------
 */

static void
InitBoxes()
{
    /*
     * For DLLs like Tk, the HINSTANCE is the same as the HMODULE.
     */

#define BOXROWS 3
#define TKROWS 2
#define BOXCOLS 4
    DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
    SIZEL sizl = {0,0}; /* use same page size as device */
#ifdef VERBOSE
    int i;
#endif
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    tsdPtr->boxesDC = DevOpenDC(TclOS2GetHAB(), OD_MEMORY, (PSZ)"*", 5L,
                                (PDEVOPENDATA)&dop, NULLHANDLE);
    if (tsdPtr->boxesDC == DEV_ERROR) {
#ifdef VERBOSE
        printf("DevOpenDC ERROR in InitBoxes\n");
#endif
        return;
    }
#ifdef VERBOSE
    printf("DevOpenDC in InitBoxes returns %x\n", tsdPtr->boxesDC);
#endif
    tsdPtr->boxesPS = GpiCreatePS(TclOS2GetHAB(), tsdPtr->boxesDC, &sizl,
                                  PU_PELS | GPIT_NORMAL | GPIA_ASSOC);
    if (tsdPtr->boxesPS == NULLHANDLE) {
#ifdef VERBOSE
        printf("InitBoxes, GpiCreatePS ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
#endif
        DevCloseDC(tsdPtr->boxesDC);
        return;
#ifdef VERBOSE
    } else {
        printf("InitBoxes, GpiCreatePS OK %x\n", tsdPtr->boxesPS);
#endif
    }
    Tcl_CreateExitHandler(CleanupBoxes, (ClientData) NULL);
/*
    tsdPtr->hBoxes = GpiLoadBitmap(tsdPtr->boxesPS, Tk_GetHMODULE(), buttons,
                                   0, 0);
*/
    tsdPtr->hBoxes = WinGetSysBitmap(HWND_DESKTOP, SBMP_CHECKBOXES);
#ifdef VERBOSE
/*
    if (tsdPtr->hBoxes == GPI_ERROR) {
*/
    if (tsdPtr->hBoxes == NULLHANDLE) {
        printf("InitBoxes, GpiGetSysBitmap into boxesPS %x ERROR %x\n",
               tsdPtr->boxesPS, WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("InitBoxes, GpiGetSysBitmap into boxesPS %x OK %x\n",
               tsdPtr->boxesPS, tsdPtr->hBoxes);
    }
#endif
/*
    if (tsdPtr->hBoxes == GPI_ERROR) {
        tsdPtr->hBoxes = NULLHANDLE;
    }
*/


    /*
     * Copy the bitmap into writable memory.
     */

    if (tsdPtr->hBoxes != NULLHANDLE) {
        BITMAPINFOHEADER bmpInfo;
        rc = GpiQueryBitmapParameters(tsdPtr->hBoxes, &bmpInfo);
        if (rc == TRUE && !(bmpInfo.cx % BOXCOLS) && !(bmpInfo.cy % BOXROWS)) {
#ifdef VERBOSE
            printf("    cx %d, cy %d, cPlanes %d, cBitCount %d\n", bmpInfo.cx,
                   bmpInfo.cy, bmpInfo.cPlanes, bmpInfo.cBitCount);
#endif
	    tsdPtr->boxWidth = bmpInfo.cx / BOXCOLS;
	    tsdPtr->boxHeight = bmpInfo.cy / BOXROWS;
            tsdPtr->boxesPtr = (PBITMAPINFO2) ckalloc ( sizeof(BITMAPINFO2) +
                          (1 << bmpInfo.cBitCount) * sizeof(LONG) +
                          tsdPtr->boxWidth * tsdPtr->boxHeight * sizeof(ULONG));

            rc = GpiSetBitmap(tsdPtr->boxesPS, tsdPtr->hBoxes);
#ifdef VERBOSE
            if (rc == HBM_ERROR) {
                printf("    GpiSetBitmap %x %x ERROR %x\n", tsdPtr->boxesPS,
                       tsdPtr->hBoxes, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("    GpiSetBitmap %x %x returned %x\n", tsdPtr->boxesPS,
                       tsdPtr->hBoxes, rc);
            }
#endif
            tsdPtr->boxesPtr->cbFix = sizeof(BITMAPINFOHEADER2);
            rc = GpiQueryBitmapInfoHeader(tsdPtr->hBoxes,
                                          (PBITMAPINFOHEADER2)tsdPtr->boxesPtr);
#ifdef VERBOSE
            if (rc == GPI_ALTERROR) {
                printf("    GpiQueryBitmapInfoHeader %x ERROR %x\n",
                       tsdPtr->hBoxes, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("    GpiQueryBitmapInfoHeader %x OK cy %d\n",
                       tsdPtr->hBoxes, tsdPtr->boxesPtr->cy);
            }
#endif
            tsdPtr->boxesPtr->cbFix = sizeof(BITMAPINFO2) +
                              (1 << tsdPtr->boxesPtr->cBitCount) * sizeof(LONG);
            /* Only use the top 2 rows (bottom is grayed checkbutton) */
            tsdPtr->boxesBits= (PBYTE)ckalloc(((
                        tsdPtr->boxesPtr->cBitCount*tsdPtr->boxesPtr->cx+31)/32)
                        * tsdPtr->boxesPtr->cPlanes * sizeof(ULONG)
                        * (tsdPtr->boxesPtr->cy * TKROWS / BOXROWS));
/*
            tsdPtr->boxesPalette = (RGB*)tsdPtr->boxesPtr->argbColor;
*/
            tsdPtr->palTable = (PULONG) tsdPtr->boxesPtr->argbColor;
            /* Get default colors */
            rc = GpiQueryPaletteInfo(NULLHANDLE, tsdPtr->boxesPS, 0L, 0, 16L,
                                     tsdPtr->palTable);
#ifdef VERBOSE
            if (rc == PAL_ERROR) {
                printf("GpiQueryPaletteInfo def. colors ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiQueryPaletteInfo def. colors returned %d entries\n",
                       rc);
            }
            for (i= 0; i<16; i++) {
                printf("initial palTable[%d] [%x]\n", i, tsdPtr->palTable[i]);
            }
#endif
            /* Only use the top 2 rows (bottom is grayed checkbutton) */
            rc = GpiQueryBitmapBits(tsdPtr->boxesPS,
                                    tsdPtr->boxesPtr->cy / BOXROWS,
                                    tsdPtr->boxesPtr->cy * TKROWS / BOXROWS,
                                    tsdPtr->boxesBits, tsdPtr->boxesPtr);
#ifdef VERBOSE
            if (rc == GPI_ALTERROR) {
                printf("    GpiQueryBitmapBits %x %x ERROR %x fix %d bits %d\n",
                       tsdPtr->boxesPS, tsdPtr->hBoxes,
                       WinGetLastError(TclOS2GetHAB()), tsdPtr->boxesPtr->cbFix,
                       (  (tsdPtr->boxesPtr->cBitCount
                           * tsdPtr->boxesPtr->cx + 31)/32)
                       * tsdPtr->boxesPtr->cPlanes * sizeof(ULONG)
                       * tsdPtr->boxesPtr->cy);
            } else {
                printf("    GpiQueryBitmapBits %x %x gave %d, fix %d bits %d\n",
                       tsdPtr->boxesPS, tsdPtr->hBoxes, rc,
                       tsdPtr->boxesPtr->cbFix,
                       (  (tsdPtr->boxesPtr->cBitCount
                           * tsdPtr->boxesPtr->cx + 31)/32)
                       * tsdPtr->boxesPtr->cPlanes * sizeof(ULONG)
                       * tsdPtr->boxesPtr->cy);
            }
#endif

            tsdPtr->boxesPalette = GpiCreatePalette(TclOS2GetHAB(), 0L,
                                                    LCOLF_CONSECRGB, 16L,
                                                    tsdPtr->palTable);
#ifdef VERBOSE
            if (tsdPtr->boxesPalette == GPI_ERROR) {
                printf("    GpiCreatePalette ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("    GpiCreatePalette OK: %x\n", tsdPtr->boxesPalette);
            }
#endif
            rc = GpiSelectPalette(tsdPtr->boxesPS, tsdPtr->boxesPalette);
#ifdef VERBOSE
            if (rc == PAL_ERROR) {
                printf("    GpiSelectPalette %x ERROR %x\n",
                       tsdPtr->boxesPalette, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("    GpiSelectPalette %x OK: %x\n", tsdPtr->boxesPalette,
                       rc);
            }
#endif
        } else {
	    tsdPtr->hBoxes = NULLHANDLE;
        }
    }
#undef BOXROWS
#undef BOXCOLS
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupBoxes --
 *
 *	This function frees the PS/DC combo for the boxes bitmap on exit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys boxesPS and closes boxesDC.
 *
 *----------------------------------------------------------------------
 */

static void
CleanupBoxes(
    ClientData clientData)	/* Not used */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("CleanupBoxes destroying PS %x and closing DC %x\n", tsdPtr->boxesPS,
           tsdPtr->boxesDC);
    fflush(stdout);
#endif
    GpiDestroyPS(tsdPtr->boxesPS);
    DevCloseDC(tsdPtr->boxesDC);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpButtonSetDefaults --
 *
 *	This procedure is invoked before option tables are created for
 *	buttons.  It modifies some of the default values to match the
 *	current values defined for this platform.
 *
 * Results:
 *	Some of the default values in *specPtr are modified.
 *
 * Side effects:
 *	Updates some of.
 *
 *----------------------------------------------------------------------
 */

void
TkpButtonSetDefaults(specPtr)
    Tk_OptionSpec *specPtr;     /* Points to an array of option specs,
                                 * terminated by one with type
                                 * TK_OPTION_END. */
{
    int width;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->defWidth[0] == 0) {
        width = WinQuerySysValue(HWND_DESKTOP, SV_CXBORDER);
        if (width == 0) {
	    width = 1;
        }
        sprintf(tsdPtr->defWidth, "%d", width);
    }
    for ( ; specPtr->type != TK_OPTION_END; specPtr++) {
	if (specPtr->internalOffset == Tk_Offset(TkButton, borderWidth)) {
	    specPtr->defValue = tsdPtr->defWidth;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCreateButton --
 *
 *	Allocate a new TkButton structure.
 *
 * Results:
 *	Returns a newly allocated TkButton structure.
 *
 * Side effects:
 *	Registers an event handler for the widget.
 *
 *----------------------------------------------------------------------
 */

TkButton *
TkpCreateButton(tkwin)
    Tk_Window tkwin;
{
    OS2Button *butPtr;

    butPtr = (OS2Button *)ckalloc(sizeof(OS2Button));
    butPtr->hwnd = NULLHANDLE;
    return (TkButton *) butPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateProc --
 *
 *	This function creates a new Button control, subclasses
 *	the instance, and generates a new Window object.
 *
 * Results:
 *	Returns the newly allocated Window object, or None on failure.
 *
 * Side effects:
 *	Causes a new Button control to come into existence.
 *
 *----------------------------------------------------------------------
 */

static Window
CreateProc(tkwin, parentWin, instanceData)
    Tk_Window tkwin;		/* Token for window. */
    Window parentWin;		/* Parent of new window. */
    ClientData instanceData;	/* Button instance data. */
{
    Window window;
    HWND parent;
    char *class;
    OS2Button *butPtr = (OS2Button *)instanceData;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    parent = Tk_GetHWND(parentWin);
    if (parent == NULLHANDLE) {
        parent = HWND_DESKTOP;
    }
    if (butPtr->info.type == TYPE_LABEL) {
#ifdef VERBOSE
        printf("CreateProc tkwin %x, butPtr %x, LABEL\n", tkwin, butPtr);
#endif
	class = WC_STATIC;
        butPtr->style = WS_VISIBLE | WS_CLIPSIBLINGS;
    } else {
#ifdef VERBOSE
        printf("CreateProc tkwin %x, butPtr %x, BUTTON\n", tkwin, butPtr);
#endif
	class = WC_BUTTON;
        butPtr->style = BS_USERBUTTON | WS_VISIBLE | WS_CLIPSIBLINGS;
    }
    tsdPtr->lastCommandID++;
    butPtr->hwnd = WinCreateWindow(parent, class, "", butPtr->style,
	                           Tk_X(tkwin),
				   TkOS2HwndHeight(parent) -
				       (Tk_Y(tkwin) + Tk_Height(tkwin)),
				   Tk_Width(tkwin), Tk_Height(tkwin),
				   parent, HWND_TOP, tsdPtr->lastCommandID,
				   (PVOID)NULL, (PVOID)NULL);
    if (butPtr->hwnd == NULLHANDLE) {
        butPtr->oldProc = WinDefWindowProc;
#ifdef VERBOSE
        printf("WinCreateWindow button p %x (%d,%d) %dx%d id %x t%s ERROR %x\n",
               parent, Tk_X(tkwin),
	       TkOS2HwndHeight(parent) - (Tk_Y(tkwin) + Tk_Height(tkwin)),
               Tk_Width(tkwin), Tk_Height(tkwin), tsdPtr->lastCommandID,
               butPtr->info.type == TYPE_LABEL ? "LABEL" :
               (butPtr->info.type == TYPE_BUTTON ? "BUTTON" :
               (butPtr->info.type == TYPE_CHECK_BUTTON ? "CHECK_BUTTON" :
               (butPtr->info.type == TYPE_RADIO_BUTTON ? "RADIO_BUTTON" :
                "UNKNOWN"))), WinGetLastError(TclOS2GetHAB()));
#endif
        return None;
#ifdef VERBOSE
    } else {
        printf("WinCreateWindow button %x p %x (%d,%d) %dx%d id %x t %s\n",
               butPtr->hwnd, parent, Tk_X(tkwin),
	       TkOS2HwndHeight(parent) - (Tk_Y(tkwin) + Tk_Height(tkwin)),
               Tk_Width(tkwin), Tk_Height(tkwin), tsdPtr->lastCommandID,
               butPtr->info.type == TYPE_LABEL ? "LABEL" :
               (butPtr->info.type == TYPE_BUTTON ? "BUTTON" :
               (butPtr->info.type == TYPE_CHECK_BUTTON ? "CHECK_BUTTON" :
               (butPtr->info.type == TYPE_RADIO_BUTTON ? "RADIO_BUTTON" :
                "UNKNOWN"))));
#endif
    }
    butPtr->oldProc = WinSubclassWindow(butPtr->hwnd, (PFNWP)ButtonProc);
#ifdef VERBOSE
    printf("WinSubclassWindow %x (%s) returns %x\n", butPtr->hwnd,
           butPtr->info.type == TYPE_LABEL ? "LABEL" :
           (butPtr->info.type == TYPE_BUTTON ? "BUTTON" :
           (butPtr->info.type == TYPE_CHECK_BUTTON ? "CHECK_BUTTON" :
           (butPtr->info.type == TYPE_RADIO_BUTTON ? "RADIO_BUTTON" :
           "UNKNOWN"))), butPtr->oldProc);
#endif

    window = Tk_AttachHWND(tkwin, butPtr->hwnd);
    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyButton --
 *
 *	Free data structures associated with the button control.
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
TkpDestroyButton(butPtr)
    TkButton *butPtr;
{
    OS2Button *os2ButPtr = (OS2Button *)butPtr;
    HWND hwnd = os2ButPtr->hwnd;
#ifdef VERBOSE
    printf("TkpDestroyButton butPtr %x hwnd %x\n", butPtr, hwnd);
    fflush(stdout);
#endif
    if (hwnd) {
        WinSubclassWindow(hwnd, (PFNWP)os2ButPtr->oldProc);
    }
/*** Not in Win port ****/
    /* Reset lastWinPtr etc. */
    /*TkPointerDeadWindow((TkWindow *)butPtr->tkwin);*/
/*** END Not in Win port ****/
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayButton --
 *
 *	This procedure is invoked to display a button widget.  It is
 *	normally invoked as an idle handler.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.  The REDRAW_PENDING flag
 *	is cleared.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayButton(clientData)
    ClientData clientData;	/* Information about widget. */
{
    TkOS2PSState state;
    HPS hps;
    register TkButton *butPtr = (TkButton *) clientData;
    GC gc;
    Tk_3DBorder border;
    Pixmap pixmap;
    int x = 0;			/* Initialization only needed to stop
				 * compiler warning. */
    int y, relief;
    register Tk_Window tkwin = butPtr->tkwin;
    int width, height;
    int defaultWidth;		/* Width of default ring. */
    int offset;			/* 0 means this is a label widget.  1 means
				 * it is a flavor of button, so we offset
				 * the text to make the button appear to
				 * move up and down as the relief changes. */
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("TkpDisplayButton type %x flags %x image %x bmp %x (%d,%d) %dx%d\n",
           butPtr->type, butPtr->flags, butPtr->image, butPtr->bitmap,
           Tk_X(tkwin), Tk_Y(tkwin), Tk_Width(tkwin), Tk_Height(tkwin));
#endif

    butPtr->flags &= ~REDRAW_PENDING;
    if ((butPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	return;
    }

    border = butPtr->normalBorder;
    if ((butPtr->state == STATE_DISABLED) && (butPtr->disabledFg != NULL)) {
	gc = butPtr->disabledGC;
    } else if ((butPtr->state == STATE_ACTIVE)
	    && !Tk_StrictMotif(butPtr->tkwin)) {
	gc = butPtr->activeTextGC;
	border = butPtr->activeBorder;
    } else {
	gc = butPtr->normalTextGC;
    }
    if ((butPtr->flags & SELECTED) && (butPtr->state != STATE_ACTIVE)
	    && (butPtr->selectBorder != NULL) && !butPtr->indicatorOn) {
	border = butPtr->selectBorder;
    }

    /*
     * Override the relief specified for the button if this is a
     * checkbutton or radiobutton and there's no indicator.
     */

    relief = butPtr->relief;
    if ((butPtr->type >= TYPE_CHECK_BUTTON) && !butPtr->indicatorOn) {
	relief = (butPtr->flags & SELECTED) ? TK_RELIEF_SUNKEN
		: TK_RELIEF_RAISED;
    }

    /*
     * Compute width of default ring and offset for pushed buttons.
     */

    if (butPtr->type == TYPE_BUTTON) {
	defaultWidth = ((butPtr->defaultState == DEFAULT_ACTIVE)
		? butPtr->highlightWidth : 0);
	offset = 1;
    } else {
	defaultWidth = 0;
	if ((butPtr->type >= TYPE_CHECK_BUTTON) && !butPtr->indicatorOn) {
	    offset = 1;
	} else {
	    offset = 0;
	}
    }

    /*
     * In order to avoid screen flashes, this procedure redraws
     * the button in a pixmap, then copies the pixmap to the
     * screen in a single operation.  This means that there's no
     * point in time where the on-sreen image has been cleared.
     */

    pixmap = Tk_GetPixmap(butPtr->display, Tk_WindowId(tkwin),
	    Tk_Width(tkwin), Tk_Height(tkwin), Tk_Depth(tkwin));
    Tk_Fill3DRectangle(tkwin, pixmap, border, 0, 0, Tk_Width(tkwin),
	    Tk_Height(tkwin), 0, TK_RELIEF_FLAT);

    /*
     * Display image or bitmap or text for button.
     */

    if (butPtr->image != None) {
	Tk_SizeOfImage(butPtr->image, &width, &height);

	imageOrBitmap:
	TkComputeAnchor(butPtr->anchor, tkwin, 0, 0,
		butPtr->indicatorSpace + width, height, &x, &y);
	x += butPtr->indicatorSpace;

	if (relief == TK_RELIEF_SUNKEN) {
	    x += offset;
	    y += offset;
	}
	if (butPtr->image != NULL) {
	    if ((butPtr->selectImage != NULL) && (butPtr->flags & SELECTED)) {
		Tk_RedrawImage(butPtr->selectImage, 0, 0, width, height,
			pixmap, x, y);
	    } else {
		Tk_RedrawImage(butPtr->image, 0, 0, width, height, pixmap,
			x, y);
	    }
	} else {
	    XSetClipOrigin(butPtr->display, gc, x, y);
	    XCopyPlane(butPtr->display, butPtr->bitmap, pixmap, gc, 0, 0,
		    (unsigned int) width, (unsigned int) height, x, y, 1);
	    XSetClipOrigin(butPtr->display, gc, 0, 0);
	}
	y += height/2;
    } else if (butPtr->bitmap != None) {
	Tk_SizeOfBitmap(butPtr->display, butPtr->bitmap, &width, &height);
	goto imageOrBitmap;
    } else {
	LONG xCursor, yCursor, cxCursor, cyCursor;
	CHARBUNDLE cBundle;
        AREABUNDLE aBundle;
        LONG oldColor, oldBackColor;

	TkComputeAnchor(butPtr->anchor, tkwin, butPtr->padX, butPtr->padY,
		butPtr->indicatorSpace + butPtr->textWidth, butPtr->textHeight,
		&x, &y);

	x += butPtr->indicatorSpace;

	if (relief == TK_RELIEF_SUNKEN) {
	    x += offset;
	    y += offset;
	}
	Tk_DrawTextLayout(butPtr->display, pixmap, gc, butPtr->textLayout,
		x, y, 0, -1);
	Tk_UnderlineTextLayout(butPtr->display, pixmap, gc,
		butPtr->textLayout, x, y, butPtr->underline);

	/*
	 * Draw the focus ring.  If this is a push button then we need to put
	 * it around the inner edge of the border, otherwise we put it around
	 * the text.
	 */

	if (butPtr->flags & GOT_FOCUS && butPtr->type != TYPE_LABEL) {
	    hps = TkOS2GetDrawablePS(butPtr->display, pixmap, &state);
	    if (butPtr->type == TYPE_BUTTON || !butPtr->indicatorOn) {
                yCursor = butPtr->borderWidth + 1 + defaultWidth;
                xCursor = yCursor;
                cxCursor = Tk_Width(tkwin) - xCursor - 2;
                cyCursor = Tk_Height(tkwin) - yCursor - 2;
	    } else {
                yCursor = y-2;
                xCursor = x-2;
                cxCursor = butPtr->textWidth + 4;
                cyCursor = butPtr->textHeight + 4;
	    }
	    GpiQueryAttrs(hps, PRIM_CHAR, LBB_COLOR, (PBUNDLE)&cBundle);
	    oldColor = cBundle.lColor;
            cBundle.lColor = gc->foreground;
            rc = GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs textColor %d ERROR %x\n", cBundle.lColor,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs textColor %d OK\n", cBundle.lColor);
            }
#endif
	    GpiQueryAttrs(hps, PRIM_AREA, LBB_BACK_COLOR, (PBUNDLE)&aBundle);
            oldBackColor = aBundle.lBackColor;
            aBundle.lBackColor = gc->background;
            rc = GpiSetAttrs(hps, PRIM_AREA, LBB_BACK_COLOR, 0L, 
                             (PBUNDLE)&aBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs areaColor %d ERROR %x\n", aBundle.lColor,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs areaColor %d OK\n", aBundle.lColor);
            }
#endif
            rc = WinCreateCursor(((OS2Button *)butPtr)->hwnd, xCursor,
	                    yCursor, cxCursor, cyCursor,
	                    CURSOR_HALFTONE | CURSOR_FRAME, NULL);
#ifdef VERBOSE
{
SWP pos;
WinQueryWindowPos(((OS2Button *)butPtr)->hwnd, &pos);
            if (rc!=TRUE) {
      printf("WinCreateCursor (%d,%d) %dx%d hwnd %x ((%d,%d) %dx%d) ERROR %x\n",
		       xCursor, yCursor, cxCursor, cyCursor,
                       ((OS2Button *)butPtr)->hwnd,
                       pos.x, pos.y, pos.cx, pos.cy,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
      printf("WinCreateCursor (%d,%d) %dx%d hwnd %x ((%d,%d) %dx%d) OK\n",
		       xCursor, yCursor, cxCursor, cyCursor,
		       ((OS2Button *)butPtr)->hwnd,
                       pos.x, pos.y, pos.cx, pos.cy);
            }
}
#endif
            rc = WinShowCursor(((OS2Button *)butPtr)->hwnd, TRUE);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("WinShowCursor hwnd %x ERROR %x\n",
		       ((OS2Button *)butPtr)->hwnd,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("WinShowCursor hwnd %x OK\n",
		       ((OS2Button *)butPtr)->hwnd);
            }
#endif
            cBundle.lColor = oldColor;
            rc = GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
            aBundle.lBackColor = oldBackColor;
            rc = GpiSetAttrs(hps, PRIM_AREA, LBB_BACK_COLOR, 0L, 
                             (PBUNDLE)&aBundle);
	    TkOS2ReleaseDrawablePS(pixmap, hps, &state);
	}
	y += butPtr->textHeight/2;
    }

    /*
     * Draw the indicator for check buttons and radio buttons.  At this
     * point x and y refer to the top-left corner of the text or image
     * or bitmap.
     */

    if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn
	    && tsdPtr->hBoxes) {
	int xSrc, ySrc;
	POINTL points[4];
#ifdef VERBOSE
        int i;
#endif

	x -= butPtr->indicatorSpace;
	y -= butPtr->indicatorDiameter / 2;

	xSrc = (butPtr->flags & SELECTED) ? tsdPtr->boxWidth : 0;
	if (butPtr->state == STATE_ACTIVE) {
	    xSrc += tsdPtr->boxWidth*2;
	}
	ySrc = (butPtr->type == TYPE_RADIO_BUTTON) ? 0 : tsdPtr->boxHeight;
		
	/*
	 * Update the palette in the boxes bitmap to reflect the current
	 * button colors.  Note that this code relies on the layout of the
	 * bitmap's palette.  Also, all of the colors used to draw the
	 * bitmap must be in the palette that is selected into the DC of
	 * the offscreen pixmap.  This requires that the static colors
	 * be placed into the palette.
	 */


	tsdPtr->palTable[PAL_CHECK] =  gc->foreground;
	tsdPtr->palTable[PAL_TOP_OUTER] = TkOS2GetBorderPixels(tkwin, border,
                                                               TK_3D_DARK_GC);
	tsdPtr->palTable[PAL_TOP_INNER] = TkOS2GetBorderPixels(tkwin, border,
                                                               TK_3D_DARK2);
	tsdPtr->palTable[PAL_BOTTOM_INNER] = TkOS2GetBorderPixels(tkwin, border,
                                                                  TK_3D_LIGHT2);
	tsdPtr->palTable[PAL_BOTTOM_OUTER] = TkOS2GetBorderPixels(tkwin, border,
                                                                TK_3D_LIGHT_GC);

	if (butPtr->state == STATE_DISABLED) {
	    tsdPtr->palTable[PAL_INTERIOR] = TkOS2GetBorderPixels(tkwin, border,
                                                                  TK_3D_LIGHT2);
	} else if (butPtr->selectBorder != NULL) {
	    tsdPtr->palTable[PAL_INTERIOR] = TkOS2GetBorderPixels(tkwin, border,
                                                                 TK_3D_FLAT_GC);
	} else {
            tsdPtr->palTable[PAL_INTERIOR] = WinQuerySysColor(HWND_DESKTOP,
                                                              SYSCLR_WINDOW,0L);
	}
	tsdPtr->palTable[PAL_BACKGROUND] = TkOS2GetBorderPixels(tkwin, border,
                                                                TK_3D_FLAT_GC);
#ifdef VERBOSE
        printf("colors C %x fg %x TO %x TI %x BI %x BO %x I %x B %x\n",
               tsdPtr->palTable[PAL_CHECK], gc->foreground,
               tsdPtr->palTable[PAL_TOP_OUTER],
               tsdPtr->palTable[PAL_TOP_INNER],
               tsdPtr->palTable[PAL_BOTTOM_INNER],
               tsdPtr->palTable[PAL_BOTTOM_OUTER],
               tsdPtr->palTable[PAL_INTERIOR],
               tsdPtr->palTable[PAL_BACKGROUND]);
#endif

        rc = GpiSetPaletteEntries(tsdPtr->boxesPalette, LCOLF_CONSECRGB, 0L,
                                  16L, tsdPtr->palTable);
#ifdef VERBOSE
        if (tsdPtr->boxesPalette == GPI_ERROR) {
            printf("    GpiSetPaletteEntries ERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("    GpiSetPaletteEntries OK\n");
        }
        for (i= 0; i<16; i++) {
            printf("palTable[%d] [%x]\n", i, tsdPtr->palTable[i]);
        }
#endif

        /* Rewrite boxes bitmap with this palette info */
        tsdPtr->boxesPtr->cbFix = sizeof(BITMAPINFOHEADER2);
        rc = GpiSetBitmapBits(tsdPtr->boxesPS, 0, tsdPtr->boxesPtr->cy,
                              tsdPtr->boxesBits, tsdPtr->boxesPtr);
#ifdef VERBOSE
        if (rc == GPI_ALTERROR) {
	    printf("GpiSetBitmapBits %x for %d lines ERROR %x\n",
                   tsdPtr->boxesPS, tsdPtr->boxesPtr->cy,
                   WinGetLastError(TclOS2GetHAB()));
	} else {
	    printf("GpiSetBitmapBits OK set %d scanlines\n", rc);
	}
        printf("boxesPtr->cx %d, boxesPtr->cy %d\n", tsdPtr->boxesPtr->cx,
               tsdPtr->boxesPtr->cy);
#endif

	hps = TkOS2GetDrawablePS(butPtr->display, pixmap, &state);

	/* Target, bottom left */
	points[0].x = x;
	points[0].y = y;
	/* Target, top right */
	points[1].x = x + tsdPtr->boxWidth;
	points[1].y = y + tsdPtr->boxHeight;
	/* Source, bottom left */
	points[2].x = xSrc;
	points[2].y = ySrc;
	/* Source, top right */
	points[3].x = xSrc + tsdPtr->boxWidth;
	points[3].y = ySrc + tsdPtr->boxHeight;

/*
	rc = GpiWCBitBlt(hps, tsdPtr->hBoxes, 4, points, ROP_SRCCOPY,
                         BBO_IGNORE);
*/
	rc = GpiBitBlt(hps, tsdPtr->boxesPS, 4, points, ROP_SRCCOPY,
                       BBO_IGNORE);
#ifdef VERBOSE
        if (rc == GPI_ERROR) {
	    printf("GpiWCBitBlt (%d,%d)-(%d,%d) <= (%d,%d)-(%d,%d) ERROR %x\n",
	           points[0].x, points[0].y, points[1].x, points[1].y,
	           points[2].x, points[2].y, points[3].x, points[3].y,
		   WinGetLastError(TclOS2GetHAB()));
	} else {
	    printf("GpiWCBitBlt (%d,%d)-(%d,%d) <= (%d,%d)-(%d,%d) OK/HIT %x\n",
	           points[0].x, points[0].y, points[1].x, points[1].y,
	           points[2].x, points[2].y, points[3].x, points[3].y,
		   rc);
	}
#endif
	TkOS2ReleaseDrawablePS(pixmap, hps, &state);
    }

    /*
     * If the button is disabled with a stipple rather than a special
     * foreground color, generate the stippled effect.  If the widget
     * is selected and we use a different background color when selected,
     * must temporarily modify the GC.
     */

    if ((butPtr->state == STATE_DISABLED)
	    && ((butPtr->disabledFg == NULL) || (butPtr->image != NULL))) {
	if ((butPtr->flags & SELECTED) && !butPtr->indicatorOn
		&& (butPtr->selectBorder != NULL)) {
	    XSetForeground(butPtr->display, butPtr->disabledGC,
		    Tk_3DBorderColor(butPtr->selectBorder)->pixel);
	}
	XFillRectangle(butPtr->display, pixmap, butPtr->disabledGC,
		butPtr->inset, butPtr->inset,
		(unsigned) (Tk_Width(tkwin) - 2*butPtr->inset),
		(unsigned) (Tk_Height(tkwin) - 2*butPtr->inset));
	if ((butPtr->flags & SELECTED) && !butPtr->indicatorOn
		&& (butPtr->selectBorder != NULL)) {
	    XSetForeground(butPtr->display, butPtr->disabledGC,
		    Tk_3DBorderColor(butPtr->normalBorder)->pixel);
	}
    }

    /*
     * Draw the border and traversal highlight last.  This way, if the
     * button's contents overflow they'll be covered up by the border.
     */

    if (relief != TK_RELIEF_FLAT) {
	Tk_Draw3DRectangle(tkwin, pixmap, border,
		defaultWidth, defaultWidth,
		Tk_Width(tkwin) - 2*defaultWidth,
		Tk_Height(tkwin) - 2*defaultWidth,
		butPtr->borderWidth, relief);
    }
    if (defaultWidth != 0) {
        LONG windowHeight = TkOS2WindowHeight((TkOS2Drawable *)pixmap);
#ifdef VERBOSE
        printf("TkpDisplayButton: wh %d (%d,%d),(%d,%d),(%d,%d),(%d,%d)\n",
               windowHeight, 0, 0, 0, 0, 0, Tk_Height(tkwin) - defaultWidth,
               Tk_Width(tkwin) - defaultWidth, 0);
#endif
	hps = TkOS2GetDrawablePS(butPtr->display, pixmap, &state);
	TkOS2FillRect(hps, 0, windowHeight - defaultWidth,
                      Tk_Width(tkwin), defaultWidth,
		      butPtr->highlightColorPtr->pixel);
	TkOS2FillRect(hps, 0, windowHeight - Tk_Height(tkwin),
                      defaultWidth, Tk_Height(tkwin),
		      butPtr->highlightColorPtr->pixel);
	TkOS2FillRect(hps, 0, windowHeight - Tk_Height(tkwin) - defaultWidth,
		      Tk_Width(tkwin), defaultWidth,
		      butPtr->highlightColorPtr->pixel);
	TkOS2FillRect(hps, Tk_Width(tkwin) - defaultWidth,
                      windowHeight - Tk_Height(tkwin), defaultWidth,
                      Tk_Height(tkwin), butPtr->highlightColorPtr->pixel);
	TkOS2ReleaseDrawablePS(pixmap, hps, &state);
    }

    /*
     * Copy the information from the off-screen pixmap onto the screen,
     * then delete the pixmap.
     */

    XCopyArea(butPtr->display, pixmap, Tk_WindowId(tkwin),
	    butPtr->copyGC, 0, 0, (unsigned) Tk_Width(tkwin),
	    (unsigned) Tk_Height(tkwin), 0, 0);
    WinDestroyCursor(((OS2Button *)butPtr)->hwnd);
    Tk_FreePixmap(butPtr->display, pixmap);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpComputeButtonGeometry --
 *
 *	After changes in a button's text or bitmap, this procedure
 *	recomputes the button's geometry and passes this information
 *	along to the geometry manager for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The button's window may change size.
 *
 *----------------------------------------------------------------------
 */

void
TkpComputeButtonGeometry(butPtr)
    register TkButton *butPtr;	/* Button whose geometry may have changed. */
{
    int width, height, avgWidth;
    Tk_FontMetrics fm;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (butPtr->highlightWidth < 0) {
	butPtr->highlightWidth = 0;
    }
    butPtr->inset = butPtr->highlightWidth + butPtr->borderWidth;
    butPtr->indicatorSpace = 0;

    if (!tsdPtr->hBoxes) {
	InitBoxes();
    }

    if (butPtr->image != NULL) {
	Tk_SizeOfImage(butPtr->image, &width, &height);
	imageOrBitmap:
	if (butPtr->width > 0) {
	    width = butPtr->width;
	}
	if (butPtr->height > 0) {
	    height = butPtr->height;
	}
	if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
	    butPtr->indicatorSpace = tsdPtr->boxWidth * 2;
	    butPtr->indicatorDiameter = tsdPtr->boxHeight;
	}
    } else if (butPtr->bitmap != None) {
	Tk_SizeOfBitmap(butPtr->display, butPtr->bitmap, &width, &height);
	goto imageOrBitmap;
    } else {
	Tk_FreeTextLayout(butPtr->textLayout);
	butPtr->textLayout = Tk_ComputeTextLayout(butPtr->tkfont,
		Tcl_GetString(butPtr->textPtr), -1, butPtr->wrapLength,
                butPtr->justify, 0, &butPtr->textWidth, &butPtr->textHeight);

	width = butPtr->textWidth;
	height = butPtr->textHeight;
	avgWidth = Tk_TextWidth(butPtr->tkfont, "0", 1);
	Tk_GetFontMetrics(butPtr->tkfont, &fm);

	if (butPtr->width > 0) {
	    width = butPtr->width * avgWidth;
	}
	if (butPtr->height > 0) {
	    height = butPtr->height * fm.linespace;
	}

	if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
	    butPtr->indicatorDiameter = tsdPtr->boxHeight;
	    butPtr->indicatorSpace = butPtr->indicatorDiameter + avgWidth;
	}

	/*
	 * Increase the inset to allow for the focus ring.
	 */

	if (butPtr->type != TYPE_LABEL) {
	    butPtr->inset += 3;
	}
    }

    /*
     * When issuing the geometry request, add extra space for the indicator,
     * if any, and for the border and padding, plus an extra pixel so the
     * display can be offset by 1 pixel in either direction for the raised
     * or lowered effect.
     */

    if ((butPtr->image == NULL) && (butPtr->bitmap == None)) {
	width += 2*butPtr->padX;
	height += 2*butPtr->padY;
    }
    if ((butPtr->type == TYPE_BUTTON)
	    || ((butPtr->type >= TYPE_CHECK_BUTTON) && !butPtr->indicatorOn)) {
	width += 1;
	height += 1;
    }
    Tk_GeometryRequest(butPtr->tkwin, (int) (width + butPtr->indicatorSpace
	    + 2*butPtr->inset), (int) (height + 2*butPtr->inset));
    Tk_SetInternalBorder(butPtr->tkwin, butPtr->inset);
}

/*
 *----------------------------------------------------------------------
 *
 * ButtonProc --
 *
 *	This function is called by OS/2 PM whenever an event occurs on
 *	a button control created by Tk.
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
ButtonProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    MRESULT result;
    OS2Button *butPtr;
    Tk_Window tkwin = Tk_HWNDToWindow(hwnd);

#ifdef VERBOSE
    printf("ButtonProc hwnd %x (tkwin %x), msg %x, p1 %x, p2 %x\n", hwnd, tkwin,
           message, param1, param2);
    switch (message) {
        case BM_QUERYCHECK: printf("Btn: BM_QUERYCHECK\n"); break;
        case BM_QUERYHILITE: printf("Btn: BM_QUERYHILITE\n"); break;
        case WM_ACTIVATE: printf("Btn: WM_ACTIVATE\n"); break;
        case WM_ADJUSTFRAMEPOS: printf("Btn: WM_ADJUSTFRAMEPOS\n"); break;
        case WM_ADJUSTWINDOWPOS: printf("Btn: WM_ADJUSTWINDOWPOS\n"); break;
        case WM_BUTTON1DOWN: printf("Btn: WM_BUTTON1DOWN\n"); break;
        case WM_BUTTON1UP: printf("Btn: WM_BUTTON1UP\n"); break;
        case WM_BUTTON1DBLCLK: printf("Btn: WM_BUTTON1DBLCLK\n"); break;
        case WM_BUTTON2DOWN: printf("Btn: WM_BUTTON2DOWN\n"); break;
        case WM_BUTTON2UP: printf("Btn: WM_BUTTON2UP\n"); break;
        case WM_BUTTON2DBLCLK: printf("Btn: WM_BUTTON2DBLCLK\n"); break;
        case WM_BUTTON3DOWN: printf("Btn: WM_BUTTON3DOWN\n"); break;
        case WM_BUTTON3UP: printf("Btn: WM_BUTTON3UP\n"); break;
        case WM_BUTTON3DBLCLK: printf("Btn: WM_BUTTON3DBLCLK\n"); break;
        case WM_CALCFRAMERECT: printf("Btn: WM_CALCFRAMERECT\n"); break;
        case WM_CALCVALIDRECTS: printf("Btn: WM_CALCVALIDRECTS\n"); break;
        case WM_CLOSE: printf("Btn: WM_CLOSE\n"); break;
        case WM_COMMAND: printf("Btn: WM_COMMAND\n"); break;
        case WM_CREATE: printf("Btn: WM_CREATE\n"); break;
        case WM_ERASEBACKGROUND: printf("Btn: WM_ERASEBACKGROUND\n"); break;
        case WM_FOCUSCHANGE: printf("Btn: WM_FOCUSCHANGE\n"); break;
        case WM_FORMATFRAME: printf("Btn: WM_FORMATFRAME\n"); break;
        case WM_MINMAXFRAME: printf("Btn: WM_MINMAXFRAME\n"); break;
        case WM_MOUSEMOVE: printf("Btn: WM_MOUSEMOVE\n"); break;
        case WM_MOVE: printf("Btn: WM_MOVE\n"); break;
        case WM_OWNERPOSCHANGE: printf("Btn: WM_OWNERPOSCHANGE\n"); break;
        case WM_PAINT: printf("Btn: WM_PAINT\n"); break;
        case WM_QUERYBORDERSIZE: printf("Btn: WM_QUERYBORDERSIZE\n"); break;
        case WM_QUERYDLGCODE: printf("Btn: WM_QUERYDLGCODE\n"); break;
        case WM_QUERYFRAMECTLCOUNT: printf("Btn: WM_QUERYFRAMECTLCOUNT\n"); break;
        case WM_QUERYFOCUSCHAIN: printf("Btn: WM_QUERYFOCUSCHAIN\n"); break;
        case WM_QUERYICON: printf("Btn: WM_QUERYICON\n"); break;
        case WM_QUERYTRACKINFO: printf("Btn: WM_QUERYTRACKINFO\n"); break;
        case WM_REALIZEPALETTE: printf("Btn: WM_REALIZEPALETTE\n"); break;
        case WM_SETFOCUS: printf("Btn: WM_SETFOCUS\n"); break;
        case WM_SETSELECTION: printf("Btn: WM_SETSELECTION\n"); break;
        case WM_UPDATEFRAME: printf("Btn: WM_UPDATEFRAME\n"); break;
        case WM_WINDOWPOSCHANGED: printf("Btn: WM_WINDOWPOSCHANGED\n"); break;
    }
#endif

    /* tkwin can be NULL for WM_DESTROY due to the order of messages */
    if (message == WM_DESTROY) {
#ifdef VERBOSE
        printf("ButtonProc: WM_DESTROY\n");
#endif
        return WinDefWindowProc(hwnd, message, param1, param2);
    }
    if (tkwin == NULL) {
#ifdef VERBOSE
        printf("panicking...\n");
	fflush(stdout);
#endif
	panic("ButtonProc called on an invalid HWND");
    }
    butPtr = (OS2Button *)((TkWindow*)tkwin)->instanceData;

    switch(message) {

        case WM_COMMAND: {
	    USHORT usCmd, usSource, usPointer;
	    usCmd = SHORT1FROMMP(param1);
	    usSource = SHORT1FROMMP(param2);
	    usPointer = SHORT2FROMMP(param2);
#ifdef VERBOSE
            printf("    WM_COMMAND usCmd 0x%x, usSource 0x%x, usPointer 0x%x\n",
                   usCmd, usSource, usPointer);
#endif
            break;
        }

        case WM_CONTROL: {
	    USHORT id, usNotify;
	    id = SHORT1FROMMP(param1);
	    usNotify = SHORT2FROMMP(param1);
#ifdef VERBOSE
            printf("    WM_CONTROL id 0x%x usNotify 0x%x\n", id, usNotify);
#endif
            switch (usNotify) {
                /* BN_PAINT -> redraw button */
	        case BN_PAINT: {
#ifdef VERBOSE
		    PUSERBUTTON ubPtr = (PUSERBUTTON) LONGFROMMP(param2);
                    printf("    BN_PAINT, hwnd %x, hps %x, state %x, old %x\n",
                           ubPtr->hwnd, ubPtr->hps, ubPtr->fsState,
                           ubPtr->fsStateOld);
#endif
	            TkpDisplayButton((ClientData)butPtr);

                    /*
                     * Special note: must cancel any existing idle handler
                     * for TkpDisplayButton;  it's no longer needed, and
                     * TkpDisplayButton cleared the REDRAW_PENDING flag.
                     */

                    Tcl_CancelIdleCall(TkpDisplayButton, (ClientData)butPtr);
	            return 0;
	        }

	        case BN_CLICKED: {
	            int code;
	            Tcl_Interp *interp = butPtr->info.interp;
#ifdef VERBOSE
                    printf("    BN_CLICKED\n");
#endif
	            if (butPtr->info.state != STATE_DISABLED) {
		        Tcl_Preserve((ClientData)interp);
		        code = TkInvokeButton((TkButton*)butPtr);
		        if (code != TCL_OK && code != TCL_CONTINUE
			        && code != TCL_BREAK) {
		            Tcl_AddErrorInfo(interp, "\n    (button invoke)");
		            Tcl_BackgroundError(interp);
		        }
		        Tcl_Release((ClientData)interp);
	            }
	            Tcl_ServiceAll();
	            return 0;
	        }

	        case BN_DBLCLICKED: {
#ifdef VERBOSE
                    printf("    BN_DBLCLICKED\n");
#endif
	            if (Tk_TranslateOS2Event(hwnd, message, param1, param2,
		                             &result)) {
		        return result;
	            }
	        }
	    }
	}

	case WM_ERASEBACKGROUND:
	    /*
	     * Return FALSE if the application processes the message.
	     * If TRUE is returned the client area is filled with the
	     * window background color.
	     */
	    return 0;

	case BM_QUERYHILITE: {
	    BOOL hasFocus = FALSE;
	    if (butPtr->info.flags & GOT_FOCUS) {
		hasFocus = BST_FOCUS;
	    }
	    return (MRESULT)hasFocus;
	}
	 
	case BM_QUERYCHECK: {
	    ULONG state = BST_INDETERMINATE;
	    if (((butPtr->info.type == TYPE_CHECK_BUTTON)
		    || (butPtr->info.type == TYPE_RADIO_BUTTON))
		    && butPtr->info.indicatorOn) {
		state = (butPtr->info.flags & SELECTED)
		    ? BST_CHECKED : BST_UNCHECKED;
	    }
	    return (MRESULT)state;
	}
	 
	case WM_ENABLE:
	    break;

        case WM_FOCUSCHANGE:
            if (SHORT1FROMMP(param2) != TRUE) {
                WinDestroyCursor(hwnd);
#ifdef VERBOSE
                printf("BtnProc: WM_FOCUSCHANGE FALSE hwnd %x\n", hwnd);
#endif
            }
            break;
/*
*/

	case WM_PAINT: {
	    HPS hps;
#ifdef VERBOSE
            RECTL rectl;
#endif

	    hps = WinBeginPaint(hwnd, NULLHANDLE, NULL);
#ifdef VERBOSE
            printf("BtnProc: WM_PAINT\n");
WinFillRect(hps, &rectl, CLR_BLUE);
#endif
	    WinEndPaint(hps);
	    TkpDisplayButton((ClientData)butPtr);
	    return 0;
	}

	default:
	    if (Tk_TranslateOS2Event(hwnd, message, param1, param2, &result)) {
		return result;
	    }
    }
    return butPtr->oldProc(hwnd, message, param1, param2);
}

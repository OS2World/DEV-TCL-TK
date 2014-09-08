/* 
 * tkOS2Draw.c --
 *
 *	This file contains the Xlib emulation functions pertaining to
 *	actually drawing objects on a window.
 *
 * Copyright (c) 1994 Software Research Associates, Inc.
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"

#define PI 3.14159265358979
#define XAngleToRadians(a) ((double)(a) / 64 * PI / 180)

/*
 * Translation tables between X gc values and OS/2 GPI line attributes.
 */

static int lineStyles[] = {
    LINETYPE_SOLID,	/* LineSolid */
    LINETYPE_SHORTDASH,	/* LineOnOffDash */
    LINETYPE_SHORTDASH	/* LineDoubleDash EXTRA PROCESSING NECESSARY */
};
static int capStyles[] = {
    LINEEND_FLAT,	/* CapNotLast EXTRA PROCESSING NECESSARY */
    LINEEND_FLAT,	/* CapButt */
    LINEEND_ROUND,	/* CapRound */
    LINEEND_SQUARE	/* CapProjecting */
};
static int joinStyles[] = {
    LINEJOIN_MITRE,	/* JoinMiter */
    LINEJOIN_ROUND,	/* JoinRound */
    LINEJOIN_BEVEL	/* JoinBevel */
};

/*
 * Translation table between X gc functions and OS/2 GPI mix attributes.
 */

int tkpOS2MixModes[] = {
    FM_ZERO,			/* GXclear */
    FM_AND,			/* GXand */
    FM_MASKSRCNOT,		/* GXandReverse */
    FM_OVERPAINT,		/* GXcopy */
    FM_SUBTRACT,		/* GXandInverted */
    FM_LEAVEALONE,		/* GXnoop */
    FM_XOR,			/* GXxor */
    FM_OR,			/* GXor */
    FM_NOTMERGESRC,		/* GXnor */
    FM_NOTXORSRC,		/* GXequiv */
    FM_INVERT,			/* GXinvert */
    FM_MERGESRCNOT,		/* GXorReverse */
    FM_NOTCOPYSRC,		/* GXcopyInverted */
    FM_MERGENOTSRC,		/* GXorInverted */
    FM_NOTMASKSRC,		/* GXnand */
    FM_ONE			/* GXset */
};


/*
 * Translation table between X gc functions and OS/2 GPI BitBlt raster op modes.
 * Some of the operations defined in X don't have names, so we have to construct
 * new opcodes for those functions.  This is arcane and probably not all that
 * useful, but at least it's accurate.
 */

#define NOTSRCAND	(LONG)0x0022 /* dest = (NOT source) AND dest */
#define NOTSRCINVERT	(LONG)0x0099 /* dest = (NOT source) XOR dest */
#define SRCORREVERSE	(LONG)0x00dd /* dest = source OR (NOT dest) */
#define SRCNAND		(LONG)0x0077 /* dest = (NOT source) OR (NOT dest) */

static int bltModes[] = {
    ROP_ZERO,			/* GXclear */
    ROP_SRCAND,			/* GXand */
    ROP_SRCERASE,		/* GXandReverse */
    ROP_SRCCOPY,		/* GXcopy */
    NOTSRCAND,			/* GXandInverted */
    ROP_PATCOPY,		/* GXnoop */
    ROP_SRCINVERT,		/* GXxor */
    ROP_SRCPAINT,		/* GXor */
    ROP_NOTSRCERASE,		/* GXnor */
    NOTSRCINVERT,		/* GXequiv */
    ROP_DSTINVERT,		/* GXinvert */
    SRCORREVERSE,		/* GXorReverse */
    ROP_NOTSRCCOPY,		/* GXcopyInverted */
    ROP_MERGEPAINT,		/* GXorInverted */
    SRCNAND,			/* GXnand */
    ROP_ONE			/* GXset */
};

/*
 * The following raster op uses the source bitmap as a mask for the
 * pattern.  This is used to draw in a foreground color but leave the
 * background color transparent.
 * dest = (src & pat) | (!src & dst)
 * pattern source target(i) target(final)
 *    0      0       0         0
 *    0      0       1         1
 *    0      1       0         0
 *    0      1       1         0
 *    1      0       0         0
 *    1      0       1         1
 *    1      1       0         1
 *    1      1       1         1
 * => 11100010 = e2
 */

/*#define MASKPAT		0x00e2 /* dest = (src & pat) | (!src & dst) */
#define MASKPAT		0x0099 /* dest = (src & pat) | (!src & dst) */

/*
 * The following two raster ops are used to copy the foreground and background
 * bits of a source pattern as defined by a stipple used as the pattern.
 */

#define COPYFG		0x00ca /* dest = (pat & src) | (!pat & dst) */
#define COPYBG		0x00ac /* dest = (!pat & src) | (pat & dst) */

/*
 * Macros used later in the file.
 */

#ifndef MIN
#define MIN(a,b)	((a>b) ? b : a)
#endif
#ifndef MAX
#define MAX(a,b)	((a<b) ? b : a)
#endif

/* All attributes of a line */
#define LINE_ATTRIBUTES (LBB_COLOR | LBB_BACK_COLOR | \
                         LBB_MIX_MODE | LBB_BACK_MIX_MODE | \
                         LBB_WIDTH | LBB_GEOM_WIDTH | \
                         LBB_TYPE | LBB_END | LBB_JOIN)
#define AREA_ATTRIBUTES (ABB_COLOR | ABB_BACK_COLOR | \
                         ABB_MIX_MODE | ABB_BACK_MIX_MODE | \
                         ABB_SET | ABB_SYMBOL | ABB_REF_POINT)

typedef struct ThreadSpecificData {
    POINTL *os2Points;	/* Array of points that is reused. */
    int nOS2Points;	/* Current size of point array. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Forward declarations for procedures defined in this file:
 */

static POINTL *		ConvertPoints (Drawable d, XPoint *points, int npoints,
			    int mode, RECTL *bbox);
static void		DrawOrFillArc (Display *display,
			    Drawable d, GC gc, int x, int y,
			    unsigned int width, unsigned int height,
			    int start, int extent, int fill);
static void		RenderObject (HPS hps, GC gc, Drawable d,
                            XPoint* points, int npoints, int mode,
                            PLINEBUNDLE linePtr, PAREABUNDLE areaPtr, int func);
static BOOL             SetUpGraphicsPort _ANSI_ARGS_((HPS hps, GC gc,
                            PLINEBUNDLE oldLineBundle,
                            PLINEBUNDLE newLineBundle,
                            PAREABUNDLE oldAreaBundle,
                            PAREABUNDLE newAreaBundle));

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetDrawablePS --
 *
 *	Retrieve the Presentation Space from a drawable.
 *
 * Results:
 *	Returns the window PS for windows.  Returns the associated memory PS
 *	for pixmaps.
 *
 * Side effects:
 *	Sets up the palette for the presentation space, and saves the old
 *	presentation space state in the passed in TkOS2PSState structure.
 *
 *----------------------------------------------------------------------
 */

HPS
TkOS2GetDrawablePS(display, d, state)
    Display *display;
    Drawable d;
    TkOS2PSState* state;
{
    HPS hps;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;
    Colormap cmap;

    if (todPtr->type == TOD_WINDOW) {
        TkWindow *winPtr = todPtr->window.winPtr;

        if (todPtr->window.handle == NULLHANDLE) {
            hps = WinGetPS(HWND_DESKTOP);
        } else {
            hps = WinGetPS(todPtr->window.handle);
        }
#ifdef VERBOSE
        printf("Draw:TkOS2GetDrawablePS window %x (hwnd %x, hps %x)\n", todPtr,
               todPtr->window.handle, hps);
#endif
        if (winPtr == NULL) {
            cmap = DefaultColormap(display, DefaultScreen(display));
        } else {
            cmap = winPtr->atts.colormap;
        }
        state->palette = TkOS2SelectPalette(hps, todPtr->window.handle, cmap);
    } else if (todPtr->type == TOD_OS2PS) {
        hps = todPtr->os2PS.hps;
#ifdef VERBOSE
        printf("Draw:TkOS2GetDrawablePS todPtr %x (os2PS.hps %x)\n", todPtr,
               hps);
#endif
        cmap = DefaultColormap(display, DefaultScreen(display));
    } else {

        hps = todPtr->bitmap.hps;
#ifdef VERBOSE
        printf("Draw:TkOS2GetDrawablePS bitmap %x (handle %x, hps %x)\n", d,
               todPtr->bitmap.handle, hps);
#endif
        cmap = todPtr->bitmap.colormap;
        state->palette = TkOS2SelectPalette(hps, todPtr->bitmap.parent, cmap);
    }
    state->backMix = GpiQueryBackMix(hps);
    return hps;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ReleaseDrawablePS --
 *
 *	Frees the resources associated with a drawable's DC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the old bitmap handle to the memory DC for pixmaps.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2ReleaseDrawablePS(d, hps, state)
    Drawable d;
    HPS hps;
    TkOS2PSState *state;
{
    ULONG changed;
    HPAL oldPal;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;

    rc= GpiSetBackMix(hps, state->backMix);
    if (todPtr->type == TOD_WINDOW) {
        oldPal = GpiSelectPalette(hps, state->palette);
#ifdef VERBOSE
        printf("Draw:TkOS2ReleaseDrawablePS window %x\n", d);
        if (oldPal == PAL_ERROR) {
            printf("Draw:GpiSelectPalette TkOS2ReleaseDrawablePS pal %x ERROR: %x\n",
                   oldPal, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSelectPalette TkOS2ReleaseDrawablePS: %x\n", oldPal);
        }
#endif
/*
        WinRealizePalette(TkOS2GetHWND(d), hps, &changed);
*/
        WinReleasePS(hps);
    } else if (todPtr->type == TOD_BITMAP) {
        oldPal = GpiSelectPalette(hps, state->palette);
#ifdef VERBOSE
        printf("Draw:TkOS2ReleaseDrawablePS bitmap %x released %x\n", d,
               state->bitmap);
        if (oldPal == PAL_ERROR) {
            printf("Draw:GpiSelectPalette TkOS2ReleaseDrawablePS pal %x ERROR: %x\n",
                   oldPal, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSelectPalette TkOS2ReleaseDrawablePS: %x\n", oldPal);
        }
#endif
        WinRealizePalette(todPtr->bitmap.parent, hps, &changed);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertPoints --
 *
 *	Convert an array of X points to an array of OS/2 GPI points.
 *
 * Results:
 *	Returns the converted array of POINTLs.
 *
 * Side effects:
 *      Allocates a block of memory in thread local storage that
 *      should not be freed.
 *
 *----------------------------------------------------------------------
 */

static POINTL *
ConvertPoints(d, points, npoints, mode, bbox)
    Drawable d;
    XPoint *points;
    int npoints;
    int mode;			/* CoordModeOrigin or CoordModePrevious. */
    RECTL *bbox;			/* Bounding box of points. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    LONG windowHeight;
    int i;

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

#ifdef VERBOSE
    printf("Draw:ConvertPoints %x, %s, windowHeight %hd\n", d,
           mode == CoordModeOrigin ? "CoordModeOrigin": "CoordModePrevious",
           windowHeight);
#endif

    /*
     * To avoid paying the cost of a malloc on every drawing routine,
     * we reuse the last array if it is large enough.
     */

    if (npoints > tsdPtr->nOS2Points) {
        if (tsdPtr->os2Points != NULL) {
            ckfree((char *) tsdPtr->os2Points);
        }
        tsdPtr->os2Points = (POINTL *) ckalloc(sizeof(POINTL) * npoints);
        if (tsdPtr->os2Points == NULL) {
            tsdPtr->nOS2Points = -1;
            return NULL;
        }
        tsdPtr->nOS2Points = npoints;
    }

    /* Convert to PM Coordinates */
    bbox->xLeft = bbox->xRight = points[0].x;
    bbox->yTop = bbox->yBottom = windowHeight - points[0].y;
    
    if (mode == CoordModeOrigin) {
#ifdef VERBOSE
        printf("Draw:   CMO points ");
#endif
        for (i = 0; i < npoints; i++) {
            tsdPtr->os2Points[i].x = points[i].x;
            /* convert to PM */
            tsdPtr->os2Points[i].y = windowHeight - points[i].y;
            bbox->xLeft = MIN(bbox->xLeft, tsdPtr->os2Points[i].x);
            /* Since GpiBitBlt excludes top & right, add one */
            bbox->xRight = MAX(bbox->xRight, tsdPtr->os2Points[i].x + 1);
            /* y: min and max switched for PM */
            bbox->yTop = MAX(bbox->yTop, tsdPtr->os2Points[i].y + 1);
            bbox->yBottom = MIN(bbox->yBottom, tsdPtr->os2Points[i].y);
#ifdef VERBOSE
            printf("(%d,%d) ", tsdPtr->os2Points[i].x, tsdPtr->os2Points[i].y);
#endif
        }
#ifdef VERBOSE
        printf("\nDraw:   CMO bbox (%d,%d)-(%d,%d)\n",
                   bbox->xLeft, bbox->yBottom, bbox->xRight, bbox->yTop);
#endif
    } else {
        /* CoordModePrevious */
        tsdPtr->os2Points[0].x = points[0].x;
        tsdPtr->os2Points[0].y = windowHeight - points[0].y;
#ifdef VERBOSE
        printf("Draw:   CMP points (%d,%d) ", tsdPtr->os2Points[0].x, tsdPtr->os2Points[0].y);
#endif
        for (i = 1; i < npoints; i++) {
            tsdPtr->os2Points[i].x = tsdPtr->os2Points[i-1].x + points[i].x;
            /* convert to PM, y is offset */
            tsdPtr->os2Points[i].y = tsdPtr->os2Points[i-1].y - points[i].y;
            bbox->xLeft = MIN(bbox->xLeft, tsdPtr->os2Points[i].x);
            /* Since GpiBitBlt excludes top & right, add one */
            bbox->xRight = MAX(bbox->xRight, tsdPtr->os2Points[i].x + 1);
            /* y: min and max switched for PM */
            bbox->yTop = MAX(bbox->yTop, tsdPtr->os2Points[i].y + 1);
            bbox->yBottom = MIN(bbox->yBottom, tsdPtr->os2Points[i].y);
#ifdef VERBOSE
            printf("(%d,%d) ", tsdPtr->os2Points[i].x, tsdPtr->os2Points[i].y);
#endif
        }
#ifdef VERBOSE
        printf("\nDraw:   CMP bbox (%d,%d)-(%d,%d)\n",
                   bbox->xLeft, bbox->yBottom, bbox->xRight, bbox->yTop);
#endif
    }
    return tsdPtr->os2Points;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copies data from one drawable to another using block transfer
 *	routines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Data is moved from a window or bitmap to a second window or
 *	bitmap.
 *
 *----------------------------------------------------------------------
 */

void
XCopyArea(display, src, dest, gc, src_x, src_y, width, height, dest_x, dest_y)
    Display* display;
    Drawable src;
    Drawable dest;
    GC gc;
    int src_x, src_y;
    unsigned int width, height;
    int dest_x, dest_y;
{
    HPS srcPS, dstPS;
    TkOS2PSState srcState, dstState;
    TkpClipMask *clipPtr = (TkpClipMask*)gc->clip_mask;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    BOOL rc;
    LONG srcWindowHeight, dstWindowHeight;
    HRGN oldReg;

    srcWindowHeight = TkOS2WindowHeight((TkOS2Drawable *)src);
    srcPS = TkOS2GetDrawablePS(display, src, &srcState);
    if (src != dest) {
        dstPS = TkOS2GetDrawablePS(display, dest, &dstState);
        dstWindowHeight = TkOS2WindowHeight((TkOS2Drawable *)dest);
    } else {
        dstPS = srcPS;
        dstWindowHeight = srcWindowHeight;
    }
    /* Bottom-left destination */
    aPoints[0].x = dest_x;
    aPoints[0].y = dstWindowHeight - dest_y - height;
    /* Top-right destination */
    aPoints[1].x = dest_x + width;
    aPoints[1].y = dstWindowHeight - dest_y;
    /* Bottom-left source */
    aPoints[2].x = src_x;
    aPoints[2].y = srcWindowHeight - src_y - height;

#ifdef VERBOSE
    printf("Draw:XCopyArea (%d,%d)->(%d,%d) %dx%d f %x fg %x bg %x (ps %x->%x)\n",
           src_x, src_y, dest_x, dest_y, width, height, gc->function,
           gc->foreground, gc->background, srcPS, dstPS);
    printf("Draw:    PM: (%d,%d)-(%d,%d) <- (%d,%d) wHd %d wHs %d\n",
           aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y,
           aPoints[2].x, aPoints[2].y, aPoints[1].y + dest_y,
           aPoints[2].y + src_y + height);
    rc = GpiRectVisible(dstPS, (PRECTL)&aPoints[0]);
    if (rc==RVIS_PARTIAL || rc==RVIS_VISIBLE) {
        printf("Draw:GpiRectVisible (%d,%d) (%d,%d) (partially) visible\n",
               aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y);
    } else {
        if (rc==RVIS_INVISIBLE) {
            printf("Draw:GpiRectVisible (%d,%d) (%d,%d) invisible\n",
                   aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y);
        } else {
            printf("Draw:GpiRectVisible (%d,%d) (%d,%d) ERROR, error %x\n",
                   aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y,
                   WinGetLastError(TclOS2GetHAB()));
        }
    }
#endif

/* Uncommenting this will make the image2 example work incorrectly every
 * second time. SpecTcl's buttons don't show up correctly.
    if (clipPtr && clipPtr->type == TKP_CLIP_REGION) {
        LONG lComplexity = GpiSetClipRegion(dstPS, (HRGN)clipPtr->value.region,
                                            &oldReg);
        if (lComplexity != RGN_ERROR) {
            POINTL offset;

            offset.x = gc->clip_x_origin;
            offset.y = gc->clip_y_origin;
            rc = GpiOffsetClipRegion(dstPS, &offset);
#ifdef VERBOSE
            if (rc == TRUE) { 
                printf("Draw:GpiOffsetClipRegion %dx%d into %x OK\n", offset.x,
                       offset.y, dstPS);
            } else {
                printf("Draw:GpiOffsetClipRegion %dx%d into %x ERROR %x\n",
                       offset.x, offset.y, dstPS,
                       WinGetLastError(TclOS2GetHAB()));
            }
        } else {
            printf("Draw:GpiSetClipRegion %x into %x RGN_ERROR %x\n",
                   clipPtr->value.region, dstPS,
                   WinGetLastError(TclOS2GetHAB()));
#endif
        }
    }
*/

    rc = GpiBitBlt(dstPS, srcPS, 3, aPoints, bltModes[gc->function],
                   BBO_IGNORE);
#ifdef VERBOSE
    printf("Draw:    srcPS %x, type %s, dstPS %x, type %s\n", srcPS,
           ((TkOS2Drawable *)src)->type == TOD_BITMAP ? "bitmap" : "window",
           dstPS,
           ((TkOS2Drawable *)dest)->type == TOD_BITMAP ? "bitmap" : "window");
    printf("Draw: GpiBitBlt %x -> %x 3 (%d,%d)(%d,%d)(%d,%d), %x returns %d\n",
           srcPS, dstPS, aPoints[0].x, aPoints[0].y,
           aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y,
           bltModes[gc->function], rc);
#endif

    if (clipPtr && clipPtr->type == TKP_CLIP_REGION) {
        GpiSetClipRegion(dstPS, oldReg, &oldReg);
    }
/*
*/

    if (src != dest) {
        TkOS2ReleaseDrawablePS(dest, dstPS, &dstState);
    }
    TkOS2ReleaseDrawablePS(src, srcPS, &srcState);
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copies a bitmap from a source drawable to a destination
 *	drawable.  The plane argument specifies which bit plane of
 *	the source contains the bitmap.  Note that this implementation
 *	ignores the gc->function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the destination drawable.
 *
 *----------------------------------------------------------------------
 */

void
XCopyPlane(display, src, dest, gc, src_x, src_y, width, height, dest_x,
        dest_y, plane)
    Display* display;
    Drawable src;
    Drawable dest;
    GC gc;
    int src_x, src_y;
    unsigned int width, height;
    int dest_x, dest_y;
    unsigned long plane;
{
    HPS srcPS, dstPS;
    TkOS2PSState srcState, dstState;
    TkpClipMask *clipPtr = (TkpClipMask*)gc->clip_mask;
    LONG oldPattern;
    LONG oldMix, oldBackMix;
    LONG oldColor, oldBackColor;
    LONG srcWindowHeight, dstWindowHeight;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    LONG rc;
    HRGN oldReg;
    AREABUNDLE aBundle;

#ifdef VERBOSE
    printf("Draw:XCopyPlane (%d,%d) -> (%d,%d), w %d, h %d; fg %x, bg %x, gc->func %x\n",
           src_x, src_y, dest_x, dest_y, width, height, gc->foreground,
           gc->background, gc->function);
#endif

    /* Translate the Y coordinates to PM coordinates */
    srcPS = TkOS2GetDrawablePS(display, src, &srcState);
    srcWindowHeight = TkOS2WindowHeight((TkOS2Drawable *)src);

    if (src != dest) {
        dstPS = TkOS2GetDrawablePS(display, dest, &dstState);
        dstWindowHeight = TkOS2WindowHeight((TkOS2Drawable *)dest);
    } else {
        dstPS = srcPS;
        dstWindowHeight = srcWindowHeight;
    }
#ifdef VERBOSE
    printf("Draw:srcWindowHeight %d, dstWindowHeight %d\n", srcWindowHeight,
           dstWindowHeight);
#endif
    /* Bottom-left destination */
    aPoints[0].x = dest_x;
    aPoints[0].y = dstWindowHeight - dest_y - height;
    /* Top-right destination */
    aPoints[1].x = dest_x + width;
    aPoints[1].y = dstWindowHeight - dest_y;
    /* Bottom-left source */
    aPoints[2].x = src_x;
    aPoints[2].y = srcWindowHeight - src_y - height;
    display->request++;

    if (plane != 1) {
        panic("Unexpected plane specified for XCopyPlane");
    }

#ifdef VERBOSE
    printf("Draw:  srcPS %x, type %s, dstPS %x, type %s, clip_mask %x\n", srcPS,
           ((TkOS2Drawable *)src)->type == TOD_BITMAP ? "bitmap" : "window",
           dstPS,
           ((TkOS2Drawable *)dest)->type == TOD_BITMAP ? "bitmap" : "window",
           gc->clip_mask);
    printf("Draw:    (%d,%d) (%d,%d) (%d,%d)\n", aPoints[0].x, aPoints[0].y,
           aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y);
#endif

    if (clipPtr == NULL || clipPtr->type == TKP_CLIP_REGION) {

        /*
         * Case 1: opaque bitmaps.
         * Copying a monochrome bitmap to a color bitmap or to a color/mono
         * device surface, src=1 adopts the current image-foreground color,
         * src=0 adopts the current image-background color.
         */
#ifdef VERBOSE
        printf("Draw:XCopyPlane case1\n");
#endif

        if (clipPtr && clipPtr->type == TKP_CLIP_REGION) {
            if (GpiSetClipRegion(dstPS, (HRGN) clipPtr->value.region, &oldReg)
                != RGN_ERROR) {
                POINTL offset;
#ifdef VERBOSE
                printf("Draw:GpiSetClipRegion OK\n");
#endif
                offset.x = gc->clip_x_origin;
                /* Reverse Y coordinates */
                offset.y = - gc->clip_y_origin;
                rc = GpiOffsetClipRegion(dstPS, &offset);
#ifdef VERBOSE
                if (rc == RGN_ERROR) {
                    printf("Draw:GpiOffsetClipRegion %d,%d ERROR %x\n", offset.x,
                           offset.y, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiSetClipRegion %d,%d OK (%s)\n", offset.x,
                           offset.y, rc == RGN_NULL ? "RGN_NULL" :
                           (rc == RGN_RECT ? "RGN_RECT" :
                           (rc == RGN_COMPLEX ? "RGN_COMPLEX" : "UNKNOWN")));
                }
#endif
            }
#ifdef VERBOSE
              else {
                printf("Draw:GpiSetClipRegion ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            }
#endif
        }

        oldColor = GpiQueryColor(dstPS);
        oldBackColor = GpiQueryBackColor(dstPS);
        oldMix = GpiQueryMix(dstPS);
        oldBackMix = GpiQueryBackMix(dstPS);
#ifdef VERBOSE
        printf("Draw: oldColor %d, oldBackColor %d, oldMix %d, oldBackMix %d\n",
               oldColor, oldBackColor, oldMix, oldBackMix);
        printf("Draw: oldColor srcPS %d, oldBackColor srcPS %d\n",
               GpiQueryColor(srcPS), GpiQueryBackColor(srcPS));
#endif

/*
        rc= GpiSetColor(dstPS, gc->foreground);
#ifdef VERBOSE
        if (rc==TRUE) printf("Draw:    GpiSetColor %x OK\n", gc->foreground);
        else printf("Draw:    GpiSetColor %x ERROR: %x\n", gc->foreground,
                    WinGetLastError(TclOS2GetHAB()));
#endif
        rc= GpiSetBackColor(dstPS, gc->background);
#ifdef VERBOSE
        if (rc==TRUE) printf("Draw:    GpiSetBackColor %x OK\n", gc->background);
        else printf("Draw:    GpiSetBackColor %x ERROR: %x\n", gc->background,
                    WinGetLastError(TclOS2GetHAB()));
#endif
*/
        rc= GpiSetColor(dstPS, gc->background);
#ifdef VERBOSE
        if (rc==TRUE) printf("Draw:    GpiSetColor %x OK\n", gc->background);
        else printf("Draw:    GpiSetColor %x ERROR: %x\n", gc->background,
                    WinGetLastError(TclOS2GetHAB()));
#endif
        rc= GpiSetBackColor(dstPS, gc->foreground);
#ifdef VERBOSE
        if (rc==TRUE) printf("Draw:    GpiSetBackColor %x OK\n", gc->foreground);
        else printf("Draw:    GpiSetBackColor %x ERROR: %x\n", gc->foreground,
                    WinGetLastError(TclOS2GetHAB()));
#endif

        rc = GpiBitBlt(dstPS, srcPS, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef VERBOSE
        printf("Draw: GpiBitBlt SRCCOPY (clip_mask None) %x -> %x returns %x\n",
               srcPS, dstPS, rc);
fflush(stdout);
#endif
        rc= GpiSetColor(dstPS, oldColor);
        rc= GpiSetBackColor(dstPS, oldBackColor);
        rc= GpiSetMix(dstPS, oldMix);
        rc= GpiSetBackMix(dstPS, oldBackMix);

        GpiSetClipRegion(dstPS, oldReg, &oldReg);
    } else if (clipPtr->type == TKP_CLIP_PIXMAP) {
        if (clipPtr->value.pixmap == src) {

            /*
             * Case 2: transparent bitmaps are handled by setting the
             * destination to the foreground color whenever the source
             * pixel is set.
             */
#if 0
#ifdef VERBOSE
            printf("Draw:XCopyPlane case2\n");
#endif
            oldPattern = GpiQueryPattern(dstPS);
            /* Create a solid "brush" (pattern) in the foreground color */
            rc = GpiSetPattern(dstPS, PATSYM_SOLID);
#ifdef VERBOSE
            printf("Draw:GpiSetPattern PATSYM_SOLID returns %x\n", rc);
#endif
            rc = GpiQueryAttrs(dstPS, PRIM_AREA, LBB_COLOR, (PBUNDLE)&aBundle);
            oldColor = aBundle.lColor;
            aBundle.lColor = gc->foreground;
            rc = GpiSetAttrs(dstPS, PRIM_AREA, LBB_COLOR, 0L,(PBUNDLE)&aBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs areaColor %x ERROR %x\n", aBundle.lColor,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs areaColor %x OK\n", aBundle.lColor);
            }
#endif
            rc = GpiBitBlt(dstPS, srcPS, 3, aPoints, MASKPAT, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw:  GpiBitBlt MASKPAT %x -> %x returns %d\n", srcPS,
                   dstPS, rc);
#endif
            GpiSetPattern(dstPS, oldPattern);
            aBundle.lColor = oldColor;
            rc = GpiSetAttrs(dstPS, PRIM_AREA, LBB_COLOR, 0L,(PBUNDLE)&aBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs reverting areaColor %x ERROR %x\n",
                       aBundle.lColor, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs revert. areaColor %x OK\n", aBundle.lColor);
            }
#endif
#endif /* 0 */
            /*
             * From the OS/2 Programming FAQ v4.0 - August 1, 1997:
             * - Use a monochrome mask to prep the destination area. The mask
             *   would define which areas would be transparent and which would
             *   show the bitmap. The bits would be defined as 1=transparent,
             *   0=bitmap. You would blit the mask to the destination using
             *   ROP_SRCAND. This would blacken out the area that would display
             *   the non-transparent bits of the bitmap.
             * - Now blit the bitmap to the destination using ROP_SRCPAINT. 
             *   Note that the "transparent" areas of the bitmap must have the
             *   color black (i.e. bits=0). This ORs the bitmap onto the prep
             *   area. Voila - "transparent" bitmap.
             */
            HDC hScreenDC;
            DEVOPENSTRUC doStruc= {0L, (PSZ)"DISPLAY", NULL,
                                   0L, 0L, 0L, 0L, 0L, 0L};
            SIZEL sizel = {0,0};
            HPS memPS;
            BITMAPINFOHEADER2 bmpInfo;
            HBITMAP bitmap, oldBitmap;
#ifdef VERBOSE
            printf("Draw:XCopyPlane case2\n");
#endif

            hScreenDC = DevOpenDC(tkHab, OD_MEMORY, (PSZ)"*", 0,
                                  (PDEVOPENDATA)&doStruc, NULLHANDLE);
            memPS = GpiCreatePS(tkHab, hScreenDC, &sizel, PU_PELS | GPIA_ASSOC);
            bmpInfo.cbFix = 16L;
            bmpInfo.cx = width;
            bmpInfo.cy = height;
            bmpInfo.cPlanes = 1;
            bmpInfo.cBitCount = aDevCaps[CAPS_COLOR_BITCOUNT];
            bitmap = GpiCreateBitmap(memPS, &bmpInfo, 0L, NULL, NULL);
#ifdef VERBOSE
            printf("Draw: GpiCreateBitmap (%d,%d) returned %x\n", width, height,
                   bitmap);
#endif
            oldBitmap = GpiSetBitmap(memPS, bitmap);
#ifdef VERBOSE
            printf("Draw: GpiSetBitmap %x returned %x\n", bitmap, oldBitmap);
#endif
            rc = GpiQueryAttrs(srcPS, PRIM_AREA, LBB_COLOR | LBB_BACK_COLOR,
                               (PBUNDLE)&aBundle);
            oldColor = aBundle.lColor;
            oldBackColor = aBundle.lBackColor;
            aBundle.lColor = CLR_BLACK;
            aBundle.lBackColor = CLR_WHITE;
            rc = GpiSetAttrs(srcPS, PRIM_AREA, LBB_COLOR | LBB_BACK_COLOR, 0L,
                             (PBUNDLE)&aBundle);
    
            /*
             * Set foreground bits.  We create a new bitmap containing
             * (NOT source).
             */
    
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = 0; /* dest_x = 0 */
            aPoints[0].y = 0; /* dest_y = 0 */
            aPoints[1].x = width;
            aPoints[1].y = height;
            aPoints[2].x = src_x;
            aPoints[2].y = srcWindowHeight - src_y - height;
            rc= GpiBitBlt(memPS, srcPS, 3, aPoints, ROP_NOTSRCCOPY, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr1 NOTSRCCOPY %x, %x returns %d\n", memPS,
                   srcPS, rc);
#endif
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = dest_x;
            aPoints[0].y = dstWindowHeight - dest_y - height;
            aPoints[1].x = dest_x + width;
            aPoints[1].y = dstWindowHeight - dest_y;
            aPoints[2].x = 0; /* dest_x = 0 */
            aPoints[2].y = 0; /* dest_y = 0 */
            rc = GpiBitBlt(dstPS, memPS, 3, aPoints, ROP_SRCAND, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr2 SRCAND %x, %x returns %d\n", dstPS,
                   memPS, rc);
#endif
/*
            rc = GpiQueryAttrs(srcPS, PRIM_AREA, LBB_BACK_COLOR,
                               (PBUNDLE)&aBundle);
            oldBackColor = aBundle.lBackColor;
            aBundle.lBackColor = CLR_BLACK;
            rc = GpiSetAttrs(srcPS, PRIM_AREA, LBB_BACK_COLOR, 0L,
                             (PBUNDLE)&aBundle);
*/
            aBundle.lColor = oldColor;
            rc = GpiSetAttrs(srcPS, PRIM_AREA, LBB_COLOR, 0L,
                             (PBUNDLE)&aBundle);
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = dest_x;
            aPoints[0].y = dstWindowHeight - dest_y - height;
            aPoints[1].x = dest_x + width;
            aPoints[1].y = dstWindowHeight - dest_y;
            aPoints[2].x = src_x;
            aPoints[2].y = srcWindowHeight - src_y - height;
            rc= GpiBitBlt(dstPS, srcPS, 3, aPoints, ROP_SRCPAINT, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr1 SRCPAINT %x, %x returns %d\n", dstPS,
                   srcPS, rc);
#endif
            aBundle.lBackColor = oldBackColor;
            aBundle.lColor = oldColor;
            rc = GpiSetAttrs(srcPS, PRIM_AREA, LBB_COLOR | LBB_BACK_COLOR, 0L,
                             (PBUNDLE)&aBundle);
            GpiDestroyPS(memPS);
            DevCloseDC(hScreenDC);

        } else {

            /*
             * Case 3: two arbitrary bitmaps.  Copy the source rectangle
             * into a color pixmap.  Use the result as a brush when
             * copying the clip mask into the destination.
             */

            HPS memPS, maskPS;
            BITMAPINFOHEADER2 bmpInfo;
            HBITMAP bitmap, oldBitmap;
            TkOS2PSState maskState;
#ifdef VERBOSE
            printf("Draw:XCopyPlane case3\n");
#endif
    
            oldColor = GpiQueryColor(dstPS);
            oldPattern = GpiQueryPattern(dstPS);
    
            maskPS = TkOS2GetDrawablePS(display, clipPtr->value.pixmap,
                                        &maskState);
            memPS = WinGetScreenPS(HWND_DESKTOP);
            bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
            bmpInfo.cx = width;
            bmpInfo.cy = height;
            bmpInfo.cPlanes = 1;
            bmpInfo.cBitCount = 1;
            bitmap = GpiCreateBitmap(memPS, &bmpInfo, 0L, NULL, NULL);
#ifdef VERBOSE
            printf("Draw:    GpiCreateBitmap (%d,%d) returned %x\n", width, height,
                   bitmap);
#endif
            oldBitmap = GpiSetBitmap(memPS, bitmap);
#ifdef VERBOSE
            printf("Draw:    GpiSetBitmap %x returned %x\n", bitmap, oldBitmap);
#endif
            rc = GpiQueryAttrs(dstPS, PRIM_AREA, LBB_COLOR, (PBUNDLE)&aBundle);
            oldColor = aBundle.lColor;
            oldPattern = GpiQueryPattern(dstPS);
            /* Create a solid "brush" (pattern) */
            rc = GpiSetPattern(dstPS, PATSYM_SOLID);
#ifdef VERBOSE
            printf("Draw:GpiSetPattern PATSYM_SOLID returns %x\n", rc);
#endif
    
            /*
             * Set foreground bits.  We create a new bitmap containing
             * (source AND mask), then use it to set the foreground color
             * into the destination.
             */
    
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = 0; /* dest_x = 0 */
            aPoints[0].y = dstWindowHeight - height; /* dest_y = 0 */
            aPoints[1].x = width;
            aPoints[1].y = dstWindowHeight;
            aPoints[2].x = src_x;
            aPoints[2].y = srcWindowHeight - src_y - height;
            rc = GpiBitBlt(memPS, srcPS, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr1 SRCCOPY %x, %x returns %d\n", memPS,
                   srcPS, rc);
#endif
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = 0; /* dest_x = 0 */
            aPoints[0].y = dstWindowHeight - height; /* dest_y = 0 */
            aPoints[1].x = dest_x + width;
            aPoints[1].y = dstWindowHeight;
            aPoints[2].x = dest_x - gc->clip_x_origin;
            aPoints[2].y = srcWindowHeight - dest_y + gc->clip_y_origin -
                           height;
            rc = GpiBitBlt(memPS, maskPS, 3, aPoints, ROP_SRCAND, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr2 SRCAND %x, %x returns %d\n", memPS,
                   maskPS, rc);
#endif
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = dest_x;
            aPoints[0].y = dstWindowHeight - dest_y - height;
            aPoints[1].x = dest_x + width;
            aPoints[1].y = dstWindowHeight - dest_y;
            aPoints[2].x = 0; /* src_x = 0 */
            aPoints[2].y = srcWindowHeight - height; /* src_y = 0 */

            aBundle.lColor = gc->foreground;
            rc = GpiSetAttrs(dstPS, PRIM_AREA, LBB_COLOR, 0L,(PBUNDLE)&aBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs areaColor %x ERROR %x\n", aBundle.lColor,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs areaColor %x OK\n", aBundle.lColor);
            }
#endif

            rc = GpiBitBlt(dstPS, memPS, 3, aPoints, MASKPAT, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr3 MASKPAT %x, %x returns %d\n", dstPS,
                   memPS, rc);
#endif
    
            /*
             * Set background bits.  Same as foreground, except we use
             * ((NOT source) AND mask) and the background brush.
             */
    
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = 0; /* dest_x = 0 */
            aPoints[0].y = dstWindowHeight - height; /* dest_y = 0 */
            aPoints[1].x = width;
            aPoints[1].y = dstWindowHeight;
            aPoints[2].x = src_x;
            aPoints[2].y = srcWindowHeight - src_y - height;
            rc = GpiBitBlt(memPS, srcPS, 3, aPoints, ROP_NOTSRCCOPY,
                           BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr4 NOTSRCCOPY %x, %x returns %d\n", dstPS,
                   srcPS, rc);
#endif
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = 0; /* dest_x = 0 */
            aPoints[0].y = dstWindowHeight - height; /* dest_y = 0 */
            aPoints[1].x = dest_x + width;
            aPoints[1].y = dstWindowHeight;
            aPoints[2].x = dest_x - gc->clip_x_origin;
            aPoints[2].y = dstWindowHeight - dest_y + gc->clip_y_origin -
                           height;
            rc = GpiBitBlt(memPS, maskPS, 3, aPoints, ROP_SRCAND, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr5 SRCAND %x, %x returns %d\n", dstPS,
                   srcPS, rc);
#endif

            aBundle.lColor = gc->background;
            rc = GpiSetAttrs(dstPS, PRIM_AREA, LBB_COLOR, 0L,(PBUNDLE)&aBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs areaColor %x ERROR %x\n", aBundle.lColor,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs areaColor %x OK\n", aBundle.lColor);
            }
#endif

            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = dest_x;
            aPoints[0].y = dstWindowHeight - dest_y - height;
            aPoints[1].x = dest_x + width;
            aPoints[1].y = dstWindowHeight - dest_y;
            aPoints[2].x = 0; /* src_x = 0 */
            aPoints[2].y = srcWindowHeight - height; /* src_y = 0 */
            rc = GpiBitBlt(dstPS, memPS, 3, aPoints, MASKPAT, BBO_IGNORE);
#ifdef VERBOSE
            printf("Draw: GpiBitBlt nr6 MASKPAT %x, %x returns %d\n", dstPS,
                   srcPS, rc);
#endif
    
            TkOS2ReleaseDrawablePS(clipPtr->value.pixmap, maskPS, &maskState);
            GpiSetPattern(dstPS, oldPattern);
            aBundle.lColor = oldColor;
            rc = GpiSetAttrs(dstPS, PRIM_AREA, LBB_COLOR, 0L,(PBUNDLE)&aBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs reverting areaColor %x and %x ERROR %x\n",
                       aBundle.lColor, aBundle.lBackColor,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs reverting areaColor %x and %x OK\n",
                       aBundle.lColor, aBundle.lBackColor);
            }
#endif
            GpiSetBitmap(memPS, oldBitmap);
            GpiDeleteBitmap(bitmap);
            WinReleasePS(memPS);
        }
    }

    if (src != dest) {
        TkOS2ReleaseDrawablePS(dest, dstPS, &dstState);
    }
    TkOS2ReleaseDrawablePS(src, srcPS, &srcState);
}

/*
 *----------------------------------------------------------------------
 *
 * TkPutImage --
 *
 *	Copies a subimage from an in-memory image to a rectangle of
 *	the specified drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws the image on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
TkPutImage(colors, ncolors, display, d, gc, image, src_x, src_y, dest_x,
        dest_y, width, height)
    unsigned long *colors;		/* Array of pixel values used by this
					 * image.  May be NULL. */
    int ncolors;			/* Number of colors used, or 0. */
    Display* display;
    Drawable d;				/* Destination drawable. */
    GC gc;
    XImage* image;			/* Source image. */
    int src_x, src_y;			/* Offset of subimage. */      
    int dest_x, dest_y;			/* Position of subimage origin in
					 * drawable.  */
    unsigned int width, height;		/* Dimensions of subimage. */
{
    HPS hps;
    LONG rc;
    TkOS2PSState state;
    BITMAPINFOHEADER2 bmpInfo;
    BITMAPINFO2 *infoPtr;
    char *data;
    LONG windowHeight;

    /* Translate the Y coordinates to PM coordinates */
    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

    display->request++;

    hps = TkOS2GetDrawablePS(display, d, &state);

#ifdef VERBOSE
    printf("Draw:TkPutImage d %x hps %x (%d,%d) => (%d,%d) %dx%d imgh %d mix %d\n",
           d, hps, src_x, src_y, dest_x, dest_y, width, height, image->height,
           gc->function);
    printf("Draw:    nrColors %d, gc->foreground %d, gc->background %d\n", ncolors,
           gc->foreground, gc->background);
#endif

    rc = GpiSetMix(hps, tkpOS2MixModes[gc->function]);
#ifdef VERBOSE
    if (rc == FALSE) {
        printf("Draw:    GpiSetMix ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("Draw:    GpiSetMix %x OK\n", tkpOS2MixModes[gc->function]);
    }
    printf("Draw:    hps color %d, hps back color %d\n", GpiQueryColor(hps),
           GpiQueryBackColor(hps));
#endif

    if (image->depth == 1) {
#ifdef VERBOSE
        printf("Draw:image->depth == 1\n");
#endif
        /*
         * If the image isn't in the right format, we have to copy
         * it into a new buffer in MSBFirst and word-aligned format.
         * Bitmap must be reversed in OS/2 wrt. the Y direction
         */
        if ((image->bitmap_bit_order != MSBFirst)
            || (image->bitmap_pad != sizeof(ULONG))) {
            data = TkAlignImageData(image, sizeof(ULONG), MSBFirst);
        } else {
            data = TkOS2ReverseImageLines(image, height);
        }
        bmpInfo.cbFix = 16L;
        bmpInfo.cx = image->width;
        bmpInfo.cy = image->height;
        bmpInfo.cPlanes = 1;
        bmpInfo.cBitCount = 1;
        rc = GpiSetBitmapBits(hps, windowHeight - dest_y - height, height,
                              (PBYTE)data, (BITMAPINFO2*) &bmpInfo);
#ifdef VERBOSE
        if (rc == GPI_ALTERROR) {
            BITMAPINFOHEADER2 info;
            printf("Draw:    GpiSetBitmapBits mono returned GPI_ALTERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
            info.cbFix = sizeof(BITMAPINFOHEADER2);
            rc = GpiQueryBitmapInfoHeader(((TkOS2Drawable *)d)->bitmap.handle,
                                          &info);
            if (rc == FALSE) {
                printf("Draw:    GpiQueryBitmapInfoHeader mono ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:    GpiQueryBitmapInfoHeader: mono %dx%d\n", info.cx,
                       info.cy);
            }
        } else {
            printf("Draw:   GpiSetBitmapBits mono %d set %d scanlines hps %x h %x\n",
                   windowHeight - dest_y - height, rc, hps,
                   ((TkOS2Drawable *)d)->bitmap.handle);
        }
#endif

        ckfree(data);
    } else {
        int i, usePalette, nrPalColors;
        LONG *colorTable;

#ifdef VERBOSE
        LONG defBitmapFormat[2];
        printf("Draw:image->bits_per_pixel %d\n", image->bits_per_pixel);
#endif

        /*
         * Do not use a palette for high color images.
         */
        
        usePalette = (image->bits_per_pixel < 24);

        if (usePalette) {
#ifdef VERBOSE
            printf("Draw:using palette (not TrueColor)\n");
#endif
            infoPtr = (BITMAPINFO2*) ckalloc(sizeof(BITMAPINFO2)
                    + sizeof(RGB)*ncolors);
            if (infoPtr == NULL) return;
            infoPtr->cbFix = sizeof(BITMAPINFOHEADER2) + sizeof(RGB)*ncolors;
            infoPtr->cclrUsed = ncolors;
            /* Determine nr. of colors in Palette / LogColorTable */
            nrPalColors = GpiQueryPaletteInfo(0L, hps, 0L, 0L, 0L, (PLONG)NULL);
            colorTable = (LONG *) ckalloc(sizeof(LONG) * nrPalColors);
            if (colorTable == NULL) {
                ckfree((char *)infoPtr);
                return;
            }
            rc = GpiQueryPaletteInfo(0L, hps, 0L, 0L, nrPalColors, colorTable);
            for (i = 0; i < ncolors && i < nrPalColors; i++) {
                infoPtr->argbColor[i].bBlue = GetBValue(colorTable[colors[i]]);
                infoPtr->argbColor[i].bGreen = GetGValue(colorTable[colors[i]]);
                infoPtr->argbColor[i].bRed = GetRValue(colorTable[colors[i]]);
#ifdef VERBOSE
            printf("   argbColor[%d] %x RGB%2x:%2x:%2x\n", i,
                   colorTable[colors[i]], infoPtr->argbColor[i].bRed,
                   infoPtr->argbColor[i].bGreen, infoPtr->argbColor[i].bBlue);
#endif
            }
        } else {
#ifdef VERBOSE
            printf("Draw:not using palette (TrueColor)\n");
#endif
            infoPtr = (BITMAPINFO2*) ckalloc(sizeof(BITMAPINFO2));
            if (infoPtr == NULL) return;
            infoPtr->cbFix = sizeof(BITMAPINFOHEADER2);
            infoPtr->cclrUsed = 0;
        }

        /* Bitmap must be reversed in OS/2 wrt. the Y direction */
        data = TkOS2ReverseImageLines(image, height);
        
        infoPtr->cx = width;
        infoPtr->cy = height;

#ifdef VERBOSE
        rc = GpiQueryDeviceBitmapFormats(hps, 2, defBitmapFormat);
        if (rc != TRUE) {
            printf("Draw:    GpiQueryDeviceBitmapFormats ERROR %x -> mono\n",
                   WinGetLastError(TclOS2GetHAB()));
            infoPtr->cPlanes = 1;
            infoPtr->cBitCount = 1;
        } else {
            printf("Draw:    GpiQueryDeviceBitmapFormats OK planes %d, bits %d\n",
                   defBitmapFormat[0], defBitmapFormat[1]);
            infoPtr->cPlanes = defBitmapFormat[0];
            infoPtr->cBitCount = defBitmapFormat[1];
        }
                fflush(stdout);
#endif

        infoPtr->cPlanes = 1;
        infoPtr->cBitCount = image->bits_per_pixel;
        infoPtr->ulCompression = BCA_UNCOMP;
        infoPtr->cbImage = 0;
        infoPtr->cxResolution = aDevCaps[CAPS_HORIZONTAL_RESOLUTION];
        infoPtr->cyResolution = aDevCaps[CAPS_VERTICAL_RESOLUTION];
        infoPtr->cclrImportant = 0;
        infoPtr->usUnits = BRU_METRIC;
        infoPtr->usReserved = 0;
        infoPtr->usRecording = BRA_BOTTOMUP;
        infoPtr->usRendering = BRH_NOTHALFTONED;
        infoPtr->cSize1 = 0L;
        infoPtr->cSize2 = 0L;
        infoPtr->ulColorEncoding = BCE_RGB;
        infoPtr->ulIdentifier = 0L;

/*
        if (usePalette) {
            if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
#ifdef VERBOSE
                printf("Draw:    Palette manager\n");
                fflush(stdout);
#endif
                rc = GpiQueryPaletteInfo(GpiQueryPalette(hps), hps, 0, 0,
                                         ncolors, (PLONG) &infoPtr->argbColor);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:    GpiQueryPaletteInfo ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                fflush(stdout);
                }
#endif
            } else {
                rc = GpiQueryLogColorTable(hps, 0, 0, ncolors,
                                           (PLONG) &infoPtr->argbColor);
#ifdef VERBOSE
                if (rc == QLCT_ERROR) {
                    printf("Draw:    GpiQueryLogColorTable ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    if (rc == QLCT_RGB) {
                        printf("Draw:    table in RGB mode, no element returned\n");
                    }
                }
                fflush(stdout);
#endif
            }
        } else {
            infoPtr->cclrUsed = 0;
        }
*/

        rc = GpiSetBitmapBits(hps, windowHeight - dest_y - height, height,
                              (PBYTE)data, infoPtr);
#ifdef VERBOSE
        printf("Draw:windowHeight %d, dest_y %d, height %d, image->height %d\n",
               windowHeight, dest_y, height, image->height);
        if (rc == GPI_ALTERROR) {
            printf("Draw:GpiSetBitmapBits hps %x returned GPI_ALTERROR %x\n", hps,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:    GpiSetBitmapBits (%d) set %d scanlines (hps %x, h %x)\n", 
                   windowHeight - dest_y - height, rc, hps,
                   ((TkOS2Drawable *)d)->bitmap.handle);
        }
                fflush(stdout);
#endif

        ckfree((char *)infoPtr);
        ckfree(data);
    }
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangles --
 *
 *	Fill multiple rectangular areas in the given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws onto the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillRectangles(display, d, gc, rectangles, nrectangles)
    Display* display;
    Drawable d;
    GC gc;
    XRectangle* rectangles;
    int nrectangles;
{
    HPS hps;
    int i;
    RECTL rect;
    TkOS2PSState state;
    LONG windowHeight;
    POINTL refPoint;
    LONG oldPattern, oldBitmap;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;

    if (d == None) {
        return;
    }

    windowHeight = TkOS2WindowHeight(todPtr);

#ifdef VERBOSE
    if (todPtr->type == TOD_BITMAP) {
         printf("Draw:XFillRectangles bmp %x, h %x cmap %x depth %d mix %x wh %d\n",
                todPtr, todPtr->bitmap.handle, todPtr->bitmap.colormap,
                todPtr->bitmap.depth, gc->function, windowHeight);
    } else if (todPtr->type == TOD_OS2PS) {
         printf("Draw:XFillRectangles hps %x hwnd %x wh %d\n", todPtr->os2PS.hps,
	        todPtr->os2PS.hwnd, windowHeight);
    } else {
         printf("Draw:XFillRectangles todPtr %x winPtr %x h %x mix %x wh %d\n",
                todPtr, todPtr->window.winPtr, todPtr->window.handle,
                gc->function, windowHeight);
    }
#endif

    hps = TkOS2GetDrawablePS(display, d, &state);
    GpiSetMix(hps, tkpOS2MixModes[gc->function]);

    if ((gc->fill_style == FillStippled
            || gc->fill_style == FillOpaqueStippled)
            && gc->stipple != None) {
        HBITMAP bitmap;
        BITMAPINFOHEADER2 bmpInfo;
        LONG rc;
        DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
        SIZEL sizl = {0,0}; /* use same page size as device */
        HDC dcMem;
        HPS psMem;
        POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
        POINTL oldRefPoint;

#ifdef VERBOSE
        printf("Draw: XFillRectangles stippled\n");
#endif
        todPtr = (TkOS2Drawable *)gc->stipple;

        if (todPtr->type != TOD_BITMAP) {
            panic("unexpected drawable type in stipple");
        }

        /*
         * Select stipple pattern into destination dc.
         */

        refPoint.x = gc->ts_x_origin;
        /* Translate Xlib y to PM y */
        refPoint.y = windowHeight - gc->ts_y_origin;

        /* The bitmap mustn't be selected in the HPS */
        TkOS2SetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                        refPoint.x, refPoint.y, &oldPattern, &oldRefPoint);

        dcMem = DevOpenDC(TclOS2GetHAB(), OD_MEMORY, (PSZ)"*", 5L,
                          (PDEVOPENDATA)&dop, NULLHANDLE);
        if (dcMem == DEV_ERROR) {
#ifdef VERBOSE
            printf("Draw:DevOpenDC ERROR in XFillRectangles\n");
#endif
            return;
        }
#ifdef VERBOSE
        printf("Draw:DevOpenDC in XFillRectangles returns %x\n", dcMem);
#endif
        psMem = GpiCreatePS(TclOS2GetHAB(), dcMem, &sizl,
                            PU_PELS | GPIT_NORMAL | GPIA_ASSOC);
        if (psMem == GPI_ERROR) {
#ifdef VERBOSE
            printf("Draw:GpiCreatePS ERROR in XFillRectangles: %x\n",
                   WinGetLastError(TclOS2GetHAB()));
#endif
            DevCloseDC(dcMem);
            return;
        }
#ifdef VERBOSE
        printf("Draw:GpiCreatePS in XFillRectangles returns %x\n", psMem);
#endif

        /*
         * For each rectangle, create a drawing surface which is the size of
         * the rectangle and fill it with the background color.  Then merge the
         * result with the stipple pattern.
         */

        for (i = 0; i < nrectangles; i++) {
            bmpInfo.cbFix = 16L;
            bmpInfo.cx = rectangles[i].width + 1;
            bmpInfo.cy = rectangles[i].height + 1;
            bmpInfo.cPlanes = 1;
            bmpInfo.cBitCount = 1;
            bitmap = GpiCreateBitmap(psMem, &bmpInfo, 0L, NULL, NULL);
#ifdef VERBOSE
            if (bitmap == GPI_ERROR) {
                printf("Draw:GpiCreateBitmap (%d,%d) GPI_ERROR %x\n",
                       bmpInfo.cx, bmpInfo.cy, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiCreateBitmap (%d,%d) returned %x\n", bmpInfo.cx, bmpInfo.cy, bitmap);
            }
#endif
            oldBitmap = GpiSetBitmap(psMem, bitmap);
#ifdef VERBOSE
            if (bitmap == HBM_ERROR) {
                printf("Draw:GpiSetBitmap (%x) HBM_ERROR %x\n", bitmap,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiSetBitmap %x returned %x\n", bitmap, oldBitmap);
            }
#endif

            /* Translate the Y coordinates to PM coordinates */

            rect.xLeft = 0;
            rect.xRight = rectangles[i].width + 1;
            rect.yBottom = 0;
            rect.yTop = rectangles[i].height + 1;

            oldPattern = GpiQueryPattern(psMem);
            GpiSetPattern(psMem, PATSYM_SOLID);
            rc = WinFillRect(psMem, &rect, gc->foreground);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:WinFillRect3 (%d, %d)->(%d,%d) fg %x ERROR %x\n",
                       rect.xLeft, rect.yBottom, rect.xRight, rect.yTop,
                       gc->foreground, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:WinFillRect3 (%d, %d)->(%d,%d) fg %x OK\n",
                       rect.xLeft, rect.yBottom, rect.xRight, rect.yTop,
                       gc->foreground);
            }
#endif
            /* Translate the Y coordinates to PM coordinates */
            aPoints[0].x = rectangles[i].x;
            aPoints[0].y = windowHeight - rectangles[i].y -
                           rectangles[i].height;
            aPoints[1].x = rectangles[i].x + rectangles[i].width + 1;
            aPoints[1].y = windowHeight - rectangles[i].y + 1;
            aPoints[2].x = 0;
            aPoints[2].y = 0;
            rc = GpiBitBlt(hps, psMem, 3, aPoints, COPYFG, BBO_IGNORE);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("Draw: GpiBitBlt COPYFG %x (%d,%d)(%d,%d) <- (%d,%d) ERROR %x\n",
                       gc->foreground, aPoints[0].x, aPoints[0].y,
                       aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw: GpiBitBlt COPYFG %x (%d,%d)(%d,%d)<-(%d,%d) OK\n",
                       gc->foreground, aPoints[0].x, aPoints[0].y,
                       aPoints[1].x, aPoints[1].y,
                       aPoints[2].x, aPoints[2].y);
            }
#endif
            if (gc->fill_style == FillOpaqueStippled) {
                rc = WinFillRect(psMem, &rect, gc->background);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:WinFillRect4 (%d, %d)->(%d,%d) bg %x ERROR %x\n",
                           rect.xLeft, rect.yBottom, rect.xRight, rect.yTop,
                           gc->background, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:WinFillRect4 (%d, %d)->(%d,%d) bg %x OK\n",
                           rect.xLeft, rect.yBottom, rect.xRight, rect.yTop,
                           gc->background);
                }
#endif
                rc = GpiBitBlt(hps, psMem, 3, aPoints, COPYBG, BBO_IGNORE);
#ifdef VERBOSE
                if (rc!=TRUE) {
                    printf("Draw: GpiBitBlt COPYBG %x (%d,%d)(%d,%d) <- (%d,%d) ERROR %x\n",
                           gc->background, aPoints[0].x, aPoints[0].y,
                           aPoints[1].x, aPoints[1].y, aPoints[2].x,
                           aPoints[2].y, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw: GpiBitBlt COPYBG %x (%d,%d)(%d,%d) <- (%d,%d) OK\n",
                           gc->background, aPoints[0].x, aPoints[0].y,
                           aPoints[1].x, aPoints[1].y,
                           aPoints[2].x, aPoints[2].y);
                }
#endif
            }
            GpiSetPattern(psMem, oldPattern);
            GpiDeleteBitmap(bitmap);
        }
        GpiDestroyPS(psMem);
        DevCloseDC(dcMem);
        /* The bitmap must be reselected in the HPS */
        TkOS2UnsetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                          oldPattern, &oldRefPoint);
    } else {

        for (i = 0; i < nrectangles; i++) {
#ifdef VERBOSE
            POINTL pos;
            pos.x = rectangles[i].x;
            pos.y = windowHeight - rectangles[i].y - rectangles[i].height;
            WinMapWindowPoints(todPtr->window.handle, HWND_DESKTOP, &pos, 1);
            printf("Draw: XFillRectangles not stippled\n");
            printf("Draw:    rect[%d] (%d,%d) %dx%d (PM: %d,%d; root (%d,%d))\n", i,
                   rectangles[i].x, rectangles[i].y, rectangles[i].width,
                   rectangles[i].height, rectangles[i].x,
                   windowHeight - rectangles[i].y - rectangles[i].height,
                   pos.x, pos.y);
#endif
            TkOS2FillRect(hps, rectangles[i].x,
                          windowHeight - rectangles[i].y - rectangles[i].height,
                          rectangles[i].width, rectangles[i].height,
                          gc->foreground);
        }
    }

    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * RenderObject --
 *
 *	This function draws a shape using a list of points, a
 *	stipple pattern, and the specified drawing function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
RenderObject(hps, gc, d, points, npoints, mode, linePtr, areaPtr, func)
    HPS hps;
    GC gc;
    Drawable d;
    XPoint* points;
    int npoints;
    int mode;
    PLINEBUNDLE linePtr;
    PAREABUNDLE areaPtr;
    int func;
{
    RECTL rect;
    LINEBUNDLE oldLineBundle;
    LONG oldPattern;
    LONG oldColor;
    POINTL oldRefPoint;
    POINTL *os2Points;
    POINTL refPoint;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    LONG windowHeight;
    POLYGON polygon;
    AREABUNDLE aBundle;

#ifdef VERBOSE
printf("Draw:RenderObject width %d style %x cap %x join %x fill %x rule %x arc %x\n",
           gc->line_width, gc->line_style, gc->cap_style, gc->join_style,
           gc->fill_style, gc->fill_rule, gc->arc_mode);
#endif
    if ( func == TOP_POLYGONS) {
        linePtr->usType = LINETYPE_INVISIBLE;
    } else {
        linePtr->usType = lineStyles[gc->line_style];
    }

    /* os2Points/rect get *PM* coordinates handed to it by ConvertPoints */
    os2Points = ConvertPoints(d, points, npoints, mode, &rect);

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

    if ((gc->fill_style == FillStippled
            || gc->fill_style == FillOpaqueStippled)
            && gc->stipple != None) {

        TkOS2Drawable *todPtr = (TkOS2Drawable *)gc->stipple;
        DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
        SIZEL sizl = {0,0}; /* use same page size as device */
        HDC dcMem;
        HPS psMem;
        LONG width, height;
        int i;
        HBITMAP bitmap, oldBitmap;
        BITMAPINFOHEADER2 bmpInfo;

#ifdef VERBOSE
        printf("Draw:RenderObject stippled (%x)\n", todPtr->bitmap.handle);
#endif
        
        if (todPtr->type != TOD_BITMAP) {
            panic("unexpected drawable type in stipple");
        }

        /*
         * Grow the bounding box enough to account for line width.
         */

        rect.xLeft -= gc->line_width;
        rect.yTop += gc->line_width;
        /* PM coordinates are just reverse: top - bottom */
        rect.xRight += gc->line_width;
        rect.yBottom -= gc->line_width;
    
        width = rect.xRight - rect.xLeft;
        /* PM coordinates are just reverse: top - bottom */
        height = rect.yTop - rect.yBottom;

        /*
         * Select stipple pattern into destination hps.
         */
        
        refPoint.x = gc->ts_x_origin;
        /* Translate Xlib y to PM y */
        refPoint.y = windowHeight - gc->ts_y_origin;

        TkOS2SetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                        refPoint.x, refPoint.y, &oldPattern, &oldRefPoint);

        /*
         * Create temporary drawing surface containing a copy of the
         * destination equal in size to the bounding box of the object.
         */
        
        dcMem = DevOpenDC(TclOS2GetHAB(), OD_MEMORY, (PSZ)"*", 5L,
                          (PDEVOPENDATA)&dop, NULLHANDLE);
        if (dcMem == DEV_ERROR) {
#ifdef VERBOSE
            printf("Draw:DevOpenDC ERROR in RenderObject\n");
#endif
            return;
        }
#ifdef VERBOSE
        printf("Draw:DevOpenDC in RenderObject returns %x\n", dcMem);
#endif
        psMem = GpiCreatePS(TclOS2GetHAB(), dcMem, &sizl,
                            PU_PELS | GPIT_NORMAL | GPIA_ASSOC);
        if (psMem == GPI_ERROR) {
#ifdef VERBOSE
            printf("Draw:GpiCreatePS ERROR in RenderObject: %x\n",
                   WinGetLastError(TclOS2GetHAB()));
#endif
            DevCloseDC(dcMem);
            return;
        }
#ifdef VERBOSE
        printf("Draw:GpiCreatePS in RenderObject returns %x\n", psMem);
#endif

        TkOS2SelectPalette(psMem, HWND_DESKTOP, todPtr->bitmap.colormap);

    /*
     * X filling includes top and left sides, excludes bottom and right sides.
     * PM filling (WinFillRect) and BitBlt-ing (GpiBitBlt) includes bottom and
     * left sides, excludes top and right sides.
     * NB! X fills a box exactly as wide and high as width and height specify,
     * while PM cuts one pixel off the right and top.
     * => decrement y (X Window System) by one / increment y (PM) by one AND
     *    increment height by one, and increment width by one.
     */

        bmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
        bmpInfo.cx = width + 1;
        bmpInfo.cy = height + 1;
        bitmap = GpiCreateBitmap(psMem, &bmpInfo, 0L, NULL, NULL);
#ifdef VERBOSE
        if (rc==GPI_ERROR) {
            printf("Draw:    GpiCreateBitmap %dx%d ERROR %x\n", width, height,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:    GpiCreateBitmap %dx%d OK: %x\n", width, height, bitmap);
        }
#endif
        oldBitmap = GpiSetBitmap(psMem, bitmap);
#ifdef VERBOSE
        if (rc==HBM_ERROR) {
            printf("Draw:    GpiSetBitmap %x ERROR %x\n", bitmap,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:    GpiSetBitmap %x OK: %x\n", bitmap, oldBitmap);
        }
#endif
        rc = GpiSetAttrs(psMem, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH
                         | LBB_TYPE | LBB_END | LBB_JOIN, 0L, linePtr);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetAttrs color %x, width %d, type %x ERROR %x\n",
                   linePtr->lColor, linePtr->lGeomWidth,
                   linePtr->usType, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetAttrs color %x, width %d, type %x OK\n",
                   linePtr->lColor, linePtr->lGeomWidth,
                   linePtr->usType);
        }
#endif
        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = 0;	/* dest_x 0 */
        aPoints[0].y = 0;	/* dest_y 0 */
        aPoints[1].x = width + 1;	/* dest_x + width */
        aPoints[1].y = height + 1;	/* dest_y + height */
        aPoints[2].x = rect.xLeft;
        aPoints[2].y = rect.yBottom;

        GpiBitBlt(psMem, hps, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("Draw: GpiBitBlt SRCCOPY %x->%x ERROR %x\n", hps, psMem,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw: GpiBitBlt SRCCOPY %x->%x OK, aPoints (%d,%d)(%d,%d) (%d,%d)\n",
                   hps, psMem, aPoints[0].x, aPoints[0].y, aPoints[1].x,
                   aPoints[1].y, aPoints[2].x, aPoints[2].y);
        }
#endif

        /*
         * Translate the object to 0,0 for rendering in the temporary drawing
         * surface. 
         */

        for (i = 0; i < npoints; i++) {
            os2Points[i].x -= rect.xLeft;
            os2Points[i].y -= rect.yBottom;
#ifdef VERBOSE
            printf("Draw:os2Points[%d].x %d, os2Points[%d].y %d\n", i,
                   os2Points[i].x, i, os2Points[i].y);
#endif
        }

        /*
         * Draw the object in the foreground color and copy it to the
         * destination wherever the pattern is set.
         */

        rc = GpiSetPattern(psMem, PATSYM_SOLID);
        aBundle.lColor = gc->foreground;
        rc = GpiSetAttrs(psMem, PRIM_AREA, LBB_COLOR, 0L, (PBUNDLE)&aBundle);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiSetAttrs reverting areaColor %x ERROR %x\n",
                   aBundle.lColor, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSetAttrs revert. areaColor %x OK\n", aBundle.lColor);
        }
#endif

        if (func == TOP_POLYGONS) {
            rc = GpiSetCurrentPosition(psMem, os2Points);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiSetCurrentPosition %d,%d ERROR %x\n", os2Points[0].x,
                       os2Points[0].y, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiSetCurrentPosition %d,%d OK\n",
                       os2Points[0].x, os2Points[0].y);
            }
#endif
            polygon.ulPoints = npoints-1;
            polygon.aPointl = os2Points+1;
            rc = GpiPolygons(psMem, 1, &polygon, POLYGON_BOUNDARY |
                             (gc->fill_rule == EvenOddRule) ? POLYGON_ALTERNATE
                                                            : POLYGON_WINDING,
                             POLYGON_INCL);
#ifdef VERBOSE
            if (rc == GPI_ERROR) {
                printf("Draw:GpiPolygons ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiPolygons OK\n");
            }
#endif
        } else { /* TOP_POLYLINE */
            rc = GpiSetCurrentPosition(psMem, os2Points);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiSetCurrentPosition %d,%d ERROR %x\n",
                       os2Points[0].x, os2Points[0].y,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiSetCurrentPosition %d,%d OK\n",
                       os2Points[0].x, os2Points[0].y);
            }
#endif
            rc = GpiBeginPath(psMem, 1);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiBeginPath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiBeginPath OK\n");
            }
#endif
            rc = GpiPolyLine(psMem, npoints-1, os2Points+1);
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPolyLine ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPolyLine OK\n");
                }
#endif
            rc = GpiEndPath(psMem);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiEndPath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiEndPath OK\n");
            }
#endif
            rc = GpiStrokePath(psMem, 1, 0);
#ifdef VERBOSE
            if (rc == GPI_OK) {
                printf("Draw:GpiStrokePath OK\n");
            } else {
                printf("Draw:GpiStrokePath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            }
#endif
        }
        aPoints[0].x = rect.xLeft;	/* dest_x */
        aPoints[0].y = rect.yBottom;
        aPoints[1].x = rect.xRight;	/* dest_x + width */
        aPoints[1].y = rect.yTop;	/* dest_y */
        aPoints[2].x = 0;	/* src_x 0 */
        aPoints[2].y = 0;	/* Src_y */
        rc = GpiBitBlt(hps, psMem, 3, aPoints, COPYFG, BBO_IGNORE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("Draw:GpiBitBlt COPYFG %d,%d-%d,%d ERROR %x\n",
                   aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiBitBlt COPYFG %d,%d-%d,%d OK\n",
                   aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y);
        }
#endif

        /*
         * If we are rendering an opaque stipple, then draw the polygon in the
         * background color and copy it to the destination wherever the pattern
         * is clear.
         */

        if (gc->fill_style == FillOpaqueStippled) {
            aBundle.lColor = gc->background;
            rc = GpiSetAttrs(psMem, PRIM_AREA, LBB_COLOR, 0L,(PBUNDLE)&aBundle);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("GpiSetAttrs reverting areaColor %x ERROR %x\n",
                       aBundle.lColor, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("GpiSetAttrs revert. areaColor %x OK\n", aBundle.lColor);
            }
#endif
            if (func == TOP_POLYGONS) {
                polygon.ulPoints = npoints;
                polygon.aPointl = os2Points;
                rc = GpiPolygons(psMem, 1, &polygon,
                              (gc->fill_rule == EvenOddRule) ? POLYGON_ALTERNATE
                                                             : POLYGON_WINDING,
                              0);
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPolygons ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPolygons OK\n");
                }
#endif
                } else { /* TOP_POLYLINE */
                rc = GpiBeginPath(psMem, 1);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:GpiBeginPath ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiBeginPath OK\n");
                }
#endif
                rc = GpiPolyLine(psMem, npoints, os2Points);
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPolyLine ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPolyLine OK\n");
                }
#endif
                rc = GpiEndPath(psMem);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:GpiEndPath ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiEndPath OK\n");
                }
#endif
                rc = GpiStrokePath(psMem, 1, 0);
#ifdef VERBOSE
                if (rc == GPI_OK) {
                    printf("Draw:GpiStrokePath OK\n");
                } else {
                    printf("Draw:GpiStrokePath ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                }
#endif
            }
            rc = GpiBitBlt(hps, psMem, 3, aPoints, COPYBG, BBO_IGNORE);
#ifdef VERBOSE
            if (rc!=TRUE) {
                printf("Draw:GpiBitBlt COPYBG %d,%d-%d,%d ERROR %x\n",
                       aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiBitBlt COPYBG %d,%d-%d,%d OK\n",
                       aPoints[0].x, aPoints[0].y, aPoints[1].x, aPoints[1].y);
            }
#endif
        }
        /* end of using 254 */
        TkOS2UnsetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                          oldPattern, &oldRefPoint);
        GpiDestroyPS(psMem);
        DevCloseDC(dcMem);
    } else {

        oldColor = GpiQueryColor(hps);
        oldPattern = GpiQueryPattern(hps);
        rc = GpiSetColor(hps, gc->foreground);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetColor %x ERROR %x\n", gc->foreground,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetColor %x OK\n", gc->foreground);
        }
#endif
        rc = GpiSetPattern(hps, PATSYM_SOLID);
        rc = GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE
                         | LBB_END | LBB_JOIN, 0L, linePtr);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetAttrs color %x, width %d, type %x ERROR %x\n",
                   linePtr->lColor, linePtr->lGeomWidth,
                   linePtr->usType, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetAttrs color %x, width %d, type %x OK\n",
                   linePtr->lColor, linePtr->lGeomWidth,
                   linePtr->usType);
        }
#endif

        if (func == TOP_POLYGONS) {
            rc = GpiSetCurrentPosition(hps, os2Points);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiSetCurrentPosition %d,%d ERROR %x\n", os2Points[0].x,
                       os2Points[0].y, WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiSetCurrentPosition %d,%d OK\n",
                       os2Points[0].x, os2Points[0].y);
            }
#endif
            polygon.ulPoints = npoints-1;
            polygon.aPointl = os2Points+1;
            rc = GpiPolygons(hps, 1, &polygon, POLYGON_BOUNDARY |
                             (gc->fill_rule == EvenOddRule) ? POLYGON_ALTERNATE
                                                            : POLYGON_WINDING,
                             POLYGON_EXCL);
#ifdef VERBOSE
            if (rc == GPI_ERROR) {
                printf("Draw:GpiPolygons ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiPolygons OK\n");
            }
#endif
        } else { /* TOP_POLYLINE */
            rc = GpiSetCurrentPosition(hps, os2Points);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiSetCurrentPosition %d,%d ERROR %x\n",
                       os2Points[0].x, os2Points[0].y,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiSetCurrentPosition %d,%d OK\n",
                       os2Points[0].x, os2Points[0].y);
            }
#endif
            rc = GpiBeginPath(hps, 1);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiBeginPath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiBeginPath OK\n");
            }
#endif
            rc = GpiPolyLine(hps, npoints-1, os2Points+1);
#ifdef VERBOSE
            if (rc == GPI_ERROR) {
                printf("Draw:GpiPolyLine ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiPolyLine OK\n");
            }
#endif

            rc = GpiEndPath(hps);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiEndPath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiEndPath OK\n");
            }
#endif
            rc = GpiStrokePath(hps, 1, 0);
#ifdef VERBOSE
            if (rc == GPI_OK) {
                printf("Draw:GpiStrokePath OK\n");
            } else {
                printf("Draw:GpiStrokePath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            }
#endif
        }

        GpiSetColor(hps, oldColor);
        GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE, 0L,
                    &oldLineBundle);
        GpiSetPattern(hps, oldPattern);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a series of connected lines.
 *
 *----------------------------------------------------------------------
 */

void
XDrawLines(display, d, gc, points, npoints, mode)
    Display* display;
    Drawable d;
    GC gc;
    XPoint* points;
    int npoints;
    int mode;
{
    LINEBUNDLE oldLineBundle, newLineBundle;
    AREABUNDLE oldAreaBundle, newAreaBundle;
    TkOS2PSState state;
    HPS hps;

#ifdef VERBOSE
    printf("Draw:XDrawLines fg %x, bg %x, width %d\n", gc->foreground,
           gc->background, gc->line_width);
#endif
    
    if (d == None) {
        return;
    }

    hps = TkOS2GetDrawablePS(display, d, &state);

    SetUpGraphicsPort(hps, gc, &oldLineBundle, &newLineBundle, &oldAreaBundle,
                      &newAreaBundle);
    RenderObject(hps, gc, d, points, npoints, mode, &newLineBundle,
                 &newAreaBundle, TOP_POLYLINE);
    rc = GpiSetAttrs(hps, PRIM_LINE, LINE_ATTRIBUTES, 0L, &oldLineBundle);
    rc = GpiSetAttrs(hps, PRIM_AREA, AREA_ATTRIBUTES, 0L, &oldAreaBundle);
    
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XFillPolygon --
 *
 *	Draws a filled polygon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a filled polygon on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillPolygon(display, d, gc, points, npoints, shape, mode)
    Display* display;
    Drawable d;
    GC gc;
    XPoint* points;
    int npoints;
    int shape;
    int mode;
{
    LINEBUNDLE oldLineBundle, newLineBundle;
    AREABUNDLE oldAreaBundle, newAreaBundle;
    TkOS2PSState state;
    HPS hps;

#ifdef VERBOSE
    printf("Draw:XFillPolygon\n");
#endif

    if (d == None) {
        return;
    }

    hps = TkOS2GetDrawablePS(display, d, &state);

    SetUpGraphicsPort(hps, gc, &oldLineBundle, &newLineBundle, &oldAreaBundle,
                      &newAreaBundle);
    RenderObject(hps, gc, d, points, npoints, mode, &newLineBundle,
                 &newAreaBundle, TOP_POLYGONS);
    rc = GpiSetAttrs(hps, PRIM_LINE, LINE_ATTRIBUTES, 0L, &oldLineBundle);
    rc = GpiSetAttrs(hps, PRIM_AREA, AREA_ATTRIBUTES, 0L, &oldAreaBundle);

    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangle --
 *
 *	Draws a rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a rectangle on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XDrawRectangle(display, d, gc, x, y, width, height)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
{
    LINEBUNDLE lineBundle, oldLineBundle;
    TkOS2PSState state;
    LONG oldPattern;
    HPS hps;
    POINTL oldCurrent, changePoint;
    LONG windowHeight;

#ifdef VERBOSE
    printf("Draw:XDrawRectangle (%d,%d) %dx%d\n", x, y, width, height);
#endif

    if (d == None) {
        return;
    }

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)d);

    hps = TkOS2GetDrawablePS(display, d, &state);

    GpiQueryAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE,
                  &oldLineBundle);
    lineBundle.lColor = gc->foreground;
    lineBundle.lGeomWidth = gc->line_width;
    lineBundle.usType = LINETYPE_SOLID;
    GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE, 0L,
                &lineBundle);
    oldPattern = GpiQueryPattern(hps);
    GpiSetPattern(hps, PATSYM_NOSHADE);
    GpiSetMix(hps, tkpOS2MixModes[gc->function]);

    GpiQueryCurrentPosition(hps, &oldCurrent);
    changePoint.x = x;
    changePoint.y = windowHeight - y - height;
    GpiSetCurrentPosition(hps, &changePoint);
    /*
     * Now put other point of box in changePoint.
     */
    changePoint.x += width;
    changePoint.y += height;
    GpiBox(hps, DRO_OUTLINE, &changePoint, 0L, 0L);
    GpiSetCurrentPosition(hps, &oldCurrent);

    GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE, 0L,
                &oldLineBundle);
    GpiSetPattern(hps, oldPattern);
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArc --
 *
 *	Draw an arc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws an arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XDrawArc(display, d, gc, x, y, width, height, angle1, angle2)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    int angle1;
    int angle2;
{
#ifdef VERBOSE
    printf("Draw:XDrawArc d %x (%d,%d) %dx%d, a1 %d a2 %d\n", d, x, y, width, height,
           angle1, angle2);
#endif
    display->request++;

    DrawOrFillArc(display, d, gc, x, y, width, height, angle1, angle2, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArc --
 *
 *	Draw a filled arc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a filled arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillArc(display, d, gc, x, y, width, height, angle1, angle2)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    int angle1;
    int angle2;
{
#ifdef VERBOSE
    printf("Draw:XFillArc d %x (%d,%d) %dx%d, a1 %d a2 %d\n", d, x, y, width, height,
           angle1, angle2);
#endif
    display->request++;

    DrawOrFillArc(display, d, gc, x, y, width, height, angle1, angle2, 1);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawOrFillArc --
 *
 *	This procedure handles the rendering of drawn or filled
 *	arcs and chords.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the requested arc.
 *
 *----------------------------------------------------------------------
 */

static void
DrawOrFillArc(display, d, gc, x, y, width, height, start, extent, fill)
    Display *display;
    Drawable d;
    GC gc;
    int x, y;			/* left top */
    unsigned int width, height;
    int start;			/* start: three-o'clock (deg*64) */
    int extent;			/* extent: relative (deg*64) */
    int fill;			/* ==0 draw, !=0 fill */
{
    HPS hps;
    LONG oldColor, oldMix, oldPattern;
    LINEBUNDLE oldLineBundle, newLineBundle;
    AREABUNDLE oldAreaBundle, newAreaBundle;
    int sign;
    POINTL center, curPt;
    TkOS2PSState state;
    LONG windowHeight;
    ARCPARAMS arcParams, oldArcParams;
    double a1sin, a1cos;
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;
    POINTL refPoint;

    if (d == None) {
        return;
    }
    a1sin = sin(XAngleToRadians(start));
    a1cos = cos(XAngleToRadians(start));
    windowHeight = TkOS2WindowHeight(todPtr);

#ifdef VERBOSE
    printf("Draw:DrawOrFillArc %d,%d (PM %d), %dx%d, c %x, a1 %d (%f), a2 %d, %s, %s, %s\n",
           x, y, windowHeight - y, width, height, gc->foreground, start,
           XAngleToRadians(start), extent, fill ? "fill" : "nofill",
           gc->arc_mode == ArcChord ? "Chord" : "Arc/Pie",
           ((gc->fill_style == FillStippled || gc->fill_style == FillOpaqueStippled)
             && gc->stipple != None) ? "stippled " : "not stippled");
#endif

    /* Translate the Y coordinates to PM coordinates */
    y = windowHeight - y;
    /* Translate angles back to positive degrees */
    start = abs(start / 64);
    if (extent < 0) {
        sign = -1;
        /*
         * Not only the sweep but also the starting angle gets computed
         * counter-clockwise when Arc Param Q is negative (p*q actually).
         */
        start = 360 - start;
    }
    else {
        sign = 1;
    }
    extent = abs(extent / 64);

    hps = TkOS2GetDrawablePS(display, d, &state);

    /*
     * Now draw a filled or open figure.
     */

    SetUpGraphicsPort(hps, gc, &oldLineBundle, &newLineBundle, &oldAreaBundle,
                      &newAreaBundle);

    if ((gc->fill_style == FillStippled || gc->fill_style == FillOpaqueStippled)
        && gc->stipple != None) {
        HBITMAP bitmap, oldBitmap;
        BITMAPINFOHEADER2 bmpInfo;
        LONG rc;
        DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
        SIZEL sizl = {0,0}; /* use same page size as device */
        HDC dcMem;
        HPS psMem;
        POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
        POINTL oldRefPoint;

        todPtr = (TkOS2Drawable *)gc->stipple;

        if (todPtr->type != TOD_BITMAP) {
            panic("unexpected drawable type in stipple");
        }

        /*
         * Select stipple pattern into destination dc.
         */
        /* Translate Xlib y to PM y */
        refPoint.x = gc->ts_x_origin;
        refPoint.y = windowHeight - gc->ts_y_origin;

        dcMem = DevOpenDC(TclOS2GetHAB(), OD_MEMORY, (PSZ)"*", 5L,
                          (PDEVOPENDATA)&dop, NULLHANDLE);
        if (dcMem == DEV_ERROR) {
#ifdef VERBOSE
            printf("Draw:DevOpenDC ERROR in DrawOrFillArc\n");
#endif
            return;
        }
#ifdef VERBOSE
        printf("Draw:DevOpenDC in DrawOrFillArc returns %x\n", dcMem);
#endif
        psMem = GpiCreatePS(TclOS2GetHAB(), dcMem, &sizl,
                            PU_PELS | GPIT_NORMAL | GPIA_ASSOC);
        if (psMem == GPI_ERROR) {
#ifdef VERBOSE
            printf("Draw:GpiCreatePS ERROR in DrawOrFillArc: %x\n",
                   WinGetLastError(TclOS2GetHAB()));
#endif
            DevCloseDC(dcMem);
            return;
        }
#ifdef VERBOSE
        printf("Draw:GpiCreatePS in DrawOrFillArc returns %x\n", psMem);
#endif

        rc = GpiQueryArcParams(psMem, &oldArcParams);
        arcParams.lP = width / 2;
        arcParams.lQ = sign * (height / 2);
        arcParams.lR = 0;
        arcParams.lS = 0;
        rc = GpiSetArcParams(psMem, &arcParams);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetArcParams p %d q %d (%d) ERROR %x\n", arcParams.lP,
                   arcParams.lQ, arcParams.lP * arcParams.lQ,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetArcParams p %d q %d (%d) OK\n", arcParams.lP,
                   arcParams.lQ, arcParams.lP * arcParams.lQ);
        }
#endif

        /*
         * Draw the object in the foreground color and copy it to the
         * destination wherever the pattern is set.
         */

        rc = GpiSetColor(psMem, gc->foreground);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetColor %x ERROR %x\n", gc->foreground,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetColor %x OK\n", gc->foreground);
        }
#endif

    /*
     * X filling includes top and left sides, excludes bottom and right sides.
     * PM filling (WinFillRect) and BitBlt-ing (GpiBitBlt) includes bottom and
     * left sides, excludes top and right sides.
     * NB! X fills a box exactly as wide and high as width and height specify,
     * while PM cuts one pixel off the right and top.
     * => decrement y (X Window System) by one / increment y (PM) by one AND
     *    increment height by one, and increment width by one.
     */

        bmpInfo.cbFix = 16L;
        /* Bitmap must be able to contain a thicker line! */
        bmpInfo.cx = width + gc->line_width + 1;
        bmpInfo.cy = height + gc->line_width + 1;
        bmpInfo.cPlanes = 1;
        bmpInfo.cBitCount = display->screens[display->default_screen].root_depth;
        bitmap = GpiCreateBitmap(psMem, &bmpInfo, 0L, NULL, NULL);
#ifdef VERBOSE
        if (bitmap == GPI_ERROR) {
            printf("Draw:GpiCreateBitmap (%d,%d) GPI_ERROR %x\n", bmpInfo.cx,
                   bmpInfo.cy, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiCreateBitmap (%d,%d) returned %x\n", bmpInfo.cx,
                   bmpInfo.cy, bitmap);
        }
#endif
        oldBitmap = GpiSetBitmap(psMem, bitmap);
#ifdef VERBOSE
        if (bitmap == HBM_ERROR) {
            printf("Draw:GpiSetBitmap (%x) HBM_ERROR %x\n", bitmap,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetBitmap %x returned %x\n", bitmap, oldBitmap);
        }
#endif

        TkOS2SelectPalette(psMem, HWND_DESKTOP, todPtr->bitmap.colormap);

        /* Line width! */
        aPoints[0].x = 0;
        aPoints[0].y = 0;
        aPoints[1].x = width + gc->line_width + 1;
        aPoints[1].y = height + gc->line_width + 1;
        aPoints[2].x = x - (gc->line_width/2);
        aPoints[2].y = y - height + 1 - (gc->line_width/2);

        rc = GpiBitBlt(psMem, hps, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("Draw: GpiBitBlt SRCCOPY %x->%x ERROR %x\n", hps, psMem,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw: GpiBitBlt SRCCOPY %x->%x OK, aPoints (%d,%d)(%d,%d) (%d,%d)\n", hps,
                   psMem, aPoints[0].x, aPoints[0].y, aPoints[1].x,
                   aPoints[1].y, aPoints[2].x, aPoints[2].y);
        }
#endif

        /* The bitmap mustn't be selected in the HPS */
        TkOS2SetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                        refPoint.x, refPoint.y, &oldPattern, &oldRefPoint);
        /* Drawing */
        /* Center of arc is at x+(0.5*width),y-(0.5*height) */
        /* Translate to 0,0 for rendering in psMem */
        center.x = (0.5 * width) + (gc->line_width/2);
        center.y = (0.5 * height) + (gc->line_width/2);
/*
        newLineBundle.lColor = gc->foreground;
        newLineBundle.lGeomWidth = gc->line_width;
        newLineBundle.usType = LINETYPE_SOLID;
        rc = GpiSetAttrs(psMem, PRIM_LINE,
                         LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE,
                         0L, &newLineBundle);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetAttrs ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetAttrs OK\n");
        }
#endif
        aBundle.lColor = gc->foreground;
        rc = GpiSetAttrs(psMem, PRIM_AREA, LBB_COLOR, 0L, (PBUNDLE)&aBundle);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("Draw:GpiSetAttrs areaColor %d ERROR %x\n", aBundle.lColor,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetAttrs areaColor %d OK\n", aBundle.lColor);
        }
#endif
*/
        if (!fill) {
            rc= GpiSetBackMix(psMem, BM_LEAVEALONE);
            curPt.x = center.x + (int) (0.5 * width * a1cos);
            curPt.y = center.y + (int) (0.5 * height * a1sin);
            rc = GpiSetCurrentPosition(psMem, &curPt);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiSetCurrentPosition %d,%d -> %d,%d ERROR %x\n",
                       center.x, center.y, curPt.x, curPt.y,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiSetCurrentPosition %d,%d -> %d,%d OK\n",
                       center.x, center.y, curPt.x, curPt.y);
            }
#endif
            rc = GpiBeginPath(psMem, 1);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiBeginPath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiBeginPath OK\n");
            }
#endif
            rc= GpiPartialArc(psMem, &center, MAKEFIXED(1, 0),
                              MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
#ifdef VERBOSE
            if (rc == GPI_ERROR) {
                printf("Draw:GpiPartialArc a1 %d, a2 %d ERROR %x\n", start, extent,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiPartialArc a1 %d, a2 %d OK\n", start, extent);
            }
#endif
            rc = GpiEndPath(psMem);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiEndPath ERROR %x\n",WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiEndPath OK\n");
            }
#endif
            rc = GpiStrokePath(psMem, 1, 0);
#ifdef VERBOSE
            if (rc == GPI_OK) {
                printf("Draw:GpiStrokePath OK\n");
            } else {
                printf("Draw:GpiStrokePath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            }
#endif
        } else {
            curPt.x = center.x + (int) (0.5 * width * a1cos);
            curPt.y = center.y + (int) (0.5 * height * a1sin);
            rc = GpiSetCurrentPosition(psMem, &curPt);
            if (gc->arc_mode == ArcChord) {
                /* Chord */
                /*
                 * See GPI reference: first do GpiPartialArc with invisible,
                 * line then again with visible line, in an Area for filling.
                 */
                rc = GpiSetLineType(psMem, LINETYPE_INVISIBLE);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:GpiSetLineType ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiSetLineType OK\n");
                }
#endif
                rc = GpiPartialArc(psMem, &center, MAKEFIXED(1, 0),
                                   MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d ERROR %x\n", start,
                           extent, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d OK\n", start, extent);
                }
#endif
                rc = GpiSetLineType(psMem, LINETYPE_SOLID);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:GpiSetLineType ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiSetLineType OK\n");
                }
#endif
                rc = GpiBeginArea(psMem, BA_NOBOUNDARY|BA_ALTERNATE);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:GpiBeginArea ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiBeginArea OK\n");
                }
#endif
                rc = GpiPartialArc(psMem, &center, MAKEFIXED(1, 0),
                                   MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d ERROR %x\n", start,
                           extent, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d OK\n", start, extent);
                }
#endif
                rc = GpiEndArea(psMem);
#ifdef VERBOSE
                if (rc == GPI_OK) {
                    printf("Draw:GpiEndArea OK\n");
                } else {
                    printf("Draw:GpiEndArea ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                }
#endif
            } else if ( gc->arc_mode == ArcPieSlice ) {
                /* Pie */
                rc = GpiSetCurrentPosition(psMem, &center);
                rc = GpiBeginArea(psMem, BA_NOBOUNDARY|BA_ALTERNATE);
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("Draw:GpiBeginArea ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiBeginArea OK\n");
                }
#endif
                rc = GpiPartialArc(psMem, &center, MAKEFIXED(1, 0),
                                   MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d ERROR %x\n", start,
                           extent, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d OK\n", start, extent);
                }
#endif
                rc = GpiLine(psMem, &center);
#ifdef VERBOSE
                if (rc == GPI_OK) {
                    printf("Draw:GpiLine OK\n");
                } else {
                    printf("Draw:GpiLine ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                }
#endif
                rc = GpiEndArea(psMem);
#ifdef VERBOSE
                if (rc == GPI_OK) {
                    printf("Draw:GpiEndArea OK\n");
                } else {
                    printf("Draw:GpiEndArea ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                }
#endif
            }
        }
        /* Translate the Y coordinates to PM coordinates */
        aPoints[0].x = x - (gc->line_width/2);
        aPoints[0].y = y - height + 1 - (gc->line_width/2);
        aPoints[1].x = x + width + 1 + (gc->line_width/2);
        aPoints[1].y = y + 2 + (gc->line_width/2);
        aPoints[2].x = 0;
        aPoints[2].y = 0;
        rc = GpiBitBlt(hps, psMem, 3, aPoints, COPYFG, BBO_IGNORE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("Draw: GpiBitBlt COPYFG %x (%d,%d)(%d,%d) <- (%d,%d) ERROR %x\n",
                   gc->foreground, aPoints[0].x, aPoints[0].y,
                   aPoints[1].x, aPoints[1].y,
                   aPoints[2].x, aPoints[2].y, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw: GpiBitBlt COPYFG %x (%d,%d)(%d,%d) <- (%d,%d) OK\n",
                   gc->foreground, aPoints[0].x, aPoints[0].y,
                   aPoints[1].x, aPoints[1].y,
                   aPoints[2].x, aPoints[2].y);
        }
#endif
        GpiSetAttrs(psMem, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE, 0L,
                    &oldLineBundle);
        /*
         * Destroy the temporary bitmap and restore the device context.
         */

        GpiSetBitmap(psMem, oldBitmap);
        GpiDeleteBitmap(bitmap);
        GpiDestroyPS(psMem);
        DevCloseDC(dcMem);
        /* The bitmap must be reselected in the HPS */
        TkOS2UnsetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                          oldPattern, &oldRefPoint);
    } else {

        /* Not stippled */

        rc = GpiQueryArcParams(hps, &oldArcParams);
        arcParams.lP = width / 2;
        arcParams.lQ = sign * (height / 2);
        arcParams.lR = 0;
        arcParams.lS = 0;
        rc = GpiSetArcParams(hps, &arcParams);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetArcParams p %d q %d (%d) ERROR %x\n", arcParams.lP,
                   arcParams.lQ, arcParams.lP * arcParams.lQ,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetArcParams p %d q %d (%d) OK\n", arcParams.lP,
                   arcParams.lQ, arcParams.lP * arcParams.lQ);
        }
#endif

        /* Center of arc is at x+(0.5*width),y-(0.5*height) */
        center.x = x + (0.5 * width);
        center.y = y - (0.5 * height);	/* PM y coordinate reversed */
/*
        newLineBundle.lColor = gc->foreground;
        newLineBundle.lGeomWidth = gc->line_width;
        newLineBundle.usType = LINETYPE_SOLID;
        rc = GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE,
                         0L, &newLineBundle);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("Draw:GpiSetAttrs ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("Draw:GpiSetAttrs OK\n");
        }
#endif
*/
        if (!fill) {
            /* direction of arc is determined by arc parameters, while angles
             * are always positive
             * p*q > r*s -> direction counterclockwise
             * p*q < r*s -> direction clockwise
             * p*q = r*s -> straight line
             * When comparing the Remarks for function GpiSetArcParams in the
             * GPI Guide and Reference with the Xlib Programming Manual
             * (Fig.6-1), * the 3 o'clock point of the unit arc is defined by
             * (p,s) and the 12 * o'clock point by (r,q), when measuring from
             * (0,0) -> (cx+p, cy+s) and * (cx+r, cy+q) from center of arc at
             * (cx, cy). => p = 0.5 width, q = (sign*)0.5 height, r=s=0
             * GpiPartialArc draws a line from the current point to the start
             * of the partial arc, so we have to set the current point to it
             * first.
             * this is (cx+0.5*width*cos(start), cy+0.5*height*sin(start))
             */
            curPt.x = center.x + (int) (0.5 * width * a1cos);
            curPt.y = center.y + (int) (0.5 * height * a1sin);
            rc = GpiSetCurrentPosition(hps, &curPt);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiSetCurrentPosition %d,%d -> %d,%d ERROR %x\n",
                       center.x, center.y, curPt.x, curPt.y,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiSetCurrentPosition %d,%d -> %d,%d OK\n",
                       center.x, center.y, curPt.x, curPt.y);
            }
#endif
            rc = GpiBeginPath(hps, 1);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiBeginPath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiBeginPath OK\n");
            }
#endif
            rc= GpiPartialArc(hps, &center, MAKEFIXED(1, 0),
                              MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
#ifdef VERBOSE
            if (rc == GPI_ERROR) {
                printf("Draw:GpiPartialArc a1 %d, a2 %d ERROR %x\n", start, extent,
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiPartialArc a1 %d, a2 %d OK\n", start, extent);
            }
#endif
            rc = GpiEndPath(hps);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("Draw:GpiEndPath ERROR %x\n",WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("Draw:GpiEndPath OK\n");
            }
#endif
            rc = GpiStrokePath(hps, 1, 0);
#ifdef VERBOSE
            if (rc == GPI_OK) {
                printf("Draw:GpiStrokePath OK\n");
            } else {
                printf("Draw:GpiStrokePath ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            }
#endif
        } else {
            curPt.x = center.x + (int) (0.5 * width * a1cos);
            curPt.y = center.y + (int) (0.5 * height * a1sin);
            rc = GpiSetCurrentPosition(hps, &curPt);
            if (gc->arc_mode == ArcChord) {
                /* Chord */
                /*
                 * See GPI reference: first do GpiPartialArc with invisible
                 * line, then again with visible line, in an Area for filling.
                 */
                GpiSetLineType(hps, LINETYPE_INVISIBLE);
                GpiPartialArc(hps, &center, MAKEFIXED(1, 0),
                              MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
                GpiSetLineType(hps, LINETYPE_SOLID);
                rc = GpiBeginArea(hps, BA_NOBOUNDARY|BA_ALTERNATE);
#ifdef VERBOSE
                    if (rc != TRUE) {
                    printf("Draw:GpiBeginArea ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiBeginArea OK\n");
                }
#endif
                rc = GpiPartialArc(hps, &center, MAKEFIXED(1, 0),
                                   MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d ERROR %x\n", start,
                           extent, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPartialArc a1 %d, a2 %d OK\n", start, extent);
                }
#endif
                rc = GpiEndArea(hps);
#ifdef VERBOSE
                if (rc == GPI_OK) {
                    printf("Draw:GpiEndArea OK\n");
                } else {
                    printf("Draw:GpiEndArea ERROR %x\n",
                           WinGetLastError(TclOS2GetHAB()));
                }
#endif
            } else if ( gc->arc_mode == ArcPieSlice ) {
                /* Pie */
                GpiSetCurrentPosition(hps, &center);
                GpiBeginArea(hps, BA_NOBOUNDARY|BA_ALTERNATE);
                rc = GpiPartialArc(hps, &center, MAKEFIXED(1, 0),
                                   MAKEFIXED(start, 0), MAKEFIXED(extent, 0));
#ifdef VERBOSE
                if (rc == GPI_ERROR) {
                    printf("Draw:GpiPartialArc start %d, extent %d ERROR %x\n",
                           start, extent, WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("Draw:GpiPartialArc start %d, extent %d OK\n", start,
                           extent);
                }
#endif
                GpiLine(hps, &center);
                GpiEndArea(hps);
            }
        }
        GpiSetAttrs(hps, PRIM_LINE, LBB_COLOR | LBB_GEOM_WIDTH | LBB_TYPE, 0L,
                    &oldLineBundle);
    } /* not Stippled */
    rc = GpiSetAttrs(hps, PRIM_LINE, LINE_ATTRIBUTES, 0L, &oldLineBundle);
    rc = GpiSetAttrs(hps, PRIM_AREA, AREA_ATTRIBUTES, 0L, &oldAreaBundle);
    rc = GpiSetArcParams(hps, &oldArcParams);
    TkOS2ReleaseDrawablePS(d, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * SetUpGraphicsPort --
 *
 *      Set up the graphics port from the given GC.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The current port is adjusted.
 *
 *----------------------------------------------------------------------
 */

static BOOL
SetUpGraphicsPort(hps, gc, oldLinePtr, newLinePtr, oldAreaPtr, newAreaPtr)
    HPS hps;
    GC gc;
    PLINEBUNDLE oldLinePtr;
    PLINEBUNDLE newLinePtr;
    PAREABUNDLE oldAreaPtr;
    PAREABUNDLE newAreaPtr;
{
    /* Determine old values of line and area attributes */
    rc = GpiQueryAttrs(hps, PRIM_LINE, LINE_ATTRIBUTES, oldLinePtr);
    rc = GpiQueryAttrs(hps, PRIM_AREA, AREA_ATTRIBUTES, oldAreaPtr);

    /* By default use the same values */
    memcpy((void *)newLinePtr, (void *)oldLinePtr, sizeof(LINEBUNDLE));
    memcpy((void *)newAreaPtr, (void *)oldAreaPtr, sizeof(AREABUNDLE));

    /* Values for both lines and areas */
    newLinePtr->lColor = gc->foreground;
    newLinePtr->lBackColor = gc->background;
    newLinePtr->usMixMode = tkpOS2MixModes[gc->function];
    newLinePtr->lGeomWidth = gc->line_width;

    newAreaPtr->lColor = gc->foreground;
    newAreaPtr->lBackColor = gc->background;
    newAreaPtr->usMixMode = tkpOS2MixModes[gc->function];

    /* Determinev values to be changed */
    if (gc->line_style == LineOnOffDash) {
        unsigned char *p = (unsigned char *) &(gc->dashes);
                                /* pointer to the dash-list */

        /*
         * Below is a simple translation of serveral dash patterns
         * to valid windows pen types. Far from complete,
         * but I don't know how to do it better.
         * Any ideas: <mailto:j.nijtmans@chello.nl>
         */

        if (p[1] && p[2]) {
            if (!p[3] || p[4]) {
                newLinePtr->usType = LINETYPE_DASHDOUBLEDOT;  /*   -..   */
            } else {
                newLinePtr->usType = LINETYPE_DASHDOT;        /*   -.    */
            }
        } else {
            if (p[0] > (8 * gc->line_width)) {
                newLinePtr->usType = LINETYPE_LONGDASH;       /*   --    */
            } else {
                if (p[0] > (4 * gc->line_width)) {
                    newLinePtr->usType = LINETYPE_SHORTDASH;  /*   -     */
                } else {
                    newLinePtr->usType = LINETYPE_DOT;        /*   .     */
                }
            }
        }
    } else {
        newLinePtr->usType = LINETYPE_SOLID;
    }

    switch (gc->cap_style) {
        case CapNotLast:
        case CapButt:
            newLinePtr->usEnd = LINEEND_FLAT;
            break;
        case CapRound:
            newLinePtr->usEnd = LINEEND_ROUND;
            break;
        default:
            newLinePtr->usEnd = LINEEND_SQUARE;
            break;
    }
    switch (gc->join_style) {
        case JoinMiter:
            newLinePtr->usJoin = LINEJOIN_MITRE;
            break;
        case JoinRound:
            newLinePtr->usJoin = LINEJOIN_ROUND;
            break;
        default:
            newLinePtr->usJoin = LINEJOIN_BEVEL;
            break;
    }

    rc = GpiSetAttrs(hps, PRIM_LINE, LINE_ATTRIBUTES, 0L, newLinePtr);
    rc = GpiSetAttrs(hps, PRIM_AREA, AREA_ATTRIBUTES, 0L, newAreaPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangle of the specified window and accumulate
 *	a damage region.
 *
 * Results:
 *	Returns 0 if the scroll genereated no additional damage.
 *	Otherwise, sets the region that needs to be repainted after
 *	scrolling and returns 1.
 *
 * Side effects:
 *	Scrolls the bits in the window.
 *
 *----------------------------------------------------------------------
 */

int
TkScrollWindow(tkwin, gc, x, y, width, height, dx, dy, damageRgn)
    Tk_Window tkwin;		/* The window to be scrolled. */
    GC gc;			/* GC for window to be scrolled. */
    int x, y, width, height;	/* Position rectangle to be scrolled. */
    int dx, dy;			/* Distance rectangle should be moved. */
    TkRegion damageRgn;		/* Region to accumulate damage in. */
{
    HWND hwnd = TkOS2GetHWND(Tk_WindowId(tkwin));
    RECTL scrollRect;
    LONG lReturn;
    LONG windowHeight;

#ifdef VERBOSE
    printf("Draw:TkScrollWindow (%d,%d) %dx%d for %d,%d\n", x, y, width, height, dx,
           dy);
#endif

    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)Tk_WindowId(tkwin));

    /* Translate the Y coordinates to PM coordinates */
    y = windowHeight - y;
    dy = -dy;
    scrollRect.xLeft = x;
    scrollRect.yTop = y;
    scrollRect.xRight = x + width;
    scrollRect.yBottom = y - height;	/* PM coordinate reversed */
    /* Hide cursor, just in case */
    WinShowCursor(hwnd, FALSE);
    lReturn = WinScrollWindow(hwnd, dx, dy, &scrollRect, NULL, (HRGN) damageRgn,
                              NULL, 0);
    /* Show cursor again */
    WinShowCursor(hwnd, TRUE);
    return ( lReturn == RGN_NULL ? 0 : 1);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawHighlightBorder --
 *
 *      This procedure draws a rectangular ring around the outside of
 *      a widget to indicate that it has received the input focus.
 *
 *      On Windows, we just draw the simple inset ring.  On other sytems,
 *      e.g. the Mac, the focus ring is a little more complicated, so we
 *      need this abstraction.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A rectangle "width" pixels wide is drawn in "drawable",
 *      corresponding to the outer area of "tkwin".
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawHighlightBorder(tkwin, fgGC, bgGC, highlightWidth, drawable)
    Tk_Window tkwin;
    GC fgGC;
    GC bgGC;
    int highlightWidth;
    Drawable drawable;
{
    TkDrawInsetFocusHighlight(tkwin, fgGC, highlightWidth, drawable, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2SetStipple --
 *
 *	Set the pattern set of a HPS to a "stipple" (bitmap).
 *
 * Results:
 *	Returns the old pattern set and reference point.
 *
 * Side effects:
 *	Unsets the bitmap in/from "its" HPS, appoints a bitmap ID to it,
 *	sets that ID as the pattern set, with its reference point as given.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2SetStipple(dstPS, bmpPS, stipple, x, y, oldPatternSet, oldRefPoint)
    HPS dstPS;		/* The HPS to receive the stipple. */
    HPS bmpPS;		/* The HPS of the stipple-bitmap. */
    HBITMAP stipple;	/* Stipple-bitmap. */
    LONG x, y;			/* Reference point for the stipple. */
    LONG *oldPatternSet;	/* Pattern set that was in effect in the HPS. */
    PPOINTL oldRefPoint;	/* Reference point that was in effect. */
{
    POINTL refPoint;

#ifdef VERBOSE
    printf("Draw:TkOS2SetStipple dstPS %x, bmpPS %x, stipple %x, (%d,%d)\n", dstPS,
           bmpPS, stipple, x, y);
#endif
    refPoint.x = x;
    refPoint.y = y;
    rc = GpiQueryPatternRefPoint(dstPS, oldRefPoint);
#ifdef VERBOSE
    if (rc!=TRUE) {
        printf("Draw:    GpiQueryPatternRefPoint ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    } else  {
        printf("Draw:    GpiQueryPatternRefPoint OK: %d,%d\n", oldRefPoint->x,
               oldRefPoint->y);
    }
#endif
    rc = GpiSetPatternRefPoint(dstPS, &refPoint);
#ifdef VERBOSE
    if (rc!=TRUE) {
        printf("Draw:    GpiSetPatternRefPoint ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("Draw:    GpiSetPatternRefPoint %d,%d OK\n", refPoint.x, refPoint.y);
    }
#endif
    *oldPatternSet = GpiQueryPatternSet(dstPS);
#ifdef VERBOSE
    if (rc==LCID_ERROR) {
        printf("Draw:    GpiQueryPatternSet ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("Draw:    GpiQueryPatternSet %x\n", oldPatternSet);
    }
#endif
    GpiSetBitmap(bmpPS, NULLHANDLE);
    rc = GpiSetBitmapId(dstPS, stipple, 254L);
#ifdef VERBOSE
    if (rc!=TRUE) {
        printf("Draw:    GpiSetBitmapId %x ERROR %x\n", stipple,
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("Draw:    GpiSetBitmapId %x OK\n", stipple);
    }
#endif
    rc = GpiSetPatternSet(dstPS, 254L);
#ifdef VERBOSE
    if (rc!=TRUE) {
        printf("Draw:    GpiSetPatternSet ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("Draw:    GpiSetPatternSet OK\n");
    }
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2UnsetStipple --
 *
 *	Unset the "stipple" (bitmap) from a HPS.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resets the pattern set and refpoint of the hps to their original
 *	(given) values and reassociates the bitmap with its "own" HPS.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2UnsetStipple(dstPS, bmpPS, stipple, oldPatternSet, oldRefPoint)
    HPS dstPS;		/* The HPS to give up the stipple. */
    HPS bmpPS;		/* The HPS of the stipple-bitmap. */
    HBITMAP stipple;	/* Stipple-bitmap. */
    LONG oldPatternSet;		/* Pattern set to be put back in effect. */
    PPOINTL oldRefPoint;	/* Reference point to put back in effect. */
{
#ifdef VERBOSE
    printf("Draw:TkOS2UnsetStipple dstPS %x, bmpPS %x, stipple %x, oldRP %d,%d\n",
           dstPS, bmpPS, stipple, oldRefPoint->x, oldRefPoint->y);
#endif
    rc = GpiSetPatternSet(dstPS, oldPatternSet);
    rc = GpiSetPatternRefPoint(dstPS, oldRefPoint);

    rc = GpiDeleteSetId(dstPS, 254L);
    /* end of using 254 */
    /* The bitmap must be reselected in the HPS */
    GpiSetBitmap(bmpPS, stipple);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2FillRect --
 *
 *      This routine fills a rectangle with the foreground color
 *      from the specified GC ignoring all other GC values.  This
 *      is the fastest way to fill a drawable with a solid color.
 *	NB: Y in PM coordinates.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies the contents of the PS drawing surface.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2FillRect(hps, x, y, width, height, pixel)
    HPS hps;
    LONG x, y, width, height;
    LONG pixel;
{
    RECTL rect;
#ifdef VERBOSE
    printf("Draw:TkOS2FillRect hps %x, (%d,%d) %dx%d, pixel %x\n", hps, x, y, width,
           height, pixel);
#endif

    rect.xLeft = x;
    rect.xRight = x + width;
    rect.yBottom = y;
    rect.yTop = y + height;

    rc = WinFillRect(hps, &rect, pixel);
#ifdef VERBOSE
    if (rc==TRUE) {
        printf("Draw:    WinFillRect(hps %x, fg %x, (%d,%d)(%d,%d)) OK\n",
               hps, pixel, rect.xLeft, rect.yBottom, rect.xRight, rect.yTop);
    } else {
        printf("Draw:    WinFillRect(hps %x, fg %x, (%d,%d)(%d,%d)) ERROR %x\n",
               hps, pixel, rect.xLeft, rect.yBottom, rect.xRight, rect.yTop,
               WinGetLastError(TclOS2GetHAB()));
    }
#endif
}

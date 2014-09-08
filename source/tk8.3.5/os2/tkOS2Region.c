/* 
 * tkOS2Region.c --
 *
 *	Tk Region emulation code.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"



/*
 *----------------------------------------------------------------------
 *
 * TkCreateRegion --
 *
 *	Construct an empty region.
 *
 * Results:
 *	Returns a new region handle.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkRegion
TkCreateRegion()
{
    HRGN region;
    HPS hps;

    hps= WinGetPS(HWND_DESKTOP);
    region = GpiCreateRegion(hps, 0, NULL);
    WinReleasePS(hps);
#ifdef VERBOSE
    printf("TkCreateRegion region %x, hps %x\n", region, hps);
#endif
    return (TkRegion) region;
}

/*
 *----------------------------------------------------------------------
 *
 * TkDestroyRegion --
 *
 *	Destroy the specified region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the storage associated with the specified region.
 *
 *----------------------------------------------------------------------
 */

void
TkDestroyRegion(r)
    TkRegion r;
{
    HPS hps= WinGetPS(HWND_DESKTOP);
#ifdef VERBOSE
    printf("TkDestroyRegion %x\n", r);
#endif
    GpiDestroyRegion(hps, (HRGN) r);
    WinReleasePS(hps);
}

/*
 *----------------------------------------------------------------------
 *
 * TkClipBox --
 *
 *	Computes the bounding box of a region.
 *
 * Results:
 *	Sets rect_return to the bounding box of the region.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkClipBox(r, rect_return)
    TkRegion r;
    XRectangle* rect_return;
{
    RECTL rect;
    HPS hps;

    hps = WinGetPS(HWND_DESKTOP);
    rc = GpiQueryRegionBox(hps, (HRGN)r, &rect);
    WinReleasePS(hps);
    if (rc == RGN_ERROR) {
#ifdef VERBOSE
        printf("TkClipBox: GpiQueryRegionBox %x returns RGN_ERROR\n", r);
#endif
        return;
    }
#ifdef VERBOSE
    printf("TkClipBox: GpiQueryRegionBox %x %s\n", r,
           rc == RGN_NULL ? "RGN_NULL" :
           (rc == RGN_RECT ? "RGN_RECT" :
           (rc == RGN_COMPLEX ? "RGN_COMPLEX" :
           "RGN_ERROR")));
#endif
    rect_return->x = (short) rect.xLeft;
    rect_return->width = (short) rect.xRight - rect.xLeft;
    /* Return the Y coordinate that X expects (top) */
    rect_return->y = (short) yScreen - rect.yTop;
    /* PM coordinates are just reversed, translate */
    rect_return->height = (short) rect.yTop - rect.yBottom;
#ifdef VERBOSE
    printf("          x %d y %d w %d h %d\n", rect_return->x, rect_return->y,
           rect_return->width, rect_return->height);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkIntersectRegion --
 *
 *	Compute the intersection of two regions.
 *
 * Results:
 *	Returns the result in the dr_return region.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkIntersectRegion(sra, srb, dr_return)
    TkRegion sra;
    TkRegion srb;
    TkRegion dr_return;
{
    HPS hps= WinGetPS(HWND_DESKTOP);
    rc = GpiCombineRegion(hps, (HRGN)dr_return, (HRGN)sra, (HRGN)srb, CRGN_AND);
#ifdef VERBOSE
    printf("TkIntersectRegion %x %x %s\n", sra, srb,
           rc == RGN_NULL ? "RGN_NULL" :
           (rc == RGN_RECT ? "RGN_RECT" :
           (rc == RGN_COMPLEX ? "RGN_COMPLEX" : "RGN_ERROR")));
#endif
    WinReleasePS(hps);
}

/*
 *----------------------------------------------------------------------
 *
 * TkUnionRectWithRegion --
 *
 *	Create the union of a source region and a rectangle.
 *
 * Results:
 *	Returns the result in the dr_return region.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkUnionRectWithRegion(rectangle, src_region, dest_region_return)
    XRectangle* rectangle;
    TkRegion src_region;
    TkRegion dest_region_return;
{
    HRGN rectRgn;
    HPS hps;
    RECTL rect;

    hps= WinGetScreenPS(HWND_DESKTOP);
    rect.xLeft = rectangle->x;
    rect.xRight = rectangle->x + rectangle->width;
    /* Translate coordinates to PM */
    rect.yTop = yScreen - rectangle->y;
    rect.yBottom = rect.yTop - rectangle->height;
#ifdef VERBOSE
    printf("TkUnionRectWithRegion Xrect (%d,%d) %dx%d, src %x (PM %dx%d)\n",
           rectangle->x, rectangle->y, rectangle->width, rectangle->height,
           src_region, rect.xLeft, rect.yBottom);
#endif
    rectRgn = GpiCreateRegion(hps, 1, &rect);
    rc = GpiCombineRegion(hps, (HRGN) dest_region_return, (HRGN) src_region,
                          rectRgn, CRGN_OR);
#ifdef VERBOSE
    printf("    GpiCombineRegion %s\n", rc == RGN_NULL ? "RGN_NULL" :
                                       (rc == RGN_RECT ? "RGN_RECT" :
                                       (rc == RGN_COMPLEX ? "RGN_COMPLEX" :
                                        "RGN_ERROR")));
#endif
    GpiDestroyRegion(hps, rectRgn);
    WinReleasePS(hps);
}

/*
 *----------------------------------------------------------------------
 *
 * TkRectInRegion --
 *
 *	Test whether a given rectangle overlaps with a region.
 *
 * Results:
 *	Returns RectanglePart, RectangleIn or RectangleOut.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkRectInRegion(r, x, y, width, height)
    TkRegion r;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
{
    RECTL rect;
    LONG in;
    HPS hps;

    /* Translate coordinates to PM */
    rect.yTop = yScreen - y;
    rect.xLeft = x;
    rect.yBottom = rect.yTop - height;
    rect.xRight = x+width;
    hps= WinGetPS(HWND_DESKTOP);
    in = GpiRectInRegion(hps, (HRGN)r, &rect);
    WinReleasePS(hps);
    if (in == RRGN_INSIDE) {
        /* all in the region */
#ifdef VERBOSE
        printf("TkRectInRegion r %x (%d,%d) %dx%d RRGN_INSIDE (PM %d,%d)\n", r,
	       x, y, width, height, rect.xLeft, rect.yBottom);
#endif
        return RectangleIn;
    } else if (in == RRGN_PARTIAL) {
        /* partly in the region */
#ifdef VERBOSE
        printf("TkRectInRegion r %x (%d,%d) %dx%d RRGN_PARTIAL (PM %d,%d)\n", r,
	       x, y, width, height, rect.xLeft, rect.yBottom);
#endif
        return RectanglePart;
    } else {
        /* not in region or error */
#ifdef VERBOSE
        if (in == RRGN_OUTSIDE) {
           printf("TkRectInRegion r %x (%d,%d) %dx%d RRGN_OUTSIDE (PM %d,%d)\n",
	          r, x, y, width, height, rect.xLeft, rect.yBottom);
	} else if (in == RRGN_ERROR) {
           printf("TkRectInRegion r %x (%d,%d) %dx%d ERROR %x (PM %d,%d)\n",
	          r, x, y, width, height, WinGetLastError(TclOS2GetHAB()),
                  rect.xLeft, rect.yBottom);
        } else {
           printf("TkRectInRegion r %x (%d,%d) %dx%d UNKNOWN (PM %d,%d)\n",
	          r, x, y, width, height, rect.xLeft, rect.yBottom);
	}
#endif
        return RectangleOut;
    }
}

/* 
 * tkOS2Pixmap.c --
 *
 *	This file contains the Xlib emulation functions pertaining to
 *	creating and destroying pixmaps.
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
 * Tk_GetPixmap --
 *
 *	Creates an in memory drawing surface.
 *
 * Results:
 *	Returns a handle to a new pixmap.
 *
 * Side effects:
 *	Allocates a new OS/2 bitmap, hps, DC.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmap(display, d, width, height, depth)
    Display* display;
    Drawable d;
    int width;
    int height;
    int depth;
{
    TkOS2Drawable *newTodPtr, *todPtr;
    int planes;
    Screen *screen;
    BITMAPINFOHEADER2 bmpInfo;
    LONG rc;
    DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
    SIZEL sizl = {0,0}; /* use same page size as device */
    sizl.cx = width; sizl.cy = height;
    
    display->request++;

    newTodPtr = (TkOS2Drawable*) ckalloc(sizeof(TkOS2Drawable));
    if (newTodPtr == NULL) {
	return (Pixmap)None;
    }
#ifdef VERBOSE
    printf("    new todPtr (drawable) %x\n", todPtr);
#endif

    newTodPtr->type = TOD_BITMAP;
    newTodPtr->bitmap.depth = depth;
    todPtr = (TkOS2Drawable *)d;
    if (todPtr->type != TOD_BITMAP) {
#ifdef VERBOSE
        printf("XCreatePixmap %x, depth %d, parent %x, %dx%d\n", newTodPtr,
               depth, todPtr->window.handle, sizl.cx, sizl.cy);
#endif
        newTodPtr->bitmap.parent = todPtr->window.handle;
        if (todPtr->window.winPtr == NULL) {
            newTodPtr->bitmap.colormap = DefaultColormap(display,
                    DefaultScreen(display));
        } else {
            newTodPtr->bitmap.colormap = todPtr->window.winPtr->atts.colormap;
        }
    } else {
#ifdef VERBOSE
        printf("XCreatePixmap %x, depth %d, parent (bitmap) %x, %dx%d\n",
               newTodPtr, depth, todPtr->bitmap.parent, sizl.cx, sizl.cy);
#endif
        newTodPtr->bitmap.colormap = todPtr->bitmap.colormap;
        newTodPtr->bitmap.parent = todPtr->bitmap.parent;
    }

    screen = &(display->screens[0]);
#ifdef VERBOSE
    printf("XCreatePixmap: colormap %x, parent %x, screen %x\n",
           newTodPtr->bitmap.colormap, newTodPtr->bitmap.parent, screen);
#endif
    planes = 1;
    if (depth == screen->root_depth) {
        planes = (int) screen->ext_data;
        depth /= planes;
    }

    bmpInfo.cbFix = 16L;
    bmpInfo.cx = width;
    bmpInfo.cy = height;
    bmpInfo.cPlanes = planes;
    bmpInfo.cBitCount = depth;
    newTodPtr->bitmap.dc = DevOpenDC(TclOS2GetHAB(), OD_MEMORY, (PSZ)"*", 5L,
                                     (PDEVOPENDATA)&dop, NULLHANDLE);
    if (newTodPtr->bitmap.dc == DEV_ERROR) {
#ifdef VERBOSE
        printf("DevOpenDC failed in XCreatePixmap\n");
#endif
        ckfree((char *) newTodPtr);
        return (Pixmap)None;
    }
#ifdef VERBOSE
    printf("DevOpenDC in XCreatePixmap returns %x\n", newTodPtr->bitmap.dc);
#endif
    newTodPtr->bitmap.hps = GpiCreatePS(TclOS2GetHAB(), newTodPtr->bitmap.dc,
                                        &sizl,
                                        PU_PELS | GPIT_MICRO | GPIA_ASSOC);
    if (newTodPtr->bitmap.hps == GPI_ERROR) {
        DevCloseDC(newTodPtr->bitmap.dc);
#ifdef VERBOSE
        printf("GpiCreatePS failed in XCreatePixmap\n");
#endif
        ckfree((char *) newTodPtr);
        return (Pixmap)None;
    }
#ifdef VERBOSE
    printf("GpiCreatePS in XCreatePixmap returns %x\n", newTodPtr->bitmap.hps);
#endif
    newTodPtr->bitmap.handle = GpiCreateBitmap(newTodPtr->bitmap.hps,
                                               &bmpInfo, 0L, NULL, NULL);

    if (newTodPtr->bitmap.handle == NULLHANDLE) {
#ifdef VERBOSE
        printf("GpiCreateBitmap ERROR %x in XCreatePixmap\n",
               WinGetLastError(TclOS2GetHAB()));
#endif
	ckfree((char *) newTodPtr);
	return (Pixmap)None;
    }
#ifdef VERBOSE
    printf("GpiCreateBitmap in XCreatePixmap returns %x\n",
           newTodPtr->bitmap.handle);
#endif
/*
    sizl.cx = width;
    sizl.cy = height;
    rc = GpiSetBitmapDimension(newTodPtr->bitmap.handle, &sizl);
#ifdef VERBOSE
    if (rc == FALSE) {
        printf("    GpiSetBitmapDimension ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("    GpiSetBitmapDimension: %dx%d\n", sizl.cx, sizl.cy);
    }
    rc = GpiQueryBitmapDimension(newTodPtr->bitmap.handle, &sizl);
    if (rc == FALSE) {
        printf("    GpiQueryBitmapDimension ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("    GpiQueryBitmapDimension: %dx%d\n", sizl.cx, sizl.cy);
    }
#endif
*/
    rc = GpiSetBitmap(newTodPtr->bitmap.hps, newTodPtr->bitmap.handle);
    if (rc == HBM_ERROR) {
#ifdef VERBOSE
        printf("GpiSetBitmap returned HBM_ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
#endif
        GpiDestroyPS(newTodPtr->bitmap.hps);
        DevCloseDC(newTodPtr->bitmap.dc);
        ckfree((char *) newTodPtr);
        return (Pixmap)None;
    }
#ifdef VERBOSE
    else printf("GpiSetBitmap %x into hps %x returns %x\n",
                newTodPtr->bitmap.handle, newTodPtr->bitmap.hps, rc);
#endif

    return (Pixmap)newTodPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreePixmap --
 *
 *	Release the resources associated with a pixmap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes the bitmap created by Tk_FreePixmap.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreePixmap(display, pixmap)
    Display* display;
    Pixmap pixmap;
{
    TkOS2Drawable *todPtr = (TkOS2Drawable *) pixmap;
    HBITMAP hbm;

#ifdef VERBOSE
    printf("XFreePixmap %x\n", todPtr);
#endif

    display->request++;
    if (todPtr != NULL) {
        hbm = GpiSetBitmap(todPtr->bitmap.hps, NULLHANDLE);
#ifdef VERBOSE
        printf("    XFreePixmap GpiSetBitmap hps %x NULLHANDLE returned %x\n",
               todPtr->bitmap.hps, hbm);
#endif
	GpiDeleteBitmap(todPtr->bitmap.handle);
        GpiDestroyPS(todPtr->bitmap.hps);
        DevCloseDC(todPtr->bitmap.dc);
	ckfree((char *)todPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSetPixmapColormap --
 *
 *      The following function is a hack used by the photo widget to
 *      explicitly set the colormap slot of a Pixmap.
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
TkSetPixmapColormap(pixmap, colormap)
    Pixmap pixmap;
    Colormap colormap;
{
    TkOS2Drawable *todPtr = (TkOS2Drawable *)pixmap;

#ifdef VERBOSE
    printf("TkSetPixmapColormap, bitmap %x, colormap %x\n",
           TkOS2GetHBITMAP(todPtr), colormap);
#endif
    todPtr->bitmap.colormap = colormap;
    /*
    rc = (HPAL) TkOS2SelectPalette(todPtr->bitmap.hps, todPtr->bitmap.parent,
                                   colormap);
    */
}

/*
 *----------------------------------------------------------------------
 *
 * XGetGeometry --
 *
 *    Retrieve the geometry of the given drawable.  Note that
 *    this is a degenerate implementation that only returns the
 *    size of a pixmap or window.
 *
 * Results:
 *    Returns 0.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

int
XGetGeometry(display, d, root_return, x_return, y_return, width_return,
        height_return, border_width_return, depth_return)
    Display* display;
    Drawable d;
    Window* root_return;
    int* x_return;
    int* y_return;
    unsigned int* width_return;
    unsigned int* height_return;
    unsigned int* border_width_return;
    unsigned int* depth_return;
{
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;

    if (todPtr->type == TOD_BITMAP) {
        BITMAPINFOHEADER2 info;

        info.cbFix = sizeof(BITMAPINFOHEADER2);
        if (!GpiQueryBitmapInfoHeader(todPtr->bitmap.handle, &info)) {
            panic("XGetGeometry: unable to get bitmap size");
        }
        *width_return = info.cx;
        *height_return = info.cy;
    } else if (todPtr->type == TOD_WINDOW) {
        RECTL rectl;

        if (todPtr->window.handle == NULLHANDLE) {
            panic("XGetGeometry: invalid window");
        }
        rc = WinQueryWindowRect(todPtr->window.handle, &rectl);
        *width_return = rectl.xRight - rectl.xLeft;
        *height_return = rectl.yTop - rectl.yBottom;
    } else {
        panic("XGetGeometry: invalid window");
    }

    return 1;
}

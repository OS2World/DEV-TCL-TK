/* 
 * tkOS2Image.c --
 *
 *	This file contains routines for manipulation of full-color of images.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"

static int             DestroyImage _ANSI_ARGS_((XImage* data));
static unsigned long   ImageGetPixel _ANSI_ARGS_((XImage *image, int x, int y));
static int             PutPixel (XImage *image, int x, int y,
			    unsigned long pixel);

/*
 *----------------------------------------------------------------------
 *
 * DestroyImage --
 *
 *      This is a trivial wrapper around ckfree to make it possible to
 *      pass ckfree as a pointer.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Deallocates the image.
 *
 *----------------------------------------------------------------------
 */

int
DestroyImage(imagePtr)
     XImage *imagePtr;          /* image to free */
{
    if (imagePtr) {
        if (imagePtr->data) {
            ckfree((char*)imagePtr->data);
        }
        ckfree((char*)imagePtr);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageGetPixel --
 *
 *      Get a single pixel from an image.
 *
 * Results:
 *      Returns the 32 bit pixel value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static unsigned long
ImageGetPixel(image, x, y)
    XImage *image;
    int x, y;
{
    unsigned long pixel = 0;
    unsigned char *srcPtr = &(image->data[(y * image->bytes_per_line)
            + ((x * image->bits_per_pixel) / NBBY)]);

#ifdef VERBOSE
    printf("ImageGetPixel %x (%d,%d)\n", image, x, y);
#endif
/*
    switch (image->bits_per_pixel) {
        case 32:
        case 24:
            pixel = RGB(srcPtr[2], srcPtr[1], srcPtr[0]);
            break;
        case 16:
            pixel = RGB(((((WORD*)srcPtr)[0]) >> 7) & 0xf8,
                    ((((WORD*)srcPtr)[0]) >> 2) & 0xf8,
                    ((((WORD*)srcPtr)[0]) << 3) & 0xf8);
            break;
        case 8:
            pixel = srcPtr[0];
            break;
        case 4:
            pixel = ((x%2) ? (*srcPtr) : ((*srcPtr) >> 4)) & 0x0f;
            break;
        case 1:
            pixel = ((*srcPtr) & (0x80 >> (x%8))) ? 1 : 0;
            break;
    }
*/
    pixel = RGB(srcPtr[0], srcPtr[1], srcPtr[2]);
    return pixel;
}

/*
 *----------------------------------------------------------------------
 *
 * PutPixel --
 *
 *	Set a single pixel in an image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
PutPixel(image, x, y, pixel)
    XImage *image;
    int x, y;
    unsigned long pixel;
{
    unsigned char *destPtr;

#ifdef VERBOSE
    printf("PutPixel %x image %x (%d,%d) bytes_p_l %d, bits_p_p %d\n", pixel,
           image, x, y, image->bytes_per_line, image->bits_per_pixel);
#endif
    destPtr = &(image->data[(y * image->bytes_per_line)
              + ((x * image->bits_per_pixel) / NBBY)]);

/*
    rc = GpiQueryLogColorTable(globalPS, 0L, pixel, 1, &pixel);
*/
    destPtr[0] = destPtr[1] = destPtr[2] = 0;
    destPtr[0] = GetRValue(pixel);
    destPtr[1] = GetGValue(pixel);
    destPtr[2] = GetBValue(pixel);
#ifdef VERBOSE
    printf(" pixel now %x, RGB (%x,%x,%x) destPtr %x\n", pixel, destPtr[0],
           destPtr[1], destPtr[2], (ULONG)destPtr);
#endif

/*
    LONG *destPtr;

    destPtr = (LONG *) &(image->data[(y * image->bytes_per_line)
              + ((x * image->bits_per_pixel) / NBBY)]);
    *destPtr = pixel;
#ifdef VERBOSE
    printf("PutPixel %x image %x (%d,%d) bytes_p_l %d, bits_p_p %d: %x\n",
           pixel, image, x, y, image->bytes_per_line, image->bits_per_pixel,
           *(destPtr));
#endif
*/

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateImage --
 *
 *	Allocates storage for a new XImage.
 *
 * Results:
 *	Returns a newly allocated XImage.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XImage *
XCreateImage(display, visual, depth, format, offset, data, width, height,
	bitmap_pad, bytes_per_line)
    Display* display;
    Visual* visual;
    unsigned int depth;
    int format;
    int offset;
    char* data;
    unsigned int width;
    unsigned int height;
    int bitmap_pad;
    int bytes_per_line;
{
    XImage* imagePtr = (XImage *) ckalloc(sizeof(XImage));

    if (imagePtr) {
        imagePtr->width = width;
        imagePtr->height = height;
        imagePtr->xoffset = offset;
        imagePtr->format = format;
        imagePtr->data = data;
/******* LSBFirst?? */
        imagePtr->byte_order = MSBFirst;
        imagePtr->bitmap_unit = 32;
        imagePtr->bitmap_bit_order = LSBFirst;
        imagePtr->bitmap_pad = bitmap_pad;
        imagePtr->bits_per_pixel = 24;
        imagePtr->depth = depth;

        /*
         * Bitmap_pad must be on 4-byte boundary.
         */

#define LONGBITS    (sizeof(LONG) * 8)

        bitmap_pad = (bitmap_pad + LONGBITS - 1) / LONGBITS * LONGBITS;
#ifdef VERBOSE
    printf("XCreateImage bpp %d, depth %d, pad %d (was %d)\n",
           imagePtr->bits_per_pixel, imagePtr->depth, imagePtr->bitmap_pad,
           bitmap_pad);
#endif

        /*
         * Round to the nearest bitmap_pad boundary.
         */

        if (bytes_per_line) {
            imagePtr->bytes_per_line = bytes_per_line;
        } else {
            imagePtr->bytes_per_line = (((depth * width)
                    + (bitmap_pad - 1)) >> 3) & ~((bitmap_pad >> 3) - 1);
        }

        imagePtr->red_mask = 0;
        imagePtr->green_mask = 0;
        imagePtr->blue_mask = 0;

        imagePtr->f.put_pixel = PutPixel;
        imagePtr->f.get_pixel = ImageGetPixel;
        imagePtr->f.destroy_image = DestroyImage;
        imagePtr->f.create_image = NULL;
        imagePtr->f.sub_image = NULL;
        imagePtr->f.add_pixel = NULL;
    }
    
    return imagePtr;
}

/*
 *----------------------------------------------------------------------
 * XGetImageZPixmap --
 *
 *      This function copies data from a pixmap or window into an
 *      XImage.  This handles the ZPixmap case only.
 *
 * Results:
 *      Returns a newly allocated image containing the data from the
 *      given rectangle of the given drawable.
 *
 * Side effects:
 *      None.
 *
 * This procedure is adapted from the XGetImage implementation in TkNT.
 * That code is Copyright (c) 1994 Software Research Associates, Inc.
 *
 *----------------------------------------------------------------------
 */

static XImage *
XGetImageZPixmap(display, d, x, y, width, height, plane_mask, format)
    Display* display;
    Drawable d;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    unsigned long plane_mask;
    int format;
{
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;
    XImage *ret_image;
    HPS hps, hpsMem;
    HBITMAP hbmp, hbmpPrev;
    BITMAPINFO2 *bmpInfoPtr;
    BITMAPINFOHEADER2 bmpInfoHeader;
    HPAL hPal, hPalPrev1, hPalPrev2;
    int size;
    unsigned int n;
    unsigned int depth;
    unsigned char *data;
    TkOS2PSState state;
    ULONG planes, bitCount, physPalEntChanged;
    SIZEL sizel = {0,0};
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    APIRET rc;

    if (format != ZPixmap) {
        TkpDisplayWarning(
            "XGetImageZPixmap: only ZPixmap types are implemented",
            "XGetImageZPixmap Failure");
        return NULL;
    }

    hps = TkOS2GetDrawablePS(display, d, &state);

    /* Do a Blt operation to copy into a new bitmap */

    /* Create compatible bitmap, PS and set bitmap into PS */
    rc = DevQueryCaps(hps, CAPS_COLOR_PLANES, 1, &planes);
    rc = DevQueryCaps(hps, CAPS_COLOR_BITCOUNT, 1, &bitCount);
    hpsMem = GpiCreatePS(TclOS2GetHAB(), hScreenDC, &sizel, PU_PELS|GPIA_ASSOC);
    bmpInfoHeader.cbFix = 16L;
    bmpInfoHeader.cx = width;
    bmpInfoHeader.cy = height;
    bmpInfoHeader.cPlanes = planes;
    bmpInfoHeader.cBitCount = bitCount;
    hbmp = GpiCreateBitmap(hpsMem, &bmpInfoHeader, 0L, NULL, NULL);
#ifdef VERBOSE
    if (hbmp!=GPI_ERROR) {
        printf("GpiCreateBitmap hbmp OK (%x)\n", hbmp);
    } else {
        printf("GpiCreateBitmap hbmp GPI_ERROR, error %x\n",
               WinGetLastError(tkHab));
    }
#endif
    hbmpPrev = GpiSetBitmap(hpsMem, hbmp);
#ifdef VERBOSE
    if (hbmpPrev != GPI_ALTERROR) {
        printf("GpiSetBitmap hbmp OK\n");
    } else {
        printf("GpiSetBitmap hbmp GPI_ALTERROR, error %x\n",
               WinGetLastError(tkHab));
    }
#endif

    hPal = state.palette;
    if (hPal) {
        hPalPrev1 = GpiSelectPalette(hpsMem, hPal);
        n = WinRealizePalette(todPtr->window.handle, hpsMem,
                              &physPalEntChanged);
        if (n > 0) {
            rc = WinUpdateWindow(todPtr->window.handle);
        }
        hPalPrev2 = GpiSelectPalette(hps, hPal);
        n = WinRealizePalette(todPtr->window.handle, hps, &physPalEntChanged);
        if (n > 0) {
            rc = WinUpdateWindow(todPtr->window.handle);
        }
    }

    aPoints[0].x = 0;
    aPoints[0].y = 0;
    aPoints[1].x = width;
    aPoints[1].y = height;
    aPoints[2].x = 0;
    aPoints[2].y = 0;
    
    rc = GpiBitBlt(hpsMem, hps, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE);
    if (hPal != PAL_ERROR) {
        GpiSelectPalette(hps, hPalPrev2);
    }
    GpiSetBitmap(hpsMem, hbmpPrev);
    TkOS2ReleaseDrawablePS(d, hps, &state);
    if (rc == GPI_ERROR) {
        goto cleanup;
    }
    if (todPtr->type == TOD_WINDOW) {
        depth = Tk_Depth((Tk_Window) todPtr->window.winPtr);
    } else {
        depth = todPtr->bitmap.depth;
    }

    size = sizeof(BITMAPINFO2);
    if (depth <= 8) {
        size += sizeof(unsigned short) * (1 << depth);
    }
    bmpInfoPtr = (BITMAPINFO2 *) ckalloc(size);

    bmpInfoPtr->cbFix = sizeof(BITMAPINFOHEADER2);
    bmpInfoPtr->cx = width;
    bmpInfoPtr->cy = height;
    bmpInfoPtr->cPlanes = 1;
    bmpInfoPtr->cBitCount = depth;
    bmpInfoPtr->ulCompression = BCA_UNCOMP;
    bmpInfoPtr->ulColorEncoding = BCE_PALETTE;

    if (depth == 1) {
        unsigned char *p, *pend;
        rc = GpiQueryBitmapInfoHeader(hbmp, (PBITMAPINFOHEADER2) &bmpInfoPtr);
        data = ckalloc(bmpInfoPtr->cbImage);
        if (!data) {
            /* printf("Failed to allocate data area for XImage.\n"); */
            ret_image = NULL;
            goto cleanup;
        }
        ret_image = XCreateImage(display, NULL, depth, ZPixmap, 0, data,
                width, height, 32, ((width + 31) >> 3) & ~1);
        if (ret_image == NULL) {
            ckfree(data);
            goto cleanup;
        }

        /* Get the BITMAP info into the Image. */
        bmpInfoPtr->ulColorEncoding = BCE_PALETTE;
        rc = GpiQueryBitmapBits(hpsMem, 0L, (LONG) height, (PBYTE)data,
                                bmpInfoPtr);
        if (rc != GPI_ALTERROR) {
            ckfree((char *) ret_image->data);
            ckfree((char *) ret_image);
            ret_image = NULL;
            goto cleanup;
        }
        p = data;
        pend = data + bmpInfoPtr->cbImage;
        while (p < pend) {
            *p = ~*p;
            p++;
        }
    } else if (depth == 8) {
        unsigned short *palette;
        unsigned int i;
        unsigned char *p;

        rc = GpiQueryBitmapInfoHeader(hbmp, (PBITMAPINFOHEADER2) bmpInfoPtr);
        data = ckalloc(bmpInfoPtr->cbImage);
        if (!data) {
            /* printf("Failed to allocate data area for XImage.\n"); */
            ret_image = NULL;
            goto cleanup;
        }
        ret_image = XCreateImage(display, NULL, 8, ZPixmap, 0, data,
                width, height, 8, width);
        if (ret_image == NULL) {
            ckfree((char *) data);
            goto cleanup;
        }

        /* Get the BITMAP info into the Image. */
        bmpInfoPtr->ulColorEncoding = BCE_PALETTE;
        rc = GpiQueryBitmapBits(hpsMem, 0L, (LONG) height, (PBYTE)data,
                                bmpInfoPtr);
        if (rc != GPI_ALTERROR) {
            ckfree((char *) ret_image->data);
            ckfree((char *) ret_image);
            ret_image = NULL;
            goto cleanup;
        }
        p = data;
        palette = (unsigned short *) bmpInfoPtr->argbColor;
        for (i = 0; i < bmpInfoPtr->cbImage; i++, p++) {
            *p = (unsigned char) palette[*p];
        }
    } else {
/*
        rc = GpiQueryBitmapInfoHeader(hbmp, (PBITMAPINFOHEADER2) &bmpInfoPtr);
*/
        data = ckalloc(width * height * 4);
        if (!data) {
            /* printf("Failed to allocate data area for XImage.\n"); */
            ret_image = NULL;
            goto cleanup;
        }
        ret_image = XCreateImage(display, NULL, 32, ZPixmap, 0, data,
                width, height, 0, width * 4);
        if (ret_image == NULL) {
            ckfree((char *) data);
            goto cleanup;
        }

        if (depth <= 24) {
            unsigned char *smallBitData, *smallBitBase, *bigBitData;
            unsigned int byte_width, h, w;

            byte_width = ((width * 3 + 3) & ~3);
            smallBitBase = ckalloc(byte_width * height);
            if (!smallBitBase) {
                ckfree((char *) ret_image->data);
                ckfree((char *) ret_image);
                ret_image = NULL;
                goto cleanup;
            }
            smallBitData = smallBitBase;

            /* Get the BITMAP info into the Image. */
            bmpInfoPtr->ulColorEncoding = BCE_RGB;
            rc = GpiQueryBitmapBits(hpsMem, 0L, (LONG) height,
                                    (PBYTE)smallBitData, bmpInfoPtr);
            if (rc != GPI_ALTERROR) {
                ckfree((char *) ret_image->data);
                ckfree((char *) ret_image);
                ckfree((char *) smallBitBase);
                ret_image = NULL;
                goto cleanup;
            }

            /* Copy the 24 Bit Pixmap to a 32-Bit one. */
            for (h = 0; h < height; h++) {
                bigBitData   = ret_image->data + h * ret_image->bytes_per_line;
                smallBitData = smallBitBase + h * byte_width;

                for (w = 0; w < width; w++) {
                    *bigBitData++ = ((*smallBitData++));
                    *bigBitData++ = ((*smallBitData++));
                    *bigBitData++ = ((*smallBitData++));
                    *bigBitData++ = 0;
                }
            }
            /* Free the Device contexts, and the Bitmap */
            ckfree((char *) smallBitBase);
        } else {
            /* Get the BITMAP info directly into the Image. */
            bmpInfoPtr->ulColorEncoding = BCE_RGB;
            rc = GpiQueryBitmapBits(hpsMem, 0L, (LONG) height,
                                    (PBYTE)ret_image->data, bmpInfoPtr);
            if (rc != GPI_ALTERROR) {
                ckfree((char *) ret_image->data);
                ckfree((char *) ret_image);
                ret_image = NULL;
                goto cleanup;
            }
        }
    }

  cleanup:
    if (bmpInfoPtr) {
        ckfree((char *) bmpInfoPtr);
    }
    if (hPal) {
        GpiSelectPalette(hpsMem, hPalPrev1);
    }
    GpiDestroyPS(hpsMem);
    GpiDeleteBitmap(hbmp);

    return ret_image;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *      This function copies data from a pixmap or window into an
 *      XImage.
 *
 * Results:
 *      Returns a newly allocated image containing the data from the
 *      given rectangle of the given drawable.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

XImage *
XGetImage(display, d, x, y, width, height, plane_mask, format)
    Display* display;
    Drawable d;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    unsigned long plane_mask;
    int format;
{
    TkOS2Drawable *todPtr = (TkOS2Drawable *)d;
    XImage *imagePtr;

#ifdef VERBOSE
    printf("XGetImage\n");
#endif

    if (todPtr == NULL) {
        /*
         * Avoid unmapped windows or bad drawables
         */
        return NULL;
    }

    if (todPtr->type != TOD_BITMAP) {
        /*
         * This handles TOD_WINDOW or TOD_WINPS, always creating a 32bit
         * image.  If the window being copied isn't visible (unmapped or
         * obscured), we quietly stop copying (no user error).
         * The user will see black where the widget should be.
         * This branch is likely followed in favor of XGetImageZPixmap as
         * postscript printed widgets require RGB data.
         */
        TkOS2PSState state;
        unsigned int xx, yy, size;
        LONG pixel;
        POINTL pointl;
        HPS hps = TkOS2GetDrawablePS(display, d, &state);

        imagePtr = XCreateImage(display, NULL, 32,
                format, 0, NULL, width, height, 32, 0);
        size = imagePtr->bytes_per_line * imagePtr->height;
        imagePtr->data = ckalloc(size);
        memset((void *)imagePtr->data, 0, size);

        for (yy = 0; yy < height; yy++) {
            /* Reverse Y coordinates */
            pointl.y = height - (y+(int)yy);
            for (xx = 0; xx < width; xx++) {
                pointl.x = x+(int)xx;
                pixel = GpiQueryPel(hps, &pointl);
                if (pixel == CLR_NOINDEX || pixel == GPI_ALTERROR) {
                    break;
                }
                PutPixel(imagePtr, xx, yy, pixel);
            }
        }

        TkOS2ReleaseDrawablePS(d, hps, &state);
    } else if (format == ZPixmap) {
        /*
         * This actually handles most TOD_WINDOW requests, but it varies
         * from the above in that it really does a screen capture of
         * an area, which is consistent with the Unix behavior, but does
         * not appear to handle all bit depths correctly. -- hobbs
         */
        imagePtr = XGetImageZPixmap(display, d, x, y,
                width, height, plane_mask, format);
    } else {
        char *errMsg = NULL;
        BITMAPINFO2 infoBuf;

        if (todPtr->bitmap.handle == NULLHANDLE) {
            errMsg = "XGetImage: not implemented for empty bitmap handles";
        } else if (format != XYPixmap) {
            errMsg = "XGetImage: not implemented for format != XYPixmap";
        } else if (plane_mask != 1) {
            errMsg = "XGetImage: not implemented for plane_mask != 1";
        }
        if (errMsg != NULL) {
            /*
             * Do a soft warning for the unsupported XGetImage types.
             */
            TkpDisplayWarning(errMsg, "XGetImage Failure");
            return NULL;
        }

        imagePtr = XCreateImage(display, NULL, 1, XYBitmap, 0, NULL,
                                width, height, 32, 0);
        imagePtr->data = ckalloc(imagePtr->bytes_per_line * imagePtr->height);

        infoBuf.cbFix = 20;
        infoBuf.cx = width;
        infoBuf.cy = height;
        infoBuf.cPlanes = 1;
        infoBuf.cBitCount = 1;
        infoBuf.ulCompression = BCA_UNCOMP;

        rc = GpiQueryBitmapBits(todPtr->bitmap.hps, 0L, (LONG)height,
                                (PBYTE)imagePtr->data, &infoBuf);
    }

    return imagePtr;
}

/* 
 * tkOS2ImgUtil.c --
 *
 *	This file contains image related utility functions.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkImgUtil.c,v 1.2 1998/09/14 18:23:13 stanton Exp $
 */

#include "tkInt.h"
#include "tkPort.h"
#include "xbytes.h"


/*
 *----------------------------------------------------------------------
 *
 * TkAlignImageData --
 *
 *	This function takes an image and copies the data into an
 *	aligned buffer, performing any necessary bit swapping.
 *	We need to reverse the lines in OS/2 because of the inverted Y
 *	coordinate system.
 *
 * Results:
 *	Returns a newly allocated buffer that should be freed by the
 *	caller.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
TkAlignImageData(image, alignment, bitOrder)
    XImage *image;		/* Image to be aligned. */
    int alignment;		/* Number of bytes to which the data should
				 * be aligned (e.g. 2 or 4) */
    int bitOrder;		/* Desired bit order: LSBFirst or MSBFirst. */
{
    long dataWidth;
    char *data, *srcPtr, *destPtr;
    int i, j;

#ifdef VERBOSE
    printf("TkAlignImageData, image->bitmap_bit_order %d, bitOrder %d\n",
           image->bitmap_bit_order, bitOrder);
#endif

    if (image->bits_per_pixel != 1) {
	panic("TkAlignImageData: Can't handle image depths greater than 1.");
    }

    /*
     * Compute line width for output data buffer.
     */

    dataWidth = image->bytes_per_line;
    if (dataWidth % alignment) {
	dataWidth += (alignment - (dataWidth % alignment));
    }

    data = ckalloc(dataWidth * image->height);

    destPtr = data;
    /* Reverse rows in Y direction */
    for (i = image->height - 1; i >= 0; i--) {
	srcPtr = &image->data[i * image->bytes_per_line];
	for (j = 0; j < dataWidth; j++) {
	    if (j >= image->bytes_per_line) {
		*destPtr = 0;
	    } else if (image->bitmap_bit_order != bitOrder) {
		*destPtr = xBitReverseTable[(unsigned char)(*(srcPtr++))];
	    } else {
		*destPtr = *(srcPtr++);
	    }
	    destPtr++;
	}
    }
    return data;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ReverseImageLines --
 *
 *	This function takes an image and copies the data into an
 *	aligned buffer, reversing the line order.
 *	We need to reverse the lines in OS/2 because of the inverted Y
 *	coordinate system.
 *
 * Results:
 *	Returns a newly allocated buffer that should be freed by the
 *	caller.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
TkOS2ReverseImageLines(image, height)
    XImage *image;		/* Image to be reversed. */
    int height;		/* Height of image, image->height may not be correct */
{
    char *data, *srcPtr, *destPtr;
    int i, j;

#ifdef VERBOSE
    printf("TkOS2ReverseImageLines image %x height %d\n", image, height);
#endif
    data = ckalloc(image->bytes_per_line * height);

    destPtr = data;
    /* Reverse rows */
    for (i = height - 1; i >= 0; i--) {
	srcPtr = &image->data[i * image->bytes_per_line];
	for (j = 0; j < image->bytes_per_line; j++) {
            *destPtr = *(srcPtr++);
            destPtr++;
	}
    }
    return data;
}

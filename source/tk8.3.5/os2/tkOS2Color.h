/*
 * tkOS2Color.h --
 *
 *	Declarations of thread specific data of tkOS2Color. Needed in
 *      tkOS2Menu.c too.
 *
 * Copyright (c) 2002-2003 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _TKOS2COLOR
#define _TKOS2COLOR

typedef struct ThreadSpecificData {
    int initialized;                    /* Set to 0 at allocation */
    int ncolors;                        /* Nr. of colors that have been set */
    LONG *logColorTable;                /* Table of colors that have been set */
    Tcl_HashTable logColorRefCounts;    /* Hash table for reference counts of
                                         * logical color table entries */
    LONG nextColor;                     /* Next free index in color table */
} ThreadSpecificData;
extern Tcl_ThreadDataKey tkOS2ColorDataKey;

#endif /* _TKOS2COLOR */

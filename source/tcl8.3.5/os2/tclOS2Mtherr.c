/* 
 * tclOS2Mtherr.c --
 *
 *	This function provides a default implementation of the
 *	_matherr function for Borland C++.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tclOS2Int.h"
#include <math.h>


/*
 *----------------------------------------------------------------------
 *
 * _matherr --
 *
 *	This procedure is invoked by Borland C++ when certain
 *	errors occur in mathematical functions.  This procedure
 *	replaces the default implementation which generates pop-up
 *	warnings.
 *
 * Results:
 *	Returns 1 to indicate that we've handled the error
 *	locally.
 *
 * Side effects:
 *	Sets errno based on what's in xPtr.
 *
 *----------------------------------------------------------------------
 */

int
_matherr(xPtr)
    struct _exception *xPtr;	/* Describes error that occurred. */
{
    if (!TclMathInProgress()) {
	return 0;
    }
    errno = ERANGE;
    return 1;
}

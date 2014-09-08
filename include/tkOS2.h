/*
 * tkOS2.h --
 *
 *	Declarations of public types and interfaces that are only
 *	available under OS/2.
 *
 * Copyright (c) 1996-1997 by Sun Microsystems, Inc.
 * Copyright (c) 1999-2003 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _TKOS2
#define _TKOS2

#ifndef _TK
#include <tk.h>
#endif

#define INCL_BASE
#define INCL_PM
#include <os2.h>
#undef INCL_PM
#undef INCL_BASE

#ifndef OS2
#define OS2
#endif

#ifdef BUILD_tk
# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS EXPENTRY
#endif

/*
 * The following messages are use to communicate between a Tk toplevel
 * and its container window.
 */

#define TK_CLAIMFOCUS	(WM_USER)
#define TK_GEOMETRYREQ	(WM_USER+1)
#define TK_ATTACHWINDOW	(WM_USER+2)
#define TK_DETACHWINDOW	(WM_USER+3)


/*
 *--------------------------------------------------------------
 *
 * Exported procedures defined for the OS/2 platform only.
 *
 *--------------------------------------------------------------
 */

#ifdef USE_TCL_STUBS
#include "tclPlatDecls.h"
#endif
#include "tkPlatDecls.h"

# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS DLLIMPORT

#endif /* _TKOS2 */

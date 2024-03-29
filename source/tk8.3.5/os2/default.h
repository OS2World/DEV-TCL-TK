/*
 * default.h --
 *
 *	This file defines the defaults for all options for all of
 *	the Tk widgets.
 *
 * Copyright (c) 1996-2000 Illya Vaes
 * Copyright (c) 1994 Sun Microsystems, Inc.
 * Copyright (c) 1991-1994 The Regents of the University of California.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * * RCS: @(#) $Id: default.h,v 1.2 1998/09/14 18:23:02 stanton Exp $
 */

#ifndef _DEFAULT
#define _DEFAULT

#if defined(__WIN32__) || defined(_WIN32)
#   include "tkWinDefault.h"
#else
#   if defined(MAC_TCL)
#	include "tkMacDefault.h"
#   else
#      if defined(__OS2__)
#         include "tkOS2Default.h"
#      else
#         include "tkUnixDefault.h"
#      endif
#   endif
#endif

#endif /* _DEFAULT */

/*
 * tclOS2Console.h --
 *
 *	Declarations of console class functions.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _TCLOS2CONSOLE
#define _TCLOS2CONSOLE

#include "tclPort.h"
#include "tcl.h"

#ifndef CLI_VERSION
BOOL RegisterTerminalClass _ANSI_ARGS_((HAB hab));
HWND CreateTerminal _ANSI_ARGS_((HAB hab, Tcl_Interp *interp));
#define FONTVAR	TCLSHPMFONT
#endif

#endif /* _TCLOS2CONSOLE */

/*
 * tclOS2Int.h --
 *
 *	Declarations of OS2-specific shared variables and procedures.
 *
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 * Copyright (c) 1998 Sun Microsystems, Inc.
 * Copyright (c) 1998-2001 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _TCLOS2INT
#define _TCLOS2INT

#ifndef _TCLINT
#include "tclInt.h"
#endif
#ifndef _TCLPORT
#include "tclPort.h"
#endif

/*
 * The following specifies how much stack space TclpCheckStackSpace()
 * ensures is available.  TclpCheckStackSpace() is called by Tcl_EvalObj()
 * to help avoid overflowing the stack in the case of infinite recursion.
 */

#define TCL_OS2_STACK_THRESHOLD 0x2000

#ifdef BUILD_tcl
# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS EXPENTRY
#endif

#define FS_CASE_SENSITIVE    1
#define FS_CASE_IS_PRESERVED 2

#define NEW_HANDLE	0xFFFFFFFF	/* DosDuphandle: return new handle */
#define HF_STDIN	0	/* Standard input handle */
#define HF_STDOUT	1	/* Standard output handle */
#define HF_STDERR	2	/* Standard error handle */

extern HAB tclHab;	/* Anchor block */
extern LONG rc;	/* Variable for checking return codes */
extern ULONG maxPath;	/* Maximum path length */
extern BOOL usePm;	/* Make use of PM calls from Tcl? */
extern ULONG sysInfo[QSV_MAX];	/* Information about OS/2 and the system */
#ifdef VERBOSE
extern int openedFiles;	/* How many files opened (DosOpen/DosDuphandle) */
#endif

/*
 * Declarations of functions that are not accessible by way
 * stubs table.
 */

EXTERN void             TclOS2Init(HMODULE hModule);
/*
 * Use PM events? TRUE if PM version; set to FALSE by tclOS2Main if that is
 * compiled with CLI_VERSION defined.
 * defined in stubs:
EXTERN HAB TclOS2GetHAB (void);
EXTERN HMQ TclOS2GetHMQ (HAB hab);
EXTERN BOOL TclOS2GetUsePm (void);
EXTERN void TclOS2SetUsePm (BOOL value);
 */
/*
 * Only necessary for PM Tcl-shell, but usage is switched at runtime via
 * the usePm global variable, so the compiler complains when compiling for
 * CLI version that it's not known when #ifdef-ed (in tclOS2Console.h).
 */
BOOL RegisterTerminalClass (HAB hab);
HWND CreateTerminal (HAB hab, Tcl_Interp *interp);

#include "tclIntPlatDecls.h"

# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS DLLIMPORT

#endif	/* _TCLOS2INT */

/* 
 * os2Main.c --
 *
 *	Main entry point for wish and other Tk-based applications.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 by Scriptics Corporation.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tk.h>
#define INCL_PM
#define INCL_DOSPROCESS
#include <os2.h>
#undef INCL_DOSPROCESS
#undef INCL_PM
#include <malloc.h>
#include <locale.h>
#include <stdarg.h>

#include "tkInt.h"

/*
 * Forward declarations for procedures defined later in this file:
 */

static void WishPanic _ANSI_ARGS_(TCL_VARARGS(char *,format));

#ifdef TK_TEST
EXTERN int              Tktest_Init _ANSI_ARGS_((Tcl_Interp *interp));
#endif /* TK_TEST */

#ifdef TCL_TEST
extern int              TclObjTest_Init _ANSI_ARGS_((Tcl_Interp *interp));
extern int              Tcltest_Init _ANSI_ARGS_((Tcl_Interp *interp));
#endif /* TCL_TEST */

static BOOL consoleRequired = TRUE;

/*
 * The following #if block allows you to change the AppInit
 * function by using a #define of TCL_LOCAL_APPINIT instead
 * of rewriting this entire file.  The #if checks for that
 * #define and uses Tcl_AppInit if it doesn't exist.
 */

#ifndef TK_LOCAL_APPINIT
#define TK_LOCAL_APPINIT Tcl_AppInit
#endif
extern int TK_LOCAL_APPINIT _ANSI_ARGS_((Tcl_Interp *interp));

/*
 * The following #if block allows you to change how Tcl finds the startup
 * script, prime the library or encoding paths, fiddle with the argv,
 * etc., without needing to rewrite Tk_Main()
 */

#ifdef TK_LOCAL_MAIN_HOOK
extern int TK_LOCAL_MAIN_HOOK _ANSI_ARGS_((int *argc, char ***argv));
#endif

/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	Main entry point from OS/2.
 *
 * Results:
 *	Returns false if initialization fails, true otherwise.
 *
 * Side effects:
 *	Just about anything, since from here we call arbitrary Tcl code.
 *
 *----------------------------------------------------------------------
 */

int
main( int argc, char **argv )
{
    APIRET rc;
    char *p;
    PPIB pibPtr;
    PTIB tibPtr;
    char moduleName[MAX_PATH+1];

    /*
     * Initialize PM: done in DLL when that was compiled without stubs
     * and in TK_LOCAL_MAIN_HOOK when compiled with stubs.
     */

    Tcl_SetPanicProc(WishPanic);

    /*
     * Create the console channels and install them as the standard
     * channels.  All I/O will be discarded until Tk_CreateConsoleWindow is
     * called to attach the console to a text widget.
     */

    consoleRequired = TRUE;

    /*
     * Set up the default locale to be standard "C" locale so parsing
     * is performed correctly.
     */

    setlocale(LC_ALL, "C");

    /*
     * Replace argv[0] with full pathname of executable, and forward
     * slashes substituted for backslashes.
     */

    rc = DosGetInfoBlocks(&tibPtr, &pibPtr);
#ifdef VERBOSE
    printf("pibPtr->pib_pchcmd [%s]\n", pibPtr->pib_pchcmd);
#endif

    rc = DosQueryModuleName((HMODULE)pibPtr->pib_hmte, 256, moduleName);
#ifdef VERBOSE
    printf("module name [%s]\n", moduleName);
#endif
    argv[0] = moduleName;
    for (p = moduleName; *p != '\0'; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }

#ifdef TK_LOCAL_MAIN_HOOK
    TK_LOCAL_MAIN_HOOK(&argc, &argv);
#endif

    Tk_Main(argc, argv, TK_LOCAL_APPINIT);

    /* Shutting down PM: done in DLL */

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_AppInit --
 *
 *	This procedure performs application-specific initialization.
 *	Most applications, especially those that incorporate additional
 *	packages, will have their own version of this procedure.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error
 *	message in the interp's result if an error occurs.
 *
 * Side effects:
 *	Depends on the startup script.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_AppInit(interp)
    Tcl_Interp *interp;		/* Interpreter for application. */
{
#ifdef INIT_PM_IN_APPINIT
    /* Initialize Presentation Manager etc. */
    TkOS2XInit();
#endif
#ifdef VERBOSE
    printf("before Tcl_Init, interp %x\n", interp);
    fflush(stdout);
#endif
    if (Tcl_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
#ifdef VERBOSE
    printf("after Tcl_Init, interp %x\n", interp);
    printf("before Tk_Init, interp %x\n", interp);
    fflush(stdout);
#endif
    if (Tk_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
#ifdef VERBOSE
    printf("after Tk_Init, interp %x\n", interp);
    fflush(stdout);
#endif

    Tcl_StaticPackage(interp, "Tk", Tk_Init, (Tcl_PackageInitProc *) NULL);

    /*
     * Initialize the console only if we are running as an interactive
     * application.
     */

    if (consoleRequired) {
#ifdef VERBOSE
        printf("before Tk_CreateConsoleWindow, interp %x\n", interp);
        fflush(stdout);
#endif
        if (Tk_CreateConsoleWindow(interp) == TCL_ERROR) {
            goto error;
        }
#ifdef VERBOSE
        printf("after Tk_CreateConsoleWindow, interp %x\n", interp);
        fflush(stdout);
#endif
    }

#ifdef TCL_TEST
    if (Tcltest_Init(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Tcltest", Tcltest_Init,
            (Tcl_PackageInitProc *) NULL);
    if (TclObjTest_Init(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }
#endif /* TCL_TEST */

#ifdef TK_TEST
    if (Tktest_Init(interp) == TCL_ERROR) {
	goto error;
    }
    Tcl_StaticPackage(interp, "Tktest", Tktest_Init,
            (Tcl_PackageInitProc *) NULL);
#endif /* TK_TEST */

    Tcl_SetVar(interp, "tcl_rcFileName", "~/wishrc.tcl", TCL_GLOBAL_ONLY);
    return TCL_OK;

error:
    /* Make sure pointer is not captured (for WinMessageBox) */
    WinSetCapture(HWND_DESKTOP, NULLHANDLE);
    WinAlarm(HWND_DESKTOP, WA_ERROR);
    WinMessageBox(HWND_DESKTOP, NULLHANDLE, Tcl_GetStringResult(interp),
                  "Error in WISH", 0, MB_OK | MB_ERROR | MB_APPLMODAL);
    exit(1);
    /* we won't reach this, but we need the return */
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * WishPanic --
 *
 *      Display a message and exit.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Exits the program.
 *
 *----------------------------------------------------------------------
 */

void
WishPanic TCL_VARARGS_DEF(char *,arg1)
{
    va_list argList;
    char buf[1024];
    char *format;

    format = TCL_VARARGS_START(char *,arg1,argList);
    vsprintf(buf, format, argList);

    /* Make sure pointer is not captured (for WinMessageBox) */
    WinSetCapture(HWND_DESKTOP, NULLHANDLE);
    WinAlarm(HWND_DESKTOP, WA_ERROR);
    WinMessageBox(HWND_DESKTOP, NULLHANDLE, buf, "Fatal Error in WISH", 0,
            MB_OK | MB_ERROR | MB_APPLMODAL);
    exit(1);
}

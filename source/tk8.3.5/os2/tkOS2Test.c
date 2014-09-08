/* 
 * tkTest.c --
 *
 *      Contains commands for platform specific tests for
 *      the OS/2 platform.
 *
 * Copyright (c) 1997 Sun Microsystems, Inc.
 * Copyright (c) 2000 by Scriptics Corporation.
 * Copyright (c) 1999-2003 Illya Vaes.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkTest.c,v 1.4 1999/02/04 20:57:17 stanton Exp $
 */

#include "tkOS2Int.h"

/*
 * Forward declarations for procedures defined later in this file:
 */

int                     TkplatformtestInit(Tcl_Interp *interp);
static int              TestclipboardObjCmd(ClientData clientData,
                            Tcl_Interp *interp, int objc,
                            Tcl_Obj *CONST objv[]);

/*
 *----------------------------------------------------------------------
 *
 * TkplatformtestInit --
 *
 *      Defines commands that test platform specific functionality for
 *      the OS/2 platform.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error
 *	message in the interp's result if an error occurs.
 *
 * Side effects:
 *	Defines new commands.
 *
 *----------------------------------------------------------------------
 */

int
TkplatformtestInit(
    Tcl_Interp *interp)         /* Interpreter to add commands to. */
{
    /*
     * Add commands for platform specific tests on MacOS here.
     */

    Tcl_CreateObjCommand(interp, "testclipboard", TestclipboardObjCmd,
            (ClientData) Tk_MainWindow(interp), (Tcl_CmdDeleteProc *) NULL);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestclipboardObjCmd --
 *
 *	This procedure implements the testclipboard command. It provides
 *	a way to determine the actual contents of the OS/2  clipboard.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestclipboardObjCmd(clientData, interp, objc, objv)
    ClientData clientData;		/* Main window for application. */
    Tcl_Interp *interp;			/* Current interpreter. */
    int objc;                           /* Number of arguments. */
    Tcl_Obj *CONST objv[];              /* Argument values. */
{
    TkWindow *winPtr = (TkWindow *) clientData;
    char *data;
    int code = TCL_OK;
    HAB hab = TclOS2GetHAB();

    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, (char *) NULL);
        return TCL_ERROR;
    }
    if (WinOpenClipbrd(hab)) {
        if ((data= (char *)WinQueryClipbrdData(hab, CF_TEXT))) {
            Tcl_AppendResult(interp, data, (char *) NULL);
        } else {
            Tcl_AppendResult(interp, "couldn't query clipboard", (char *) NULL);
            code = TCL_ERROR;
        }
        WinCloseClipbrd(hab);
        return code;
    } else {
        Tcl_AppendResult(interp, "couldn't open clipboard", (char *) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

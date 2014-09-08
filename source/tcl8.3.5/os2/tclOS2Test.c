/* 
 * tclOS2Test.c --
 *
 *	Contains commands for platform specific tests on OS/2.
 *
 * Copyright (c) 1996 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tclOS2Int.h"

/*
 * Forward declarations of procedures defined later in this file:
 */
int			TclplatformtestInit _ANSI_ARGS_((Tcl_Interp *interp));
static int              TesteventloopCmd _ANSI_ARGS_((ClientData dummy,
                            Tcl_Interp *interp, int argc, char **argv));
static int              TestvolumetypeCmd _ANSI_ARGS_((ClientData dummy,
                            Tcl_Interp *interp, int objc,
                            Tcl_Obj *CONST objv[]));

/*
 *----------------------------------------------------------------------
 *
 * TclplatformtestInit --
 *
 *	Defines commands that test platform specific functionality for
 *	OS/2 platforms.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Defines new commands.
 *
 *----------------------------------------------------------------------
 */

int
TclplatformtestInit(interp)
    Tcl_Interp *interp;		/* Interpreter to add commands to. */
{
    /*
     * Add commands for platform specific tests for OS/2 here.
     */

    Tcl_CreateCommand(interp, "testeventloop", TesteventloopCmd,
            (ClientData) 0, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateObjCommand(interp, "testvolumetype", TestvolumetypeCmd,
            (ClientData) 0, (Tcl_CmdDeleteProc *) NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TesteventloopCmd --
 *
 *      This procedure implements the "testeventloop" command. It is
 *      used to test the Tcl notifier from an "external" event loop
 *      (i.e. not Tcl_DoOneEvent()).
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
TesteventloopCmd(clientData, interp, argc, argv)
    ClientData clientData;              /* Not used. */
    Tcl_Interp *interp;                 /* Current interpreter. */
    int argc;                           /* Number of arguments. */
    char **argv;                        /* Argument strings. */
{
    static int *framePtr = NULL; /* Pointer to integer on stack frame of
                                  * innermost invocation of the "wait"
                                  * subcommand. */

   if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # arguments: should be \"", argv[0],
                " option ... \"", (char *) NULL);
        return TCL_ERROR;
    }
    if (strcmp(argv[1], "done") == 0) {
        *framePtr = 1;
    } else if (strcmp(argv[1], "wait") == 0) {
        int *oldFramePtr;
        int done;
        QMSG msg;
        int oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);

        /*
         * Save the old stack frame pointer and set up the current frame.
         */

        oldFramePtr = framePtr;
        framePtr = &done;

        /*
         * Enter a standard OS/2 PM event loop until the flag changes.
         * Note that we do not explicitly call Tcl_ServiceEvent().
         */

        done = 0;
        while (!done) {
            if (!WinGetMsg(TclOS2GetHAB(), &msg, NULLHANDLE, 0, 0)) {
                /*
                 * The application is exiting, so repost the quit message
                 * and start unwinding.
                 */

                break;
            }
            WinDispatchMsg(TclOS2GetHAB(), &msg);
        }
        (void) Tcl_SetServiceMode(oldMode);
        framePtr = oldFramePtr;
    } else {
        Tcl_AppendResult(interp, "bad option \"", argv[1],
                "\": must be done or wait", (char *) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Testvolumetype --
 *
 *      This procedure implements the "testvolumetype" command. It is
 *      used to check the volume type (FAT, HPFS) of a volume.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
TestvolumetypeCmd(clientData, interp, objc, objv)
    ClientData clientData;              /* Not used. */
    Tcl_Interp *interp;                 /* Current interpreter. */
    int objc;                           /* Number of arguments. */
    Tcl_Obj *CONST objv[];              /* Argument objects. */
{
    APIRET ret;
    char *path;
    BYTE fsBuf[sizeof(FSQBUFFER2) + (3*CCHMAXPATH)] = {0};
    ULONG bufSize = sizeof(fsBuf);

    if (objc > 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?name?");
        return TCL_ERROR;
    }
    if (objc == 2) {
        /*
         * path has to be really a proper volume
         */
        path = Tcl_GetString(objv[1]);
    } else {
        path = NULL;
    }
    ret = DosQueryFSAttach(path, 0L, FSAIL_DRVNUMBER, ((PFSQBUFFER2)fsBuf),
                           &bufSize);
    if (ret != NO_ERROR) {
        Tcl_AppendResult(interp, "could not get volume type for \"",
                (path?path:""), "\"", (char *) NULL);
        TclOS2ConvertError(ret);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, ((PFSQBUFFER2) fsBuf)->szFSDName, TCL_VOLATILE);
    return TCL_OK;
}

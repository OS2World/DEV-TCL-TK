/*
 * tkOS2Dialog.c --
 *
 *	Contains the OS/2 implementation of the common dialog boxes.
 *
 * Copyright (c) 1996-1997 Sun Microsystems, Inc.
 * Copyright (c) 1999-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */
 
#include "tkOS2Int.h"
#include "TkResIds.h"
#include "tkFileFilter.h"

/*
 * Undocumented color dialog messages
 */
#define WM_CHOSENCOLOR_WARP4	0x0601
#define WM_CHOSENCOLOR_EARLIER	0x130C
#define WM_SETCOLOR_WARP4	0x0602
#define WM_SETCOLOR_EARLIER	0x1384

/*
 * Global variables
 */
static PFN colorSelectWndProcPtr = NULL;
static PFNWP oldDlgProc = NULL;
static ULONG chosenColor = 0;

typedef struct ThreadSpecificData {
    int debugFlag;            /* Flags whether we should output debugging
                               * information while displaying a builtin
                               * dialog. */
    Tcl_Interp *debugInterp;  /* Interpreter to used for debugging. */
    ULONG WM_LBSELCHANGED;    /* Holds a registered windows event used for
                               * communicating between the Directory
                               * Chooser dialog and its hook proc. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * The following structures are used by Tk_MessageBoxCmd() to parse
 * arguments and return results.
 */

static const TkStateMap iconMap[] = {
    {MB_ERROR,                  "error"},
    {MB_INFORMATION,            "info"},
    {MB_ICONQUESTION,           "question"},
    {MB_WARNING,                "warning"},
    {-1,                        NULL}
};

static const TkStateMap typeMap[] = {
    {MB_ABORTRETRYIGNORE,       "abortretryignore"},
    {MB_OK,                     "ok"},
    {MB_OKCANCEL,               "okcancel"},
    {MB_RETRYCANCEL,            "retrycancel"},
    {MB_YESNO,                  "yesno"},
    {MB_YESNOCANCEL,            "yesnocancel"},
    {-1,                        NULL}
};

static const TkStateMap buttonMap[] = {
    {MBID_ABORT,               "abort"},
    {MBID_RETRY,               "retry"},
    {MBID_IGNORE,              "ignore"},
    {MBID_OK,                  "ok"},
    {MBID_CANCEL,              "cancel"},
    {MBID_NO,                  "no"},
    {MBID_YES,                 "yes"},
    {-1,                        NULL}
};

static const int buttonFlagMap[] = {
    MB_DEFBUTTON1, MB_DEFBUTTON2, MB_DEFBUTTON3
};

static const struct {int type; int btnIds[3];} allowedTypes[] = {
    {MB_ABORTRETRYIGNORE,       {MBID_ABORT, MBID_RETRY,  MBID_IGNORE}},
    {MB_OK,                     {MBID_OK,    -1,          -1         }},
    {MB_OKCANCEL,               {MBID_OK,    MBID_CANCEL, -1         }},
    {MB_RETRYCANCEL,            {MBID_RETRY, MBID_CANCEL, -1         }},
    {MB_YESNO,                  {MBID_YES,   MBID_NO,     -1         }},
    {MB_YESNOCANCEL,            {MBID_YES,   MBID_NO,     MBID_CANCEL}}
};

#define NUM_TYPES (sizeof(allowedTypes) / sizeof(allowedTypes[0]))

/*
 * The value of TK_MULTI_MAX_PATH dictactes how many files can
 * be retrieved with tk_get*File -multiple 1.  It must be allocated
 * on the stack, so make it large enough but not too large.  -- hobbs
 * The data is stored as <dir>\0<file1>\0<file2>\0...<fileN>\0\0.
 * MAX_PATH == 260 on Win2K/NT and OS/2.
 * 8.3 doesn't have -multiple, so this == MAX_PATH.
 */

#define TK_MULTI_MAX_PATH       (MAX_PATH)

static int              EvalCmdWithObjv(Tcl_Interp *interp, char * cmdName,
                            int objc, Tcl_Obj *CONST objv[]);
static MRESULT EXPENTRY ColorDlgProc _ANSI_ARGS_((HWND hwnd, ULONG message,
                            MPARAM param1, MPARAM param2));
static int 		GetFileName _ANSI_ARGS_((ClientData clientData,
    			    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[],
    			    int isOpen));
static int              MakeFilter(Tcl_Interp *interp, char *string,
                            Tcl_DString *dsPtr, FILEDLG *fileDlgPtr);
static void             SetTkDialog(ClientData clientData);


/*
 *----------------------------------------------------------------------
 *
 * EvalCmdWithObjv --
 *
 *      Evaluates the Tcl procedure with the arguments. objv[0] is set by
 *      the caller of this function. It may be different than cmdName.
 *      The TCL command will see objv[0], not cmdName, as its name if it
 *      invokes [lindex [info level 0] 0]
 *
 * Results:
 *      TCL_ERROR if the command does not exist and cannot be autoloaded.
 *      Otherwise, return the result of the evaluation of the command.
 *
 * Side effects:
 *      The command may be autoloaded.
 *
 *----------------------------------------------------------------------
 */

static int
EvalCmdWithObjv(interp, cmdName, objc, objv)
    Tcl_Interp *interp;         /* Current interpreter. */
    char * cmdName;             /* Name of the TCL command to call */
    int objc;                   /* Number of arguments. */
    Tcl_Obj *CONST objv[];      /* Argument objects. */
{
    int i, code;
    Tcl_Obj **newObjv = (Tcl_Obj **) ckalloc((unsigned) objc *
                                             sizeof (Tcl_Obj *));

    for (i = objc-1; i > 0; i--) {
        newObjv[i] = objv[i];
    }
    newObjv[0] = Tcl_NewStringObj(cmdName, -1);
    Tcl_IncrRefCount(newObjv[0]);
    code = Tcl_EvalObjv(interp, objc, newObjv, 0);
    Tcl_DecrRefCount(newObjv[0]);
    ckfree((char *) newObjv);

    return code;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_ChooseColorObjCmd --
 *
 *	This procedure implements the color dialog box for the OS/2
 *	platform. See the user documentation for details on what it
 *	does.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	A dialog window is created the first time this procedure is called.
 *	This window is not destroyed and will be reused the next time the
 *	application invokes the "tk_chooseColor" command.
 *
 *----------------------------------------------------------------------
 */

int
Tk_ChooseColorObjCmd(clientData, interp, objc, objv)
    ClientData clientData;	/* Main window associated with interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    /*
     * From Rick Papo's "Undocumented Features of OS/2" (INF file):
     * The color wheel control used by the Solid and Mixed Color Palette
     * object is a publicly registered window class within OS/2, but is
     * undocumented.  The following notes are all that is necessary to
     * use this control class:
     * (1) You must load the module WPCONFIG.DLL so that the publicly
     *     registered window message processor (colorSelectWndProc) can
     *     be used without an addressing violation.
     * (2) Create your control window with WinCreateWindow or through a
     *     dialog template, using the window class name "ColorSelectClass".
     * (3) If you used WinCreateWindow you will need to reposition the
     *     control window each time the parent window is resized, as
     *     otherwise the control will reposition itself out of view.
     *     Dialogs seem to handle this automatically.
     * (4) The only control message defined -to- the control is 0x0602
     *     under OS/2 Warp 4 or later, or (by some reports) 0x1384 on
     *     older versiosn of OS/2. Message parameter one must contain the
     *     RGB value to which the color wheel will be set.
     * (5) The only control message defined -from- the control is 0x0601
     *     under OS/2 Warp 4 or later, or (by some reports) 0x130C on
     *     older version of OS/2. Message parameter one will contain the
     *     RGB value to which the color wheel will be set.
     * (6) The control can only be sized at creation, and should be sized
     *     so that its height is approximately 60% of its width.
     */
    Tk_Window tkwin, parent;
    HWND hwndOwner;
    int i, oldMode, winCode, result;
    char * colorStr = NULL;
    char * title = NULL;
    static int inited = 0;
    static long oldColor;               /* the color selected last time */
    static char *optionStrings[] = {
        "-initialcolor", "-parent", "-title", NULL
    };
    enum options {
        COLOR_INITIAL, COLOR_PARENT, COLOR_TITLE
    };
    ULONG ulReply;
    ULONG startColor = 0L;
    HMODULE wpConfigHandle;
    UCHAR loadError[256];       /* Area for name of DLL that we failed on */
    static BOOL useOS2Dlg = TRUE;
    static HWND hwndDlg = NULLHANDLE, hwndWheel = NULLHANDLE;
    static ULONG info[QSV_MAX]= {0};   /* System Information Data Buffer */

#ifdef VERBOSE
    printf("Tk_ChooseColorCmd\n");
    fflush(stdout);
#endif

    result = TCL_OK;
    if (inited == 0) {
        oldColor = RGB(0xa0, 0xa0, 0xa0);

        /* Load DLL to get access to it */
        if (DosLoadModule((PSZ)loadError, sizeof(loadError), "WPCONFIG.DLL",
                          &wpConfigHandle) != NO_ERROR) {
#ifdef VERBOSE
            printf("DosLoadModule WPCONFIG.DLL ERROR on %s\n", loadError);
            fflush(stdout);
#endif
            useOS2Dlg = FALSE;
            goto handlefallback;
        }
#ifdef VERBOSE
        printf("DosLoadModule WPCONFIG.DLL returned %x\n", wpConfigHandle);
        fflush(stdout);
#endif
 
        /* Get address of color selection window procedure */
        rc = DosQueryProcAddr(wpConfigHandle, 0, "ColorSelectWndProc",
                              &colorSelectWndProcPtr);
        if (rc != NO_ERROR) {
#ifdef VERBOSE
            printf("DosQueryProcAddr %x ERROR %d\n", wpConfigHandle, rc);
            fflush(stdout);
#endif
            useOS2Dlg = FALSE;
            goto handlefallback;
        }
#ifdef VERBOSE
        printf("DosQueryProcAddr %x returned %x\n", wpConfigHandle,
               colorSelectWndProcPtr);
        printf("calling WinLoadDlg(H_D %x hOwn %x CDP %x hMod %x ID %x\n",
               HWND_DESKTOP, hwndOwner, ColorDlgProc, Tk_GetHMODULE(),
               ID_COLORDLGTEMPLATE);
        fflush(stdout);
#endif
        /* Load the dialog around the color wheel from our Tk DLL */
        hwndDlg = WinLoadDlg(HWND_DESKTOP, hwndOwner, WinDefDlgProc,
                             Tk_GetHMODULE(), ID_COLORDLGTEMPLATE, NULL);
        if (hwndDlg == NULLHANDLE) {
            useOS2Dlg = FALSE;
            goto handlefallback;
        }
#ifdef VERBOSE
        printf("WinLoadDlg hOwn %x hMod %x returned %x\n", hwndOwner,
               Tk_GetHMODULE(), hwndDlg);
        fflush(stdout);
#endif
        /* Subclass to get our own procedure in */
        hwndWheel = WinWindowFromID(hwndDlg, ID_COLORWHEEL);
        if (hwndWheel == NULLHANDLE) {
            useOS2Dlg = FALSE;
            goto handlefallback;
#ifdef VERBOSE
            printf("WinWindowFromID ID_COLORWHEEL (%x) ERROR %x\n",
                   ID_COLORWHEEL, WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
        } else {
            printf("WinWindowFromID ID_COLORWHEEL (%x) OK: %x\n", ID_COLORWHEEL,
                   hwndWheel);
            fflush(stdout);
#endif
        }
        oldDlgProc = WinSubclassWindow(hwndDlg, ColorDlgProc);
        if (oldDlgProc == NULL) {
            useOS2Dlg = FALSE;
            goto handlefallback;
#ifdef VERBOSE
            printf("WinSubclassWindow %x ERROR %x\n", hwndDlg,
                   WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
        } else {
            printf("WinSubclassWindow %x OK\n", hwndDlg);
            fflush(stdout);
#endif
        }

        rc= DosQuerySysInfo (1L, QSV_MAX, (PVOID)info, sizeof(info));

        inited = 1;
    } else {
        /*
         * If we use the native color dialog and don't have to initialise,
         * we have to reset the 'dismissed' dialog flag FF_DLGDISMISSED
         */
        if (useOS2Dlg) {
            USHORT flags = WinQueryWindowUShort(hwndDlg, QWS_FLAGS);
            rc = WinSetWindowUShort(hwndDlg, QWS_FLAGS,
                                    flags & ~FF_DLGDISMISSED);
#ifdef VERBOSE
            if (rc != TRUE) {
                printf("WinSetWindowUShort FF_DLGDISMISSED ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("WinSetWindowUShort FF_DLGDISMISSED OK\n");
            }
            fflush(stdout);
#endif
            /* pre-"choose" the previous color */
            chosenColor = oldColor;
        }
    }
handlefallback:

    /* If no init necessary, go to Tcl code if we don't use the Dlg code */
    if (!useOS2Dlg) {
        return EvalCmdWithObjv(interp, "tkColorDialog", objc, objv);
    }

    tkwin = (Tk_Window) clientData;
    parent = tkwin;

    for (i = 1; i < objc; i += 2) {
        int index;
        char *string;
        Tcl_Obj *optionPtr, *valuePtr;

        optionPtr = objv[i];
        valuePtr = objv[i + 1];

        if (Tcl_GetIndexFromObj(interp, optionPtr, optionStrings, "option",
                TCL_EXACT, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (i + 1 == objc) {
            string = Tcl_GetStringFromObj(optionPtr, NULL);
            Tcl_AppendResult(interp, "value for \"", string, "\" missing",
                    (char *) NULL);
            return TCL_ERROR;
        }

        string = Tcl_GetStringFromObj(valuePtr, NULL);
        switch ((enum options) index) {
            case COLOR_INITIAL: {
                XColor *colorPtr;

                colorPtr = Tk_GetColor(interp, tkwin, string);
                if (colorPtr == NULL) {
                    return TCL_ERROR;
                }
                startColor = RGB(colorPtr->red / 0x100, colorPtr->green / 0x100,
                                 colorPtr->blue / 0x100);
                /* pre-"choose" the color */
                chosenColor = startColor;
                break;
            }
            case COLOR_PARENT: {
                parent = Tk_NameToWindow(interp, string, tkwin);
                if (parent == NULL) {
                    return TCL_ERROR;
                }
                break;
            }
            case COLOR_TITLE: {
                Tcl_DString ds;

                /* Set title of dialog */
                Tcl_UtfToExternalDString(NULL, string, -1, &ds);
                rc = WinSetWindowText(hwndDlg, Tcl_DStringValue(&ds));
#ifdef VERBOSE
                if (rc != TRUE) {
                    printf("WinSetWindowText [%s] ERROR %x\n",
                           Tcl_DStringValue(&ds),
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("WinSetWindowText [%s] OK\n", Tcl_DStringValue(&ds));
                }
                fflush(stdout);
#endif
                Tcl_DStringFree(&ds);
                break;
            }
        }
    }

    Tk_MakeWindowExist(parent);
    hwndOwner = Tk_GetHWND(Tk_WindowId(parent));
#ifdef VERBOSE
    printf("    hwndOwner now %x\n", hwndOwner);
    fflush(stdout);
#endif

    /*
     * Set to pre-chosen color.
     * Hack for LX-versions above 2.11
     *  OS/2 version    MAJOR MINOR
     *  2.0             20    0
     *  2.1             20    10
     *  2.11            20    11
     *  3.0             20    30
     *  4.0             20    40
     */
    if (info[QSV_VERSION_MAJOR - 1] == 20 &&
        info[QSV_VERSION_MINOR - 1] >= 40) {
        /* Warp 4 or higher */
#ifdef VERBOSE
        printf("Warp 4 or higher => WM_SETCOLOR_WARP4 (%x), startColor 0x%x\n",
               WM_SETCOLOR_WARP4, startColor);
        fflush(stdout);
#endif
        WinSendMsg(hwndWheel, WM_SETCOLOR_WARP4, MPFROMLONG(0x8fff), MPVOID);
        WinSendMsg(hwndWheel, WM_SETCOLOR_WARP4, MPFROMLONG(startColor), MPVOID);
    } else {
        /* 2.0 - 3.0 */
#ifdef VERBOSE
        printf("OS/2 2.0 - 3.0 => WM_SETCOLOR_EARLIER (%x), startColor 0x%x\n",
               WM_SETCOLOR_EARLIER, startColor);
        fflush(stdout);
#endif
        WinSendMsg(hwndWheel, WM_SETCOLOR_EARLIER, MPFROMLONG(startColor),
                   MPVOID);
    }

    /*
     * 2. Popup the dialog
     */

    oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
    ulReply = WinProcessDlg(hwndDlg);
#ifdef VERBOSE
    printf("WinProcessDlg hwndDlg %x returned 0x%x (%d)\n", hwndDlg, ulReply,
           ulReply);
    fflush(stdout);
#endif
    (void) Tcl_SetServiceMode(oldMode);

    /*
     * Ensure that hwndOwner is enabled, because it can happen that we
     * have updated the wrapper of the parent, which causes us to
     * leave this child disabled (losing sync).
     */
    WinEnableWindow(hwndOwner, TRUE);

    /*
     * Clear the interp result since anything may have happened during the
     * modal loop.
     */

    Tcl_ResetResult(interp);

    /*
     * 3. Process the result of the dialog
     */
    switch (ulReply) {
    case DID_OK:
    case ID_OK: {
        /*
         * User has selected a color
         */
        char colorStr[100];

        sprintf(colorStr, "#%02x%02x%02x", GetRValue(chosenColor),
                GetGValue(chosenColor), GetBValue(chosenColor));
#ifdef VERBOSE
        printf("ulReply ID_OK, returning color %x (%s)\n", chosenColor,
               colorStr);
        fflush(stdout);
#endif
        Tcl_AppendResult(interp, colorStr, NULL);
        oldColor = chosenColor;
        result = TCL_OK;
        break;
    } 
    case ID_TKVERSION:
#ifdef VERBOSE
        printf("ulReply ID_TKVERSION\n");
        fflush(stdout);
#endif
        return EvalCmdWithObjv(interp, "tkColorDialog", objc, objv);
        break;
    case DID_CANCEL:
    case ID_CANCEL:
#ifdef VERBOSE
        printf("ulReply (D)ID_CANCEL\n");
        fflush(stdout);
#endif
        result = TCL_RETURN;
        break;
    default:
        /*
         * User probably pressed Cancel, or an error occurred
         */
#ifdef VERBOSE
        printf("ulReply default for 0x%x\n", ulReply);
        fflush(stdout);
#endif
    } /* of switch */

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ColorDlgProc --
 *
 *      This function is called by OS/2 PM whenever an event occurs on
 *      a color dialog control created by Tk.
 *
 * Results:
 *      Standard OS/2 PM return value.
 *
 * Side effects:
 *      May generate events.
 *
 *----------------------------------------------------------------------
 */

static MRESULT EXPENTRY
ColorDlgProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    MRESULT ret;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("ColorDlgProc hwnd %x msg %x mp1 %x mp2 %x\n", hwnd, message, param1,
           param2);
    fflush(stdout);
#endif
    if (message == WM_CHOSENCOLOR_WARP4 || message == WM_CHOSENCOLOR_EARLIER) {
        chosenColor = LONGFROMMP(param1);
#ifdef VERBOSE
        printf("Message %x from color dialog, color %x\n", message,chosenColor);
        fflush(stdout);
#endif
    }
    ret = (MRESULT) oldDlgProc(hwnd, message, param1, param2);
#ifdef VERBOSE
    printf("oldDlgProc returned 0x%x (%d)\n", ret, ret);
    fflush(stdout);
#endif
    return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetOpenFileObjCmd --
 *
 *	This procedure implements the "open file" dialog box for the
 *	OS/2 platform. See the user documentation for details on what
 *	it does.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	A dialog window is created the first this procedure is called.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetOpenFileObjCmd(clientData, interp, objc, objv)
    ClientData clientData;	/* Main window associated with interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    /* "Unix look-and-feel"
    return EvalCmdWithObjv(interp, "tkFDialog", objc, objv);
    */
    /* OS/2 look-and-feel */
    return GetFileName(clientData, interp, objc, objv, 1);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetSaveFileObjCmd --
 *
 *	Same as Tk_GetOpenFileObjCmd but opens a "save file" dialog box
 *	instead
 *
 * Results:
 *	Same as Tk_GetOpenFileObjCmd.
 *
 * Side effects:
 *	Same as Tk_GetOpenFileObjCmd.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetSaveFileObjCmd(clientData, interp, objc, objv)
    ClientData clientData;	/* Main window associated with interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    /* "Unix look-and-feel"
    return EvalCmdWithObjv(interp, "tkFDialog", objc, objv);
    */
    /* OS/2 look-and-feel */
    return GetFileName(clientData, interp, objc, objv, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * GetFileName --
 *
 *	Create File Open or File Save Dialog.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	See user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
GetFileName(clientData, interp, objc, objv, open)
    ClientData clientData;	/* Main window associated with interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
    int open;			/* 1 to call GetOpenFileName(), 0 to
				 *  call GetSaveFileName(). */
{
    char file[TK_MULTI_MAX_PATH];
    int result, winCode, oldMode, i, multi = 0;
    char *extension, *filter, *title;
    Tk_Window tkwin;
    Tcl_DString utfFilterString, utfDirString;
    Tcl_DString extString, filterString, dirString, titleString;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    static char *optionStrings[] = {
        "-defaultextension", "-filetypes", "-initialdir", "-initialfile",
        "-parent", "-title", NULL
    };
    ULONG length = MAX_PATH+1;
    ULONG curDrive, logical;
    char buffer[MAX_PATH+1];
    HWND hwndParent, hwndDlg;
    FILEDLG fileDlg;
    BOOL hadInitialFile = FALSE;
    BOOL hadDefExtension = FALSE;
    ERRORID errorId = NO_ERROR;
    FileFilterList flist;

    enum options {
        FILE_DEFAULT,   FILE_TYPES,     FILE_INITDIR,   FILE_INITFILE,
        FILE_PARENT,    FILE_TITLE
    };

#ifdef VERBOSE
    printf("GetFileName\n");
    fflush(stdout);
#endif

    result = TCL_ERROR;
    file[0] = '\0';

    memset(&fileDlg, 0, sizeof(FILEDLG));
    fileDlg.cbSize = sizeof(FILEDLG);
    /* Remember current dir and disk */
    rc = DosQueryCurrentDisk(&curDrive, &logical);
#ifdef VERBOSE
    if (rc != NO_ERROR) {
        printf("DosQueryCurrentDisk ERROR %d\n", rc);
        fflush(stdout);
    } else {
        printf("DosQueryCurrentDisk OK\n");
        fflush(stdout);
    }
#endif
    rc = DosQueryCurrentDir(0, (PBYTE)&buffer, &length);
#ifdef VERBOSE
    if (rc != NO_ERROR) {
        printf("DosQueryCurrentDir ERROR %d\n", rc);
        fflush(stdout);
    } else {
        printf("DosQueryCurrentDir OK\n");
        fflush(stdout);
    }
#endif
    
    /*
     * Parse the arguments.
     */

    extension = NULL;
    filter = NULL;
    Tcl_DStringInit(&utfFilterString);
    Tcl_DStringInit(&utfDirString);
    tkwin = (Tk_Window) clientData;
    title = NULL;

    for (i = 1; i < objc; i += 2) {
        int index;
        char *string;
        Tcl_Obj *optionPtr, *valuePtr;

        optionPtr = objv[i];
        valuePtr = objv[i + 1];

        if (Tcl_GetIndexFromObj(interp, optionPtr, optionStrings,
                "option", 0, &index) != TCL_OK) {
            goto end;
        }
        if (i + 1 == objc) {
            string = Tcl_GetStringFromObj(optionPtr, NULL);
            Tcl_AppendResult(interp, "value for \"", string, "\" missing",
                    (char *) NULL);
            goto end;
        }

        string = Tcl_GetStringFromObj(valuePtr, NULL);
        switch ((enum options) index) {
            case FILE_DEFAULT: {
                if (string[0] == '.') {
                    string++;
                }
                extension = string;
                if (hadInitialFile) {
                    /* Add default extension if necessary */
                    if (strchr(fileDlg.szFullFile, '.') == NULL) {
                        /* No extension given */
    #ifdef VERBOSE
                        printf("ext %s for initialfile %s\n", extension,
                               fileDlg.szFullFile);
                        fflush(stdout);
#endif
                        strcat(fileDlg.szFullFile, extension);
                    }
                }
                hadDefExtension = TRUE;
                break;
            }
            case FILE_TYPES: {
                Tcl_DStringFree(&utfFilterString);
                if (MakeFilter(interp, string, &utfFilterString, &fileDlg)
                    != TCL_OK) {
                    goto end;
                }
                filter = Tcl_DStringValue(&utfFilterString);
                break;
            }
            case FILE_INITDIR: {
                ULONG diskNum;
                char drive, *dirName;

                Tcl_DStringFree(&utfDirString);
                if (Tcl_TranslateFileName(interp, string,
                        &utfDirString) == NULL) {
                    goto end;
                }
                dirName = Tcl_UtfToExternalDString(NULL, dirName, -1,
                                                   &utfDirString);
                if (! (dirName[0] == '\\' && dirName[1] == '\\')) {
                    drive = dirName[0];
                    diskNum = (ULONG) drive - 'A' + 1;
                    if (drive >= 'a') {
                        diskNum -= ('a' - 'A');
                    }
                    rc = DosSetDefaultDisk(diskNum);
#ifdef VERBOSE
                    if (rc != NO_ERROR) {
                        printf("DosSetDefaultDisk %c (%d) ERROR %d\n", drive,
                               diskNum, rc);
                        fflush(stdout);
                    } else {
                        printf("DosSetDefaultDisk %c (%d) OK\n", drive,diskNum);
                        fflush(stdout);
                    }
#endif
                }
                rc = DosSetCurrentDir(dirName + 2);
#ifdef VERBOSE
                if (rc != NO_ERROR) {
                    printf("DosSetCurrentDir %s ERROR %d\n", dirName+2, rc);
                    fflush(stdout);
                } else {
                    printf("DosSetCurrentDir %s OK\n", dirName+2);
                    fflush(stdout);
                }
#endif
                break;
            }
            case FILE_INITFILE: {
                Tcl_DString ds;

                if (Tcl_TranslateFileName(interp, string, &ds) == NULL) {
                    goto end;
                }
                Tcl_UtfToExternalDString(NULL, file, -1, &ds);
                strcpy(fileDlg.szFullFile, file);
                if (hadDefExtension) {
                    if (strchr(fileDlg.szFullFile, '.') == NULL) {
                        /* No extension given */
#ifdef VERBOSE
                        printf("initialfile %s gets extension %s\n", file,
                               extension);
                        fflush(stdout);
#endif
                        strcat(fileDlg.szFullFile, extension);
                    }
                }
                hadInitialFile = TRUE;
                break;
            }
            case FILE_PARENT: {
                tkwin = Tk_NameToWindow(interp, string, tkwin);
                if (tkwin == NULL) {
                    goto end;
                }
                break;
            }
            case FILE_TITLE: {
                Tcl_DString ds;

                Tcl_UtfToExternalDString(NULL, string, -1, &ds);
                fileDlg.pszTitle = Tcl_DStringValue(&ds);
                break;
            }
        }
    }

    if (filter == NULL) {
        if (MakeFilter(interp, "", &utfFilterString, &fileDlg) != TCL_OK) {
            goto end;
        }
    }

    Tk_MakeWindowExist(tkwin);
    hwndParent = Tk_GetHWND(Tk_WindowId(tkwin));

    /* Fill in the FILEDLG structure */
    if (open == 0) {
        fileDlg.fl = FDS_OPEN_DIALOG | FDS_CENTER | FDS_ENABLEFILELB |
                      FDS_FILTERUNION | FDS_PRELOAD_VOLINFO;
    } else {
        fileDlg.fl = FDS_SAVEAS_DIALOG | FDS_CENTER | FDS_ENABLEFILELB |
                      FDS_FILTERUNION | FDS_PRELOAD_VOLINFO;
    }

    if (multi != 0) {
        fileDlg.fl |= FDS_MULTIPLESEL;
    }

    Tcl_UtfToExternalDString(NULL, Tcl_DStringValue(&utfFilterString),
                             Tcl_DStringLength(&utfFilterString),
                             &filterString);
    strcpy(fileDlg.szFullFile, Tcl_DStringValue(&filterString));

    /*
     * 2. Call the common dialog function.
     */
    oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
    hwndDlg = WinFileDlg(HWND_DESKTOP, hwndParent, &fileDlg);
    (void) Tcl_SetServiceMode(oldMode);

#ifdef VERBOSE
    printf("fileDlg.lReturn %x\n", fileDlg.lReturn);
#endif

    /*
     * Ensure that hwndParent is enabled, because it can happen that we
     * have updated the wrapper of the parent, which causes us to
     * leave this child disabled (losing sync).
     */
    WinEnableWindow(hwndParent, TRUE);

    /*
     * Clear the interp result since anything may have happened during the
     * modal loop.
     */

    Tcl_ResetResult(interp);

    /* Reset default disk and dir */
    rc = DosSetDefaultDisk(curDrive);
    rc = DosSetCurrentDir(buffer);

    /*
     * Process the results.
     */

    if (hwndDlg && (fileDlg.lReturn == DID_OK)) {
        /*
         * If more than one file has been selected, fileDlg.ulFQFCount
         * will contain the number of pointers to fully specified file
         * names in the table pointed to by fileDlg.papszFQFilename.
         * The storage for the table is allocated by the file dialog.
         * When done accessing it, the application must call WinFreeFileDlgList
         * to free the storage.
         * When the user types a file name in the file name entry field,
         * that name will be in fileDlg.szFullFile and the table pointer
         * will be empty. When more files have been selected,
         * fileDlg.szFullFile will contain only the topmost selected name.
         */
        if ( (fileDlg.fl & FDS_MULTIPLESEL) && (fileDlg.ulFQFCount > 1)) {
            PSZ filename;
            char *p;
            Tcl_DString ds;
            Tcl_Obj *returnList;
            int count = 0;

            returnList = Tcl_NewObj();
            Tcl_IncrRefCount(returnList);

            while (count < fileDlg.ulFQFCount) {
                filename = *(fileDlg.papszFQFilename)[count];
                Tcl_ExternalToUtfDString(NULL, (char *)filename, -1, &ds);
	        for (p = Tcl_DStringValue(&ds); *p != '\0'; p++) {
	            /*
	             * Change the pathname to the Tcl "normalized" pathname,
	             * where back slashes are used instead of forward slashes
	             */
	            if (*p == '\\') {
	                *p = '/';
	            }
	        }
#ifdef VERBOSE
                printf("    adding file [%s]\n", Tcl_DStringValue(&ds));
                fflush(stdout);
#endif
                Tcl_ListObjAppendElement(interp, returnList,
                                         Tcl_NewStringObj(Tcl_DStringValue(&ds),
                                         -1));
                Tcl_DStringFree(&ds);

                count++;
            }
            /* Free the file dialog allocated table of pointers to file names */
            WinFreeFileDlgList(fileDlg.papszFQFilename);
            Tcl_SetObjResult(interp, returnList);
            Tcl_DecrRefCount(returnList);
        } else {
            char *p;
            Tcl_DString ds;

            Tcl_ExternalToUtfDString(NULL, (char *)fileDlg.szFullFile, -1, &ds);
	    for (p = Tcl_DStringValue(&ds); *p != '\0'; p++) {
	        /*
	         * Change the pathname to the Tcl "normalized" pathname,
	         * where back slashes are used instead of forward slashes
	         */
	        if (*p == '\\') {
	            *p = '/';
	        }
	    }
	    Tcl_AppendResult(interp, Tcl_DStringValue(&ds), NULL);
            Tcl_DStringFree(&ds);
        }
        result = TCL_OK;
    } else {
	if (fileDlg.lReturn == DID_CANCEL) {
	    /* User hit Cancel */
	    result = TCL_OK;
	} else {
            char *p;
            Tcl_DString ds;

            Tcl_DStringInit(&ds);
            switch (fileDlg.lSRC) {
            case FDS_ERR_INVALID_DRIVE:
                Tcl_SetResult(interp, "invalid drive \"", TCL_STATIC);
	        result = TCL_ERROR;
                break;
            case FDS_ERR_DRIVE_ERROR:
                Tcl_SetResult(interp, "drive error \"", TCL_STATIC);
	        result = TCL_ERROR;
                break;
            case FDS_ERR_TOO_MANY_FILE_TYPES:
            case FDS_ERR_INVALID_FILTER: {
                Tcl_DString add;
                PSZ typePtr;

                if (fileDlg.pszIType != NULL) {
                    Tcl_ExternalToUtfDString(NULL, (char *) fileDlg.pszIType,
                                             -1, &ds);
                }
                for (typePtr = *(fileDlg.papszITypeList)[0]; typePtr != NULL;
                     typePtr += strlen(typePtr)) {
                    Tcl_ExternalToUtfDString(NULL, (char *) typePtr, -1, &add);
                    Tcl_DStringAppendElement(&ds, Tcl_DStringValue(&add));
                }
                if (fileDlg.lSRC == FDS_ERR_INVALID_FILTER) {
                    Tcl_SetResult(interp, "invalid filter(s) \"", TCL_STATIC);
                } else {
                    Tcl_SetResult(interp, "too many filetypes \"", TCL_STATIC);
                }
	        result = TCL_ERROR;
                break;
            }
            case FDS_ERR_INVALID_PATHFILE:
                Tcl_ExternalToUtfDString(NULL, (char *) fileDlg.szFullFile, -1,
                                         &ds);
                Tcl_SetResult(interp, "invalid path / filename \"", TCL_STATIC);
	        result = TCL_ERROR;
                break;
            case FDS_ERR_PATH_TOO_LONG:
                Tcl_SetResult(interp, "invalid filename \"", TCL_STATIC);
	        result = TCL_ERROR;
                break;
            default:
                /* ignore */
	        result = TCL_OK;
                break;
            }

            if (result == TCL_ERROR) {
                for (p = Tcl_DStringValue(&ds); *p != '\0'; p++) {
                    /*
                     * Change the pathname to the Tcl "normalized" pathname,
                     * where back slashes are used instead of forward slashes
                     */
                    if (*p == '\\') {
                        *p = '/';
                    }
                }
                Tcl_AppendResult(interp, Tcl_DStringValue(&ds), "\"", NULL);
                Tcl_DStringFree(&ds);
            }
        }
    }

    if (fileDlg.papszITypeList) {
	ckfree((char*)fileDlg.papszITypeList);
    }
    if (fileDlg.papszIDriveList) {
	ckfree((char*)fileDlg.papszIDriveList);
    }

    end:
    Tcl_DStringFree(&utfDirString);
    Tcl_DStringFree(&utfFilterString);

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeFilter --
 *
 *	Allocate a buffer to store the filters and types in a format
 *      understood by OS/2
 *
 * Results:
 *	A standard TCL return value.
 *
 * Side effects:
 *	fileDlgPtr->pszIType, papszITypeList, szFullFile are modified.
 *
 *----------------------------------------------------------------------
 */
static int MakeFilter(interp, string, dsPtr, fileDlgPtr) 
    Tcl_Interp *interp;		/* Current interpreter. */
    char *string;		/* String value of the -filetypes option */
    Tcl_DString *dsPtr;         /* Filled with OS/2 filter string. */
    FILEDLG *fileDlgPtr;   	/* Add EA list to this file dialog */
{
    char *filterStr;
    char *p;
    int pass;
    FileFilterList flist;
    FileFilter *filterPtr;

    TkInitFileFilters(&flist);
    if (TkGetFileFilters(interp, &flist, string, 1) != TCL_OK) {
#ifdef VERBOSE
        printf("MakeFilter, TkGetFileFilters failed\n");
        fflush(stdout);
#endif
	return TCL_ERROR;
    }

#ifdef VERBOSE
    printf("MakeFilter, %d filter(s): %s\n", flist.numFilters, string);
    fflush(stdout);
#endif

    /*
     * Since the full file name can only contain CCHMAXPATH characters, we
     * don't need to (cannot) allocate more space.
     */
    filterStr = (CHAR *) ckalloc(CCHMAXPATH);
    if (filterStr == (CHAR *)NULL) {
        return TCL_ERROR;
    }

    if (flist.filters == NULL) {
	/*
	 * Use "All Files" (*.*) as the default filter is none is specified
	 */
	char *defaultFilter = "*.*";

	strcpy(filterStr, defaultFilter);
#ifdef VERBOSE
        printf("    default filter %s\n", defaultFilter);
        fflush(stdout);
#endif
    } else {
	/*
	 * We put the filter types in a table, and format the extension
	 * into the full filename field.
	 * BEWARE! Specifying the same extension twice gets you a crash
	 * in PMCTLS.DLL, so make sure that doesn't happen.
	 */

        char *sep;
	int typeCounter;

	filterStr[0] = '\0';
	/* Table of extended-attribute types, *END WITH NULL!* */
        fileDlgPtr->papszITypeList = (PAPSZ) ckalloc((unsigned int)
                                            flist.numFilters * sizeof(PSZ) + 1);
	if (fileDlgPtr->papszITypeList == (PAPSZ)NULL) {
            ckfree((char *)filterStr);
	    return TCL_ERROR;
	}

        sep = "";
	for (filterPtr = flist.filters, typeCounter=0, p = filterStr;
	        filterPtr; filterPtr = filterPtr->next, typeCounter++) {
	    FileFilterClause *clausePtr;

	    /*
	     *  First, put in the name of the file type
	     */
	    *(fileDlgPtr->papszITypeList)[typeCounter] = (PSZ)filterPtr->name;
#ifdef VERBOSE
            printf("    adding type %s\n", filterPtr->name);
            fflush(stdout);
#endif

            /* We format the extensions in the filter pattern field */
            for (clausePtr=filterPtr->clauses;clausePtr;
                     clausePtr=clausePtr->next) {
                GlobPattern *globPtr;
            
                for (globPtr=clausePtr->patterns; globPtr;
                     globPtr=globPtr->next) {
                    char *sub = strstr(filterStr, globPtr->pattern);
                    /*
                     * See if pattern is already in filterStr. Watch out for
                     * it being there as a substring of another pattern!
                     * eg. *.c is part of *.cpp
                     */
                    if (sub == NULL ||
                        (*(sub+strlen(globPtr->pattern)) != ';' &&
                         *(sub+strlen(globPtr->pattern)) != '\0')) {
/*
if (strncmp(globPtr->pattern, "*.*", 3) !=0 ) {
*/
                        strcpy(p, sep);
                        p+= strlen(sep);
                        strcpy(p, globPtr->pattern);
#ifdef VERBOSE
                        printf("    adding pattern %s, filterStr %s\n",
                               globPtr->pattern, filterStr);
                        fflush(stdout);
#endif
                        p+= strlen(globPtr->pattern);
                        sep = ";";
/*
}
*/
                    }
#ifdef VERBOSE
                      else {
                        printf("not re-adding pattern %s\n", globPtr->pattern);
                    }
#endif
                }
            }
        }
        /* End table with NULL! */
	*(fileDlgPtr->papszITypeList)[typeCounter] = (PSZ)NULL;
        /* Don't specify initial type, so extensions can play too */
    }

    Tcl_DStringAppend(dsPtr, filterStr, (int) (p - filterStr));
    ckfree((char *)filterStr);

    TkFreeFileFilters(&flist);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MessageBoxObjCmd --
 *
 *	This procedure implements the MessageBox window for the
 *	OS/2 platform. See the user documentation for details on what
 *	it does.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	None. The MessageBox window will be destroy before this procedure
 *	returns.
 *
 *----------------------------------------------------------------------
 */

int
Tk_MessageBoxObjCmd(clientData, interp, objc, objv)
    ClientData clientData;	/* Main window associated with interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    Tk_Window tkwin, parent;
    HWND hWnd;
    char *message, *title;
    int defaultBtn, icon, type;
    int i, oldMode, flags, code;
    Tcl_DString messageString, titleString;
    static char *optionStrings[] = {
        "-default",     "-icon",        "-message",     "-parent",
        "-title",       "-type",        NULL
    };
    enum options {
        MSG_DEFAULT,    MSG_ICON,       MSG_MESSAGE,    MSG_PARENT,
        MSG_TITLE,      MSG_TYPE
    };

#ifdef VERBOSE
    printf("Tk_MessageBoxCmd\n");
#endif

    tkwin = (Tk_Window) clientData;

    defaultBtn  = -1;
    icon        = MB_INFORMATION;
    message     = NULL;
    parent      = tkwin;
    title       = NULL;
    type        = MB_OK;

    for (i = 1; i < objc; i += 2) {
        int index;
        char *string;
        Tcl_Obj *optionPtr, *valuePtr;

        optionPtr = objv[i];
        valuePtr = objv[i + 1];

        if (Tcl_GetIndexFromObj(interp, optionPtr, optionStrings, "option",
                TCL_EXACT, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (i + 1 == objc) {
            string = Tcl_GetStringFromObj(optionPtr, NULL);
            Tcl_AppendResult(interp, "value for \"", string, "\" missing",
                    (char *) NULL);
            return TCL_ERROR;
        }

        string = Tcl_GetStringFromObj(valuePtr, NULL);
        switch ((enum options) index) {
        case MSG_DEFAULT:
            defaultBtn = TkFindStateNumObj(interp, optionPtr, buttonMap,
                    valuePtr);
            if (defaultBtn < 0) {
                return TCL_ERROR;
            }
            break;

        case MSG_ICON:
            icon = TkFindStateNumObj(interp, optionPtr, iconMap, valuePtr);
            if (icon < 0) {
                return TCL_ERROR;
            }
            break;

        case MSG_MESSAGE:
            message = string;
            break;

        case MSG_PARENT:
            parent = Tk_NameToWindow(interp, string, tkwin);
            if (parent == NULL) {
                return TCL_ERROR;
            }
            break;

        case MSG_TITLE:
            title = string;
            break;

        case MSG_TYPE:
            type = TkFindStateNumObj(interp, optionPtr, typeMap, valuePtr);
            if (type < 0) {
                return TCL_ERROR;
            }
            break;

        }
    }

    Tk_MakeWindowExist(parent);
    hWnd = Tk_GetHWND(Tk_WindowId(parent));

    flags = 0;
    if (defaultBtn >= 0) {
        int defaultBtnIdx;

        defaultBtnIdx = -1;
        for (i = 0; i < NUM_TYPES; i++) {
            if (type == allowedTypes[i].type) {
                int j;

                for (j = 0; j < 3; j++) {
                    if (allowedTypes[i].btnIds[j] == defaultBtn) {
                        defaultBtnIdx = j;
                        break;
                    }
                }
                if (defaultBtnIdx < 0) {
                    Tcl_AppendResult(interp, "invalid default button \"",
                            TkFindStateString(buttonMap, defaultBtn),
                            "\"", NULL);
                    return TCL_ERROR;
                }
                break;
            }
        }
        flags = buttonFlagMap[defaultBtnIdx];
    }

    flags |= icon | type;

    Tcl_UtfToExternalDString(NULL, message, -1, &messageString);
    Tcl_UtfToExternalDString(NULL, title, -1, &titleString);

    oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
#ifdef VERBOSE
    printf("WinMessageBox [%s] title [%s], flags %x\n", message, title, flags);
#endif
    /* Windows Port uses SYSTEM modal dialog, I use application modal */
    code = WinMessageBox(HWND_DESKTOP, hWnd,
                            Tcl_DStringValue(&messageString),
                            Tcl_DStringValue(&titleString),
                            0, flags | MB_APPLMODAL);
    (void) Tcl_SetServiceMode(oldMode);

    /*
     * Ensure that hWnd is enabled, because it can happen that we
     * have updated the wrapper of the parent, which causes us to
     * leave this child disabled (Windows loses sync).
     */
    WinEnableWindow(hWnd, TRUE);

    Tcl_DStringFree(&messageString);
    Tcl_DStringFree(&titleString);

    Tcl_SetResult(interp, TkFindStateString(buttonMap, code), TCL_STATIC);
    return TCL_OK;
}

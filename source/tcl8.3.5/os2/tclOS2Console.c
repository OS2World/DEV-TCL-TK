/*
 * tclOS2Console.c --
 *
 *      This file implements the OS/2-specific console functions,
 *      and the "console" channel driver.
 *      Also OS/2 PM console window class definition.
 *
 * Copyright (c) 1994 Software Research Associates, Inc.
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1999 by Scriptics Corp.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tclOS2Int.h"

#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <sys/stat.h>

/*
 * The following variable is used to tell whether this module has been
 * initialized.
 */

static int initialized = 0;

/*
 * The consoleMutex locks around access to the initialized variable, and it is
 * used to protect background threads from being terminated while they are
 * using APIs that hold locks.
 */

TCL_DECLARE_MUTEX(consoleMutex)

/*
 * Bit masks used in the flags field of the ConsoleInfo structure below.
 */

#define CONSOLE_PENDING (1<<0)  /* Message is pending in the queue. */
#define CONSOLE_ASYNC   (1<<1)  /* Channel is non-blocking. */

/*
 * Bit masks used in the sharedFlags field of the ConsoleInfo structure below.
 */

#define CONSOLE_EOF       (1<<2)  /* Console has reached EOF. */
#define CONSOLE_BUFFERED  (1<<3)  /* data was read into a buffer by the reader
                                     thread */

#define CONSOLE_BUFFER_SIZE (8*1024)
/*
 * This structure describes per-instance data for a console based channel.
 */

typedef struct ConsoleInfo {
    HFILE handle;
    int type;
    struct ConsoleInfo *nextPtr;/* Pointer to next registered console. */
    Tcl_Channel channel;        /* Pointer to channel structure. */
    int validMask;              /* OR'ed combination of TCL_READABLE,
                                 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
                                 * which operations are valid on the file. */
    int watchMask;              /* OR'ed combination of TCL_READABLE,
                                 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
                                 * which events should be reported. */
    int flags;                  /* State flags, see above for a list. */
    Tcl_ThreadId threadId;      /* Thread to which events should be reported.
                                 * This value is used by the reader/writer
                                 * threads. */
    TID writeThread;            /* Handle to writer thread. */
    TID readThread;             /* Handle to reader thread. */
    HEV writable;               /* Manual-reset event to signal when the
                                 * writer thread has finished waiting for
                                 * the current buffer to be written. */
    HEV readable;               /* Manual-reset event to signal when the
                                 * reader thread has finished waiting for
                                 * input. */
    HEV startWriter;            /* Auto-reset event used by the main thread to
                                 * signal when the writer thread should attempt
                                 * to write to the console. */
    HEV startReader;            /* Auto-reset event used by the main thread to
                                 * signal when the reader thread should attempt
                                 * to read from the console. */
    ULONG writeError;           /* An error caused by the last background
                                 * write.  Set to 0 if no error has been
                                 * detected.  This word is shared with the
                                 * writer thread so access must be
                                 * synchronized with the writable object.
                                 */
    char *writeBuf;             /* Current background output buffer.
                                 * Access is synchronized with the writable
                                 * object. */
    int writeBufLen;            /* Size of write buffer.  Access is
                                 * synchronized with the writable
                                 * object. */
    int toWrite;                /* Current amount to be written.  Access is
                                 * synchronized with the writable object. */
    int readFlags;              /* Flags that are shared with the reader
                                 * thread.  Access is synchronized with the
                                 * readable object.  */
    ULONG bytesRead;            /* number of bytes in the buffer */
    int offset;                 /* number of bytes read out of the buffer */
    char buffer[CONSOLE_BUFFER_SIZE];
                                /* Data consumed by reader thread. */
} ConsoleInfo;

typedef struct ThreadSpecificData {
    /*
     * The following pointer refers to the head of the list of consoles
     * that are being watched for file events.
     */

    ConsoleInfo *firstConsolePtr;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

/*
 * The following structure is what is added to the Tcl event queue when
 * console events are generated.
 */

typedef struct ConsoleEvent {
    Tcl_Event header;           /* Information that is standard for
                                 * all events. */
    ConsoleInfo *infoPtr;       /* Pointer to console info structure.  Note
                                 * that we still have to verify that the
                                 * console exists before dereferencing this
                                 * pointer. */
} ConsoleEvent;

/*
 * Stuff for the PM console.
 */

/* Predefined control identifiers. */
#define IDC_EDIT    1
#define MLN_USER    0xff

#define MAX(a,b)  ( (a) > (b) ? (a) : (b) )

/* Initial screen size. */
#define INIT_SCREEN_CX    80
#define INIT_SCREEN_CY    25

static HWND hwndEdit;        /* Handle for edit control. */
#define APP_NAME "TclPM " ## TCL_VERSION
static char szAppName[] = APP_NAME;
static int cxFrame, cyFrame, cyCaption, cxVScroll;
static Tcl_DString command;     /* Used to buffer incomplete commands. */
char cmdBuf[256];        /* Line buffer for commands */
IPT insPoint;
PFNWP oldEditProc = NULL;    /* Pointer to system Edit control procedure */


/*
 * Declarations for functions used only in this file.
 */

static void             DisplayString _ANSI_ARGS_((char *str, int newline));
static MRESULT EXPENTRY TerminalProc _ANSI_ARGS_((HWND, ULONG, MPARAM, MPARAM));
static MRESULT EXPENTRY EditProc _ANSI_ARGS_((HWND, ULONG, MPARAM, MPARAM));
static int              TerminalPutsCmd _ANSI_ARGS_((ClientData clientData,
                                Tcl_Interp *interp, int argc, char **argv));

static int              ConsoleBlockModeProc(ClientData instanceData, int mode);
static void             ConsoleCheckProc(ClientData clientData, int flags);
static int              ConsoleCloseProc(ClientData instanceData,
                            Tcl_Interp *interp);
static int              ConsoleEventProc(Tcl_Event *evPtr, int flags);
static void             ConsoleExitHandler(ClientData clientData);
static int              ConsoleGetHandleProc(ClientData instanceData,
                            int direction, ClientData *handlePtr);
static ThreadSpecificData *ConsoleInit(void);
static int              ConsoleInputProc(ClientData instanceData, char *buf,
                            int toRead, int *errorCode);
static int              ConsoleOutputProc(ClientData instanceData, char *buf,
                            int toWrite, int *errorCode);
static void             ConsoleReaderThread(void *arg);
static void             ConsoleSetupProc(ClientData clientData, int flags);
static void             ConsoleWatchProc(ClientData instanceData, int mask);
static void             ConsoleWriterThread(void *arg);
static void             ProcExitHandler(ClientData clientData);
static int              WaitForRead(ConsoleInfo *infoPtr, int blocking);

/*
 * This structure describes the channel type structure for command console
 * based IO.
 */

static Tcl_ChannelType consoleChannelType = {
    "console",                  /* Type name. */
    TCL_CHANNEL_VERSION_2,      /* v2 channel */
    ConsoleCloseProc,           /* Close proc. */
    ConsoleInputProc,           /* Input proc. */
    ConsoleOutputProc,          /* Output proc. */
    NULL,                       /* Seek proc. */
    NULL,                       /* Set option proc. */
    NULL,                       /* Get option proc. */
    ConsoleWatchProc,           /* Set up notifier to watch the channel. */
    ConsoleGetHandleProc,       /* Get an OS handle from channel. */
    NULL,                       /* close2proc. */
    ConsoleBlockModeProc,       /* Set blocking or non-blocking mode.*/
    NULL,                       /* flush proc. */
    NULL,                       /* handler proc. */
};

/*
 *----------------------------------------------------------------------
 *
 * RegisterTerminalClass --
 *
 *    Creates the application class for the console window.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The global window class "Tclsh" is created.
 *
 *----------------------------------------------------------------------
 */

BOOL
RegisterTerminalClass(hab)
    HAB hab;
{            
    return WinRegisterClass(hab, "Terminal", TerminalProc, CS_SIZEREDRAW,
                            sizeof(Tcl_Interp*));
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTerminal --
 *
 *    Creates an instance of the Tclsh console window.
 *
 * Results:
 *    A Window handle for the newly created instance.
 *
 * Side effects:
 *    Creates a new window instance with a pointer to the
 *    interpreter stored in the window instance data.
 *
 *----------------------------------------------------------------------
 */

HWND
CreateTerminal(hab, interp)
    HAB hab;
    Tcl_Interp *interp;
{
    HPS hps;
    FONTMETRICS fm;
    HWND hwnd, hFrame;
    ULONG flags = FCF_TITLEBAR | FCF_SYSMENU | FCF_MINMAX | FCF_SHELLPOSITION |
                  FCF_SIZEBORDER | FCF_TASKLIST;

    Tcl_DStringInit(&command);
    hwnd = hFrame = NULLHANDLE;
    cxFrame = WinQuerySysValue(HWND_DESKTOP, SV_CXSIZEBORDER);
    cyFrame = WinQuerySysValue(HWND_DESKTOP, SV_CYSIZEBORDER);
    cyCaption = WinQuerySysValue(HWND_DESKTOP, SV_CYTITLEBAR);
    cxVScroll = WinQuerySysValue(HWND_DESKTOP, SV_CXVSCROLL);

    hFrame= WinCreateStdWindow(HWND_DESKTOP, 0, &flags, "Terminal",
                               szAppName, 0, NULLHANDLE, 1, &hwnd);
    hps = WinGetPS(HWND_DESKTOP);
    if (hwnd != NULLHANDLE) {
        PSZ font = "8.Courier";
        WinSetPresParam(hwnd, PP_FONTNAMESIZE, strlen(font), (PVOID)font);
        /* This next setting is important in TerminalProc */
        WinSetWindowULong(hwnd, QWL_USER, (ULONG)interp);
        if (GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm)) {
            /* Select system font? */
            WinSetWindowPos(hFrame, HWND_TOP, 0, 0,
                  (fm.lAveCharWidth * INIT_SCREEN_CX)+(cxFrame * 2)+ cxVScroll,
                  (fm.lMaxBaselineExt * INIT_SCREEN_CY)+cyCaption+(cyFrame * 2),
                  SWP_SIZE | SWP_SHOW);
            Tcl_CreateCommand(interp, "puts", TerminalPutsCmd, NULL, NULL);
        }
        WinReleasePS(hps);
    }
    return hwnd;
}

/*
 *----------------------------------------------------------------------
 *
 * TerminalProc --
 *
 *    Window procedure for the Tclsh "Terminal" class.
 *
 * Results:
 *    The usual Window procedure values.
 *
 * Side effects:
 *    On window creation, it creates an edit child window.  Most
 *    of the messages are forwarded to the child.
 *
 *----------------------------------------------------------------------
 */

static MRESULT EXPENTRY
TerminalProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    switch(message) {
    case WM_CREATE: {
        MLECTLDATA mleCtlData;
        IPT firstPos;

        mleCtlData.cbCtlData = sizeof(mleCtlData);
        mleCtlData.afIEFormat = MLFIE_CFTEXT;
        /*
         * Don't specify unbounded text limit by giving -1, so we don't
         * groooowwwwwww the swapfile. The limit will be raised by overflows
         * that don't fit into this limit; other overflows will cause silent
         * deletion of that amount from the beginning.
         */
        mleCtlData.cchText = 1024 * 1024;   /* 1 MB */
        mleCtlData.iptAnchor = 0;
        mleCtlData.iptCursor = 0;
        mleCtlData.cxFormat = 0;
        mleCtlData.cyFormat = 0;
        mleCtlData.afFormatFlags = MLFFMTRECT_MATCHWINDOW;
        hwndEdit = WinCreateWindow(hwnd, WC_MLE, NULL,
                                   WS_VISIBLE | MLS_HSCROLL | MLS_VSCROLL |
                                   MLS_BORDER | MLS_WORDWRAP, 0, 0, 0, 0, hwnd,
                                   HWND_TOP, IDC_EDIT, &mleCtlData, NULL);
        oldEditProc = WinSubclassWindow(hwndEdit, EditProc);
        /* Have the first prompt displayed */
        insPoint = (IPT) WinSendMsg(hwndEdit, MLM_QUERYFIRSTCHAR, (MPARAM)0,
                                    (MPARAM)0);
        firstPos = insPoint;
        DisplayString("Welcome to the Tcl shell "TCL_VERSION" for OS/2 Presentation Manager", 1);
        DisplayString("% ", 0);
        WinSendMsg(hwndEdit, MLM_SETFIRSTCHAR, (MPARAM)firstPos, (MPARAM)0);
        /*
        insPoint = (IPT)WinSendMsg(hwndEdit, MLM_CHARFROMLINE, MPFROMLONG(-1),
                                   0);
        */
#ifdef VERBOSE
        {
        LONG limit = (LONG) WinSendMsg(hwndEdit, MLM_QUERYTEXTLIMIT, (MPARAM)0,
                                       (MPARAM)0);
        printf("MLE text limit is %d\n", limit);
        fflush(stdout);
        }
#endif
        return 0;
    }

    case WM_CONTROL:
        if (SHORT1FROMMP(param1) == IDC_EDIT) {
            /* This is our MLE calling */
            switch (SHORT2FROMMP(param1)) {
            case MLN_USER: {
                int length, offset, exp;
                char *cmd;

                /*
                 * Get line containing cursor.
                 */

                /* Get line length */
                length = (int)WinSendMsg(hwndEdit, MLM_QUERYLINELENGTH,
                                         MPFROMLONG(insPoint), 0);
                /* Set export buffer */
                if (!WinSendMsg(hwndEdit, MLM_SETIMPORTEXPORT, MPFROMP(cmdBuf),
                                MPFROMLONG(length))) {
                    break;
                }
                /* Export the text from the MLE */
                exp = (ULONG)WinSendMsg(hwndEdit, MLM_EXPORT,
                                        MPFROMP(&insPoint), MPFROMP(&length));
                cmdBuf[exp] = '\n';
                cmdBuf[exp+1] = '\0';
#ifdef VERBOSE
                printf("cmdBuf [%s]\n", cmdBuf);
                fflush(stdout);
#endif
                if (cmdBuf[0] == '%' || cmdBuf[0] == '>') {
                    if (cmdBuf[1] == ' ') offset = 2;
                    else offset = 1;
                } else {
                    offset = 0;
                }
                cmd = Tcl_DStringAppend(&command, cmdBuf + offset, -1);
                DisplayString("", 1);
                if (Tcl_CommandComplete(cmd)) {
                    Tcl_Interp* interp = (Tcl_Interp*) WinQueryWindowULong(hwnd,
                                         QWL_USER);
                    Tcl_RecordAndEval(interp, cmd, 0);
                    Tcl_DStringFree(&command);
                    if (interp->result != NULL && *interp->result != '\0') {
                        DisplayString(interp->result, 1);
                    }
                    DisplayString("% ", 0);
                } else {
                    DisplayString("> ", 0);
                }
                break;
            }

            case MLN_TEXTOVERFLOW:
                /*
                 * Character(s) typed causing overflow, delete a whole block
                 * of text at the beginning so the next character won't cause
                 * this message again, or the amount of overflow (in param2)
                 * if that's larger.
                 * Return TRUE to signal that corrective action has been taken.
                 */
#ifdef VERBOSE
                printf("MLN_TEXTOVERFLOW %d\n", MAX(1024, LONGFROMMP(param2)));
                fflush(stdout);
#endif
                WinSendMsg(hwndEdit, MLM_DELETE,
                           (MPARAM) WinSendMsg(hwndEdit, MLM_CHARFROMLINE,
                                           (MPARAM)0, (MPARAM)0),
                           (MPARAM) MAX(1024, LONGFROMMP(param2)));
                return (MRESULT)1;

            case MLN_OVERFLOW: {
                /*
                 * Some action != typing character has caused overflow, delete
                 * the amount of overflow (in MLEOVERFLOW structure pointed to
                 * by param2) at the beginning if this is because of a paste.
                 * Return TRUE to signal that corrective action has been taken.
                 */
                POVERFLOW pOverflow = (POVERFLOW) PVOIDFROMMP(param2);
                if (pOverflow->afErrInd & MLFETL_TEXTBYTES) {
                    /*
                     * If the overflow is larger than the text limit, increase
                     * it to the overflow, so it will fit entirely. Otherwise,
                     * delete the first <overflow> bytes.
                     */
                    IPT firstPoint;
                    LONG limit = (LONG) WinSendMsg(hwndEdit, MLM_QUERYTEXTLIMIT,
                                            (MPARAM)0, (MPARAM)0);
#ifdef VERBOSE
                    printf("MLE text limit is %d\n", limit);
                    fflush(stdout);
#endif
                    /* limit is < 0 for unbounded, but then we can't overflow */
                    if (pOverflow->nBytesOver > limit) {
#ifdef VERBOSE
                        printf("Increasing MLE text limit by %d to %d\n",
                               pOverflow->nBytesOver,
                               pOverflow->nBytesOver + limit);
                        fflush(stdout);
#endif
                        WinSendMsg(hwndEdit, MLM_SETTEXTLIMIT,
                                   (MPARAM) pOverflow->nBytesOver + limit,
                                   (MPARAM)0);
                    }
#ifdef VERBOSE
                    printf("MLN_OVERFLOW %d\n",
                           MAX(1024, pOverflow->nBytesOver));
                    fflush(stdout);
#endif
                    firstPoint = (IPT) WinSendMsg(hwndEdit, MLM_CHARFROMLINE,
                                                  (MPARAM)0, (MPARAM)0);
                    firstPoint = 0;
                    WinSendMsg(hwndEdit, MLM_DELETE, (MPARAM)firstPoint,
                               (MPARAM) MAX(1024, pOverflow->nBytesOver));
                    insPoint = (IPT)WinSendMsg(hwndEdit, MLM_CHARFROMLINE,
                                          (MPARAM)WinSendMsg(hwndEdit,
                                                             MLM_QUERYLINECOUNT,
                                                             (MPARAM)0,
                                                             (MPARAM)0),
                                          (MPARAM)0);
                    insPoint += (int)WinSendMsg(hwndEdit, MLM_QUERYLINELENGTH,
                                                MPFROMLONG(insPoint), 0);
#ifdef VERBOSE
                    printf("lineCount %d\n", (long)WinSendMsg(hwndEdit,
                           MLM_QUERYLINECOUNT, (MPARAM)0, (MPARAM)0));
                    printf("firstPoint %d, insPoint %d\n", firstPoint,
                           insPoint);
                    fflush(stdout);
#endif
                } else {
                    /* What to do? */
#ifdef VERBOSE
                    printf("non-textlimit MLN_OVERFLOW %d\n",
                           pOverflow->nBytesOver);
                    fflush(stdout);
#endif
                    return (MRESULT)0;
                }
                return (MRESULT)1;
            }

            case MLN_MEMERROR:
                WinMessageBox(HWND_DESKTOP, hwnd,
                              "MLE says \"MLN_MEMERROR\"",
                              szAppName, 0, MB_OK | MB_ERROR | MB_APPLMODAL);
                return (MRESULT)0;
        }
    }
    break;

    case WM_CHAR: {
#ifdef VERBOSE
        USHORT flags= CHARMSG(&message)->fs;
        USHORT charcode= CHARMSG(&message)->chr;
        printf("WM_CHAR flags %x, charcode %d\n", flags, charcode);
        fflush(stdout);
#endif
        if ((CHARMSG(&message)->fs) & KC_CTRL &&
            (CHARMSG(&message)->chr) == 'c') {
            Tcl_Interp* interp = (Tcl_Interp*) WinQueryWindowULong(hwnd,
                                 QWL_USER);
            int length, exp;

            /*
             * Get line containing cursor.
             */

            /* Get line length */
            length = (int)WinSendMsg(hwndEdit, MLM_QUERYLINELENGTH,
                                     MPFROMLONG(insPoint), 0);
            /* Set export buffer */
            WinSendMsg(hwndEdit, MLM_SETIMPORTEXPORT, MPFROMP(cmdBuf),
                            MPFROMLONG(sizeof(cmdBuf)));
            /* Export the text from the MLE */
            exp = (ULONG)WinSendMsg(hwndEdit, MLM_EXPORT, MPFROMP(&insPoint),
                                    MPFROMP(&length));
            Tcl_DStringFree(&command);
            Tcl_Eval(interp, "break");
            DisplayString("", 1);
            DisplayString("% ", 0);
        }
        break;
    }

    case WM_SETFOCUS:
        WinSetFocus(HWND_DESKTOP, hwndEdit);
        return 0;
        
    case WM_SIZE:
        WinSetWindowPos(hwndEdit, HWND_TOP, 0, 0, SHORT1FROMMP(param2),
                        SHORT2FROMMP(param2), SWP_MOVE | SWP_SIZE);
        return 0;

    case WM_CLOSE:
        if (WinMessageBox(HWND_DESKTOP, hwnd, "Do you really want to exit?",
                          szAppName, 0, MB_YESNO|MB_ICONQUESTION|MB_APPLMODAL)
            == MBID_YES) {
            Tcl_Interp* interp= (Tcl_Interp*) WinQueryWindowULong(hwnd,
                                                                  QWL_USER);
#ifdef VERBOSE
            printf("WM_CLOSE, exiting, interp %x\n", (long)interp);
            fflush(stdout);
#endif
            /*
             * Rather than calling exit, invoke the "exit" command so that
             * users can replace "exit" with some other command to do additional
             * cleanup on exit.  The Tcl_Eval call should never return.
             */

            Tcl_Eval(interp, "exit 0");
        }
#ifdef VERBOSE
        printf("WM_CLOSE, not exiting\n");
        fflush(stdout);
#endif
        return 0 ;

    }
    return WinDefWindowProc(hwnd, message, param1, param2);
}

/*
 *----------------------------------------------------------------------
 *
 * EditProc --
 *
 *    Edit subclass window procedure.
 *
 * Results:
 *    The usual Window procedure values.
 *
 * Side effects:
 *    Allows user to edit commands.  Sends a double click event to
 *    the main window when the user presses enter.
 *
 *----------------------------------------------------------------------
 */

static MRESULT EXPENTRY
EditProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    if (message == WM_CHAR && SHORT1FROMMP(param1) & KC_CHAR &&
            ((USHORT)SHORT1FROMMP(param2) == '\r'
             || (USHORT)SHORT1FROMMP(param2) == '\n')) {
#ifdef VERBOSE
        printf("short1(param1) [%x], char3(param1) [%x], char4(param1) [%x]\n",
               SHORT1FROMMP(param1), CHAR3FROMMP(param1), CHAR4FROMMP(param1));
        printf("short1(param2) [%x], short2(param2) [%x]\n",
               SHORT1FROMMP(param2), SHORT2FROMMP(param2));
        fflush(stdout);
#endif
        WinSendMsg(WinQueryWindow(hwnd, QW_PARENT), WM_CONTROL,
                   MPFROM2SHORT(IDC_EDIT,MLN_USER), (MPARAM)hwnd);
        return 0 ;
    } else {
/*
#ifdef VERBOSE
        printf("Returning oldEditProc (%x, %x (%s), %x, %x)\n", hwnd,
               message, message == WM_CONTROL ? "WM_CONTROL" :
               (message == MLM_SETSEL ? "MLM_SETSEL" :
                (message == MLM_QUERYLINECOUNT ? "MLM_QUERYLINECOUNT" :
                 (message == MLM_CHARFROMLINE ? "MLM_CHARFROMLINE" :
                  (message == MLM_PASTE ? "MLM_PASTE" : "unknown")))),
               param1, param2);
        fflush(stdout);
#endif
*/
        return oldEditProc(hwnd, message, param1, param2);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TerminalPutsCmd --
 *
 *    Replacement for Tcl "puts" command that writes output to the
 *    terminal instead of stdout/stderr.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Same as Tcl_PutsCmd, except that it puts the output string
 *    into the terminal if the specified file is stdout.
 *
 *----------------------------------------------------------------------
 */

int
TerminalPutsCmd(clientData, interp, argc, argv)
    ClientData clientData;        /* Not used. */
    Tcl_Interp *interp;            /* Current interpreter. */
    int argc;                /* Number of arguments. */
    char **argv;            /* Argument strings. */
{
    Tcl_Channel chan;                   /* The channel to puts on. */
    int i;                              /* Counter. */
    int newline;                        /* Add a newline at end? */
    char *channelId;                    /* Name of channel for puts. */
    int result;                         /* Result of puts operation. */
    int mode;                           /* Mode in which channel is opened. */

#ifdef VERBOSE
    printf("TerminalPutsCmd ");
    fflush(stdout);
    for (i=0; i<argc; i++) {
       printf("[%s] ", argv[i]);
    }
    printf("\n");
    fflush(stdout);
#endif
    
    i = 1;
    newline = 1;
    if ((argc >= 2) && (strcmp(argv[1], "-nonewline") == 0)) {
        newline = 0;
        i++;
    }
    if ((i < (argc-3)) || (i >= argc)) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                " ?-nonewline? ?channelId? string\"", (char *) NULL);
        return TCL_ERROR;
    }

    /*
     * The code below provides backwards compatibility with an old
     * form of the command that is no longer recommended or documented.
     */

    if (i == (argc-3)) {
        if (strncmp(argv[i+2], "nonewline", strlen(argv[i+2])) != 0) {
            Tcl_AppendResult(interp, "bad argument \"", argv[i+2],
                "\": should be \"nonewline\"", (char *) NULL);
            return TCL_ERROR;
        }
        newline = 0;
    }
    if (i == (argc-1)) {
        /* Output on console terminal */
        DisplayString(argv[i], newline);
    } else if ( (stricmp(Tcl_GetVar(interp, "tcl_interactive", TCL_GLOBAL_ONLY),
                        "1") == 0 &&
              ( (stricmp(argv[i], "stdout") == 0 ||
                 stricmp(argv[i], "stderr") == 0))
        ) ) {
        i++;
        /* Output on console terminal */
        DisplayString(argv[i], newline);
    } else {
        /* Other channel specified, use standard (tclIOCmd) stuff */
        channelId = argv[i];
        i++;
        chan = Tcl_GetChannel(interp, channelId, &mode);
        if (chan == (Tcl_Channel) NULL) {
            return TCL_ERROR;
        }
        if ((mode & TCL_WRITABLE) == 0) {
            Tcl_AppendResult(interp, "channel \"", channelId,
                    "\" wasn't opened for writing", (char *) NULL);
            return TCL_ERROR;
        }

        result = Tcl_Write(chan, argv[i], -1);
        if (result < 0) {
            goto error;
        }
        if (newline != 0) {
            result = Tcl_Write(chan, "\n", 1);
            if (result < 0) {
                goto error;
            }
        }
    }

    return TCL_OK;

error:
    Tcl_AppendResult(interp, "error writing \"", Tcl_GetChannelName(chan),
            "\": ", Tcl_PosixError(interp), (char *) NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayString --
 *
 *    Insert a string into the console with an optional trailing
 *    newline.
 *      NOTE: the MLE control assumes the text to be in the clipboard as
 *      a single contiguous data segment, which restricts the amount to
 *      the maximum segment size (64K).
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Updates the MLE control.
 *
 *----------------------------------------------------------------------
 */

void
DisplayString(str, newline)
    char *str;
    int newline;
{
    char *p;
    const char *tmp;
    PVOID clipmem;
    ULONG size, lineCnt;
#ifdef VERBOSE
    printf("DisplayString (%s,%d) (%d) (insPoint %d)\n", str, newline,
           strlen(str), insPoint);
    fflush(stdout);
#endif

    tmp = str;
    for(lineCnt = 0; *tmp; tmp++) {
        if(*tmp == '\n') {
            lineCnt++;
        }
    }
    if (newline) {
        size  = strlen(str) + lineCnt + 3;
    } else {
        size  = strlen(str) + lineCnt + 1;
    }
    if ( (clipmem = ckalloc(size)) ) {
        for (p = (char *)clipmem; *str != '\0'; p++, str++) {
            if(*str == '\n') {
                *p++ = '\r';
            }
            *p = *str;
        }
        if (newline) {
            *p++ = '\r';
            *p++ = '\n';
        }
        *p = '\0';
#ifdef VERBOSE
        printf("    sending [%s] (size %d)\n", clipmem, size);
        fflush(stdout);
#endif
        WinSendMsg(hwndEdit, MLM_DISABLEREFRESH, (MPARAM)0, (MPARAM)0);
        if (WinSendMsg(hwndEdit, MLM_SETIMPORTEXPORT, MPFROMP(clipmem),
                       MPFROMLONG(size))) {
#ifdef VERBOSE
            ULONG imported;
            printf("before MLM_IMPORT, insPoint %d, size %d\n", insPoint,
                   size);
            fflush(stdout);
            imported = (ULONG)
#endif
            WinSendMsg(hwndEdit, MLM_IMPORT, MPFROMP(&insPoint),
                       MPFROMLONG(size));
#ifdef VERBOSE
            printf("MLM_IMPORT imported %d (insPoint %d, size %d)\n",
                   imported, insPoint, size);
            fflush(stdout);
#endif
        }
        WinSendMsg(hwndEdit, MLM_SETSEL, (MPARAM)insPoint,
                   (MPARAM)insPoint);
        WinSendMsg(hwndEdit, MLM_ENABLEREFRESH, (MPARAM)0, (MPARAM)0);
        ckfree((char *)clipmem);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleInit --
 *
 *	This function initializes the static variables for this file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a new event source.
 *
 *----------------------------------------------------------------------
 */

static ThreadSpecificData *
ConsoleInit()
{
    ThreadSpecificData *tsdPtr;
#ifdef VERBOSE
    printf("ConsoleInit\n");
    fflush(stdout);
#endif

    /*
     * Check the initialized flag first, then check again in the mutex.
     * This is a speed enhancement.
     */

    if (!initialized) {
	Tcl_MutexLock(&consoleMutex);
	if (!initialized) {
	    initialized = 1;
	    Tcl_CreateExitHandler(ProcExitHandler, NULL);
	}
	Tcl_MutexUnlock(&consoleMutex);
    }

    tsdPtr = (ThreadSpecificData *)TclThreadDataKeyGet(&dataKey);
    if (tsdPtr == NULL) {
	tsdPtr = TCL_TSD_INIT(&dataKey);
	tsdPtr->firstConsolePtr = NULL;
	Tcl_CreateEventSource(ConsoleSetupProc, ConsoleCheckProc, NULL);
	Tcl_CreateThreadExitHandler(ConsoleExitHandler, NULL);
    }
    return tsdPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleExitHandler --
 *
 *	This function is called to cleanup the console module before
 *	Tcl is unloaded.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the console event source.
 *
 *----------------------------------------------------------------------
 */

static void
ConsoleExitHandler(
    ClientData clientData)	/* Old window proc */
{
#ifdef VERBOSE
    printf("ConsoleExitHandler\n");
    fflush(stdout);
#endif
    Tcl_DeleteEventSource(ConsoleSetupProc, ConsoleCheckProc, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * ProcExitHandler --
 *
 *	This function is called to cleanup the process list before
 *	Tcl is unloaded.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resets the process list.
 *
 *----------------------------------------------------------------------
 */

static void
ProcExitHandler(
    ClientData clientData)	/* Old window proc */
{
#ifdef VERBOSE
    printf("ProcExitHandler\n");
    fflush(stdout);
#endif
    Tcl_MutexLock(&consoleMutex);
    initialized = 0;
    Tcl_MutexUnlock(&consoleMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleSetupProc --
 *
 *	This procedure is invoked before Tcl_DoOneEvent blocks waiting
 *	for an event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adjusts the block time if needed.
 *
 *----------------------------------------------------------------------
 */

void
ConsoleSetupProc(
    ClientData data,		/* Not used. */
    int flags)			/* Event flags as passed to Tcl_DoOneEvent. */
{
    ConsoleInfo *infoPtr;
    Tcl_Time blockTime = { 0, 0 };
    int block = 1;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

#ifdef VERBOSE
    printf("ConsoleSetupProc\n");
    fflush(stdout);
#endif
    if (!(flags & TCL_FILE_EVENTS)) {
	return;
    }
    
    /*
     * Look to see if any events are already pending.  If they are, poll.
     */

    for (infoPtr = tsdPtr->firstConsolePtr; infoPtr != NULL; 
	    infoPtr = infoPtr->nextPtr) {
	if (infoPtr->watchMask & TCL_WRITABLE) {
	    if (DosWaitEventSem(infoPtr->writable, SEM_IMMEDIATE_RETURN)
                != ERROR_TIMEOUT) {
		block = 0;
	    }
	}
	if (infoPtr->watchMask & TCL_READABLE) {
	    if (WaitForRead(infoPtr, 0) >= 0) {
		block = 0;
	    }
	}
    }
    if (!block) {
	Tcl_SetMaxBlockTime(&blockTime);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleCheckProc --
 *
 *	This procedure is called by Tcl_DoOneEvent to check the console
 *	event source for events. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May queue an event.
 *
 *----------------------------------------------------------------------
 */

static void
ConsoleCheckProc(
    ClientData data,		/* Not used. */
    int flags)			/* Event flags as passed to Tcl_DoOneEvent. */
{
    ConsoleInfo *infoPtr;
    ConsoleEvent *evPtr;
    int needEvent;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

#ifdef VERBOSE
    printf("ConsoleCheckProc\n");
    fflush(stdout);
#endif
    if (!(flags & TCL_FILE_EVENTS)) {
	return;
    }
    
    /*
     * Queue events for any ready consoles that don't already have events
     * queued.
     */

    for (infoPtr = tsdPtr->firstConsolePtr; infoPtr != NULL; 
	    infoPtr = infoPtr->nextPtr) {
	if (infoPtr->flags & CONSOLE_PENDING) {
	    continue;
	}
	
	/*
	 * Queue an event if the console is signaled for reading or writing.
	 */

	needEvent = 0;
	if (infoPtr->watchMask & TCL_WRITABLE) {
	    if (DosWaitEventSem(infoPtr->writable, SEM_IMMEDIATE_RETURN)
                != ERROR_TIMEOUT) {
		needEvent = 1;
	    }
	}
	
	if (infoPtr->watchMask & TCL_READABLE) {
	    if (WaitForRead(infoPtr, 0) >= 0) {
		needEvent = 1;
	    }
	}

	if (needEvent) {
	    infoPtr->flags |= CONSOLE_PENDING;
	    evPtr = (ConsoleEvent *) ckalloc(sizeof(ConsoleEvent));
	    evPtr->header.proc = ConsoleEventProc;
	    evPtr->infoPtr = infoPtr;
	    Tcl_QueueEvent((Tcl_Event *) evPtr, TCL_QUEUE_TAIL);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ConsoleBlockModeProc --
 *
 *	Set blocking or non-blocking mode on channel.
 *
 * Results:
 *	0 if successful, errno when failed.
 *
 * Side effects:
 *	Sets the device into blocking or non-blocking mode.
 *
 *----------------------------------------------------------------------
 */

static int
ConsoleBlockModeProc(
    ClientData instanceData,	/* Instance data for channel. */
    int mode)			/* TCL_MODE_BLOCKING or
                                 * TCL_MODE_NONBLOCKING. */
{
    ConsoleInfo *infoPtr = (ConsoleInfo *) instanceData;
#ifdef VERBOSE
    printf("ConsoleBlockModeProc\n");
    fflush(stdout);
#endif
    
    /*
     * Consoles on OS/2 can not be switched between blocking and nonblocking,
     * hence we have to emulate the behavior. This is done in the input
     * function by checking against a bit in the state. We set or unset the
     * bit here to cause the input function to emulate the correct behavior.
     */

    if (mode == TCL_MODE_NONBLOCKING) {
	infoPtr->flags |= CONSOLE_ASYNC;
    } else {
	infoPtr->flags &= ~(CONSOLE_ASYNC);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleCloseProc --
 *
 *	Closes a console based IO channel.
 *
 * Results:
 *	0 on success, errno otherwise.
 *
 * Side effects:
 *	Closes the physical channel.
 *
 *----------------------------------------------------------------------
 */

static int
ConsoleCloseProc(
    ClientData instanceData,	/* Pointer to ConsoleInfo structure. */
    Tcl_Interp *interp)		/* For error reporting. */
{
    ConsoleInfo *consolePtr = (ConsoleInfo *) instanceData;
    int errorCode;
    ConsoleInfo *infoPtr, **nextPtrPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
#ifdef VERBOSE
    printf("ConsoleCloseProc\n");
    fflush(stdout);
#endif

    errorCode = 0;
    
    /*
     * Clean up the background thread if necessary.  Note that this
     * must be done before we can close the file, since the 
     * thread may be blocking trying to read from the console.
     */
    
    if (consolePtr->readThread) {
	/*
	 * Forcibly terminate the background thread.  We cannot rely on the
	 * thread to cleanly terminate itself because we have no way of
	 * closing the handle without blocking in the case where the
	 * thread is in the middle of an I/O operation.  Note that we need
	 * to guard against terminating the thread while it is in the
	 * middle of Tcl_ThreadAlert because it won't be able to release
	 * the notifier lock.
	 */

	Tcl_MutexLock(&consoleMutex);
	DosKillThread(consolePtr->readThread);
	Tcl_MutexUnlock(&consoleMutex);

	/*
	 * Wait for the thread to terminate.  This ensures that we are
	 * completely cleaned up before we leave this function. 
	 */

	DosWaitThread(&consolePtr->readThread, DCWW_WAIT);
	DosCloseEventSem(consolePtr->readable);
	DosCloseEventSem(consolePtr->startReader);
	consolePtr->readThread = (TID)NULL;
    }
    consolePtr->validMask &= ~TCL_READABLE;

    /*
     * Wait for the writer thread to finish the current buffer, then
     * terminate the thread and close the handles.  If the channel is
     * nonblocking, there should be no pending write operations.
     */
    
    if (consolePtr->writeThread) {
        if (consolePtr->toWrite) {
	    DosWaitEventSem(consolePtr->writable, SEM_INDEFINITE_WAIT);
        }

	/*
	 * Forcibly terminate the background thread.  We cannot rely on the
	 * thread to cleanly terminate itself because we have no way of
	 * closing the handle without blocking in the case where the
	 * thread is in the middle of an I/O operation.  Note that we need
	 * to guard against terminating the thread while it is in the
	 * middle of Tcl_ThreadAlert because it won't be able to release
	 * the notifier lock.
	 */

	Tcl_MutexLock(&consoleMutex);
	DosKillThread(consolePtr->writeThread);
	Tcl_MutexUnlock(&consoleMutex);

	/*
	 * Wait for the thread to terminate.  This ensures that we are
	 * completely cleaned up before we leave this function. 
	 */

	DosWaitThread(&consolePtr->writeThread, DCWW_WAIT);
	DosCloseEventSem(consolePtr->writable);
	DosCloseEventSem(consolePtr->startWriter);
	consolePtr->writeThread = (TID)NULL;
    }
    consolePtr->validMask &= ~TCL_WRITABLE;


    /*
     * Don't close the OS/2 handle if the handle is a standard channel
     * during the exit process.  Otherwise, one thread may kill the stdio
     * of another.
     */

    if (!TclInExit() || ((consolePtr->handle != HF_STDIN)
		         && (consolePtr->handle != HF_STDOUT)
		         && (consolePtr->handle != HF_STDERR))) {
	rc = DosClose(consolePtr->handle);
	if (rc != NO_ERROR) {
	    TclOS2ConvertError(rc);
	    errorCode = errno;
	}
    }
    
    consolePtr->watchMask &= consolePtr->validMask;

    /*
     * Remove the file from the list of watched files.
     */

    for (nextPtrPtr = &(tsdPtr->firstConsolePtr), infoPtr = *nextPtrPtr;
	    infoPtr != NULL;
	    nextPtrPtr = &infoPtr->nextPtr, infoPtr = *nextPtrPtr) {
	if (infoPtr == (ConsoleInfo *)consolePtr) {
	    *nextPtrPtr = infoPtr->nextPtr;
	    break;
	}
    }
    if (consolePtr->writeBuf != NULL) {
	ckfree(consolePtr->writeBuf);
	consolePtr->writeBuf = 0;
    }
    ckfree((char*) consolePtr);

    return errorCode;
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleInputProc --
 *
 *	Reads input from the IO channel into the buffer given. Returns
 *	count of how many bytes were actually read, and an error indication.
 *
 * Results:
 *	A count of how many bytes were read is returned and an error
 *	indication is returned in an output argument.
 *
 * Side effects:
 *	Reads input from the actual channel.
 *
 *----------------------------------------------------------------------
 */

static int
ConsoleInputProc(
    ClientData instanceData,		/* Console state. */
    char *buf,				/* Where to store data read. */
    int bufSize,			/* How much space is available
                                         * in the buffer? */
    int *errorCode)			/* Where to store error code. */
{
    ConsoleInfo *infoPtr = (ConsoleInfo *) instanceData;
    ULONG count, bytesRead = 0;
    int result;
#ifdef VERBOSE
    printf("ConsoleInputProc\n");
    fflush(stdout);
#endif

    *errorCode = 0;

    /*
     * Synchronize with the reader thread.
     */
    
    result = WaitForRead(infoPtr, (infoPtr->flags & CONSOLE_ASYNC) ? 0 : 1);
    
    /*
     * If an error occurred, return immediately.
     */
    
    if (result == -1) {
	*errorCode = errno;
	return -1;
    }

    if (infoPtr->readFlags & CONSOLE_BUFFERED) {
	/*
	 * Data is stored in the buffer.
	 */

	if (bufSize < (infoPtr->bytesRead - infoPtr->offset)) {
	    memcpy(buf, &infoPtr->buffer[infoPtr->offset], bufSize);
	    bytesRead = bufSize;
	    infoPtr->offset += bufSize;
	} else {
	    memcpy(buf, &infoPtr->buffer[infoPtr->offset], bufSize);
	    bytesRead = infoPtr->bytesRead - infoPtr->offset;

	    /*
	     * Reset the buffer
	     */
	    
	    infoPtr->readFlags &= ~CONSOLE_BUFFERED;
	    infoPtr->offset = 0;
	}

	return bytesRead;
    }
    
    /*
     * Attempt to read bufSize bytes.  The read will return immediately
     * if there is any data available.  Otherwise it will block until
     * at least one byte is available or an EOF occurs.
     */

    rc = DosRead(infoPtr->handle, (PVOID) buf, (ULONG) bufSize, &count);
    if (rc == NO_ERROR) {
	buf[count] = '\0';
	return count;
    }
    TclOS2ConvertError(rc);
    *errorCode = errno;

    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleOutputProc --
 *
 *	Writes the given output on the IO channel. Returns count of how
 *	many characters were actually written, and an error indication.
 *
 * Results:
 *	A count of how many characters were written is returned and an
 *	error indication is returned in an output argument.
 *
 * Side effects:
 *	Writes output on the actual channel.
 *
 *----------------------------------------------------------------------
 */

static int
ConsoleOutputProc(
    ClientData instanceData,		/* Console state. */
    char *buf,				/* The data buffer. */
    int toWrite,			/* How many bytes to write? */
    int *errorCode)			/* Where to store error code. */
{
    ConsoleInfo *infoPtr = (ConsoleInfo *) instanceData;
    ULONG bytesWritten, timeout;
    APIRET rc;
#ifdef VERBOSE
    printf("ConsoleOutputProc\n");
    fflush(stdout);
#endif
    
    *errorCode = 0;
    timeout = (infoPtr->flags & CONSOLE_ASYNC) ? 0 : SEM_INDEFINITE_WAIT;
    if (DosWaitEventSem(infoPtr->writable, timeout) == ERROR_TIMEOUT) {
	/*
	 * The writer thread is blocked waiting for a write to complete
	 * and the channel is in non-blocking mode.
	 */

	errno = EAGAIN;
	goto error;
    }
    
    /*
     * Check for a background error on the last write.
     */

    if (infoPtr->writeError) {
	TclOS2ConvertError(infoPtr->writeError);
	infoPtr->writeError = 0;
	goto error;
    }

    if (infoPtr->flags & CONSOLE_ASYNC) {
        ULONG posted;
	/*
	 * The console is non-blocking, so copy the data into the output
	 * buffer and restart the writer thread.
	 */

	if (toWrite > infoPtr->writeBufLen) {
	    /*
	     * Reallocate the buffer to be large enough to hold the data.
	     */

	    if (infoPtr->writeBuf) {
		ckfree(infoPtr->writeBuf);
	    }
	    infoPtr->writeBufLen = toWrite;
	    infoPtr->writeBuf = ckalloc(toWrite);
	}
	memcpy(infoPtr->writeBuf, buf, toWrite);
	infoPtr->toWrite = toWrite;
	DosResetEventSem(infoPtr->writable, &posted);
	rc = DosPostEventSem(infoPtr->startWriter);
#ifdef VERBOSE
        printf("DosPostEventSem startWriter 0x%x returns %d\n",
               infoPtr->startWriter, rc);
        fflush(stdout);
#endif
	bytesWritten = toWrite;
    } else {
	/*
	 * In the blocking case, just try to write the buffer directly.
	 * This avoids an unnecessary copy.
	 */

        rc = DosWrite(infoPtr->handle, (PVOID) buf, (ULONG) toWrite,
                      &bytesWritten);
	if (rc != NO_ERROR) {
	    TclOS2ConvertError(rc);
	    goto error;
	}
    }
    return bytesWritten;

    error:
    *errorCode = errno;
    return -1;

}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleEventProc --
 *
 *	This function is invoked by Tcl_ServiceEvent when a file event
 *	reaches the front of the event queue.  This procedure invokes
 *	Tcl_NotifyChannel on the console.
 *
 * Results:
 *	Returns 1 if the event was handled, meaning it should be removed
 *	from the queue.  Returns 0 if the event was not handled, meaning
 *	it should stay on the queue.  The only time the event isn't
 *	handled is if the TCL_FILE_EVENTS flag bit isn't set.
 *
 * Side effects:
 *	Whatever the notifier callback does.
 *
 *----------------------------------------------------------------------
 */

static int
ConsoleEventProc(
    Tcl_Event *evPtr,		/* Event to service. */
    int flags)			/* Flags that indicate what events to
				 * handle, such as TCL_FILE_EVENTS. */
{
    ConsoleEvent *consoleEvPtr = (ConsoleEvent *)evPtr;
    ConsoleInfo *infoPtr;
    int mask;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
#ifdef VERBOSE
    printf("ConsoleEventProc\n");
    fflush(stdout);
#endif

    if (!(flags & TCL_FILE_EVENTS)) {
	return 0;
    }

    /*
     * Search through the list of watched consoles for the one whose handle
     * matches the event.  We do this rather than simply dereferencing
     * the handle in the event so that consoles can be deleted while the
     * event is in the queue.
     */

    for (infoPtr = tsdPtr->firstConsolePtr; infoPtr != NULL;
	    infoPtr = infoPtr->nextPtr) {
	if (consoleEvPtr->infoPtr == infoPtr) {
	    infoPtr->flags &= ~(CONSOLE_PENDING);
	    break;
	}
    }

    /*
     * Remove stale events.
     */

    if (!infoPtr) {
	return 1;
    }

    /*
     * Check to see if the console is readable.  Note
     * that we can't tell if a console is writable, so we always report it
     * as being writable unless we have detected EOF.
     */

    mask = 0;
    if (infoPtr->watchMask & TCL_WRITABLE) {
	if (DosWaitEventSem(infoPtr->writable, SEM_IMMEDIATE_RETURN)
            != ERROR_TIMEOUT) {
	  mask = TCL_WRITABLE;
	}
    }

    if (infoPtr->watchMask & TCL_READABLE) {
	if (WaitForRead(infoPtr, 0) >= 0) {
	    if (infoPtr->readFlags & CONSOLE_EOF) {
		mask = TCL_READABLE;
	    } else {
		mask |= TCL_READABLE;
	    }
	} 
    }

    /*
     * Inform the channel of the events.
     */

    Tcl_NotifyChannel(infoPtr->channel, infoPtr->watchMask & mask);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleWatchProc --
 *
 *	Called by the notifier to set up to watch for events on this
 *	channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
ConsoleWatchProc(
    ClientData instanceData,		/* Console state. */
    int mask)				/* What events to watch for, OR-ed
                                         * combination of TCL_READABLE,
                                         * TCL_WRITABLE and TCL_EXCEPTION. */
{
    ConsoleInfo **nextPtrPtr, *ptr;
    ConsoleInfo *infoPtr = (ConsoleInfo *) instanceData;
    int oldMask = infoPtr->watchMask;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
#ifdef VERBOSE
    printf("ConsoleWatchProc\n");
    fflush(stdout);
#endif

    /*
     * Since most of the work is handled by the background threads,
     * we just need to update the watchMask and then force the notifier
     * to poll once. 
     */

    infoPtr->watchMask = mask & infoPtr->validMask;
    if (infoPtr->watchMask) {
	Tcl_Time blockTime = { 0, 0 };
	if (!oldMask) {
	    infoPtr->nextPtr = tsdPtr->firstConsolePtr;
	    tsdPtr->firstConsolePtr = infoPtr;
	}
	Tcl_SetMaxBlockTime(&blockTime);
    } else {
	if (oldMask) {
	    /*
	     * Remove the console from the list of watched consoles.
	     */

	    for (nextPtrPtr = &(tsdPtr->firstConsolePtr), ptr = *nextPtrPtr;
		 ptr != NULL;
		 nextPtrPtr = &ptr->nextPtr, ptr = *nextPtrPtr) {
		if (infoPtr == ptr) {
		    *nextPtrPtr = ptr->nextPtr;
		    break;
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleGetHandleProc --
 *
 *	Called from Tcl_GetChannelHandle to retrieve OS handles from
 *	inside a command consoleline based channel.
 *
 * Results:
 *	Returns TCL_OK with the fd in handlePtr, or TCL_ERROR if
 *	there is no handle for the specified direction. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ConsoleGetHandleProc(
    ClientData instanceData,	/* The console state. */
    int direction,		/* TCL_READABLE or TCL_WRITABLE */
    ClientData *handlePtr)	/* Where to store the handle.  */
{
    ConsoleInfo *infoPtr = (ConsoleInfo *) instanceData;
#ifdef VERBOSE
    printf("ConsoleGetHandleProc\n");
    fflush(stdout);
#endif

    *handlePtr = (ClientData) infoPtr->handle;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WaitForRead --
 *
 *	Wait until some data is available, the console is at
 *	EOF or the reader thread is blocked waiting for data (if the
 *	channel is in non-blocking mode).
 *
 * Results:
 *	Returns 1 if console is readable.  Returns 0 if there is no data
 *	on the console, but there is buffered data.  Returns -1 if an
 *	error occurred.  If an error occurred, the threads may not
 *	be synchronized.
 *
 * Side effects:
 *	Updates the shared state flags.  If no error occurred,
 *      the reader thread is blocked waiting for a signal from the 
 *      main thread.
 *
 *----------------------------------------------------------------------
 */

static int
WaitForRead(
    ConsoleInfo *infoPtr,		/* Console state. */
    int blocking)		/* Indicates whether call should be
				 * blocking or not. */
{
    ULONG timeout, posted;
    APIRET rc;
#ifdef VERBOSE
    printf("WaitForRead readable 0x%x, startReader 0x%x\n",
           infoPtr->readable, infoPtr->startReader);
    fflush(stdout);
#endif
    
    while (1) {
	/*
	 * Synchronize with the reader thread.
	 */
       
	timeout = blocking ? SEM_INDEFINITE_WAIT : SEM_IMMEDIATE_RETURN;
	rc = DosWaitEventSem(infoPtr->readable, timeout);
	if (rc == ERROR_TIMEOUT) {
	    /*
	     * The reader thread is blocked waiting for data and the channel
	     * is in non-blocking mode.
	     */
#ifdef VERBOSE
            printf("DosWaitEventSem readable 0x%x %s timed out\n", 
                   infoPtr->readable, timeout == SEM_INDEFINITE_WAIT ?
                   "SEM_INDEFINITE_WAIT" : "SEM_IMMEDIATE_RETURN");
            fflush(stdout);
#endif
	    errno = EAGAIN;
	    return -1;
#ifdef VERBOSE
        } else {
            printf("DosWaitEventSem readable 0x%x %s returned %d\n", 
                   infoPtr->readable, timeout == SEM_INDEFINITE_WAIT ?
                   "SEM_INDEFINITE_WAIT" : "SEM_IMMEDIATE_RETURN", rc);
            fflush(stdout);
#endif
	}
	
	/*
	 * At this point, the two threads are synchronized, so it is safe
	 * to access shared state.
	 */
	
	/*
	 * If the console has hit EOF, it is always readable.
	 */
	
	if (infoPtr->readFlags & CONSOLE_EOF) {
#ifdef VERBOSE
            printf("readFlags & CONSOLE_EOF => WaitForRead returns 1\n");
            fflush(stdout);
#endif
	    return 1;
	}
	
	/*
	 * If there is data in the buffer, the console must be
	 * readable (since it is a line-oriented device).
	 */

	if (infoPtr->readFlags & CONSOLE_BUFFERED) {
#ifdef VERBOSE
            printf("readFlags & CONSOLE_BUFFERED => WaitForRead returns 1\n");
            fflush(stdout);
#endif
	    return 1;
	}

	
	/*
	 * There wasn't any data available, so reset the thread and
	 * try again.
	 */
    
	rc = DosResetEventSem(infoPtr->readable, &posted);
#ifdef VERBOSE
        printf("DosResetEventSem readable 0x%x returns %d, posted %d\n",
               infoPtr->readable, rc, posted);
        fflush(stdout);
#endif
	rc = DosPostEventSem(infoPtr->startReader);
#ifdef VERBOSE
        printf("DosPostEventSem startReader 0x%x returns %d\n",
               infoPtr->startReader, rc);
        fflush(stdout);
#endif
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleReaderThread --
 *
 *	This function runs in a separate thread and waits for input
 *	to become available on a console.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Signals the main thread when input become available.  May
 *	cause the main thread to wake up by posting a message.  May
 *	one line from the console for each wait operation.
 *
 *----------------------------------------------------------------------
 */

static void
ConsoleReaderThread(void *arg)
{
    APIRET rc;
    ConsoleInfo *infoPtr = (ConsoleInfo *)arg;
    ULONG count;

    /* Other threads than the creator have to open the event semaphores */
    rc = DosOpenEventSem(NULL, &infoPtr->readable);
    rc = DosOpenEventSem(NULL, &infoPtr->startReader);

    for (;;) {
	/*
	 * Wait for the main thread to signal before attempting to wait.
	 */

#ifdef VERBOSE
        printf("ConsoleReaderThread: startReader 0x%x readable 0x%x\n",
               infoPtr->startReader, infoPtr->readable);
        fflush(stdout);
#endif
        DosWaitEventSem(infoPtr->startReader, SEM_INDEFINITE_WAIT);
	DosResetEventSem(infoPtr->startReader, &count);
#ifdef VERBOSE
        printf("ConsoleReaderThread: done startReader 0x%x readable 0x%x\n",
               infoPtr->startReader, infoPtr->readable);
        fflush(stdout);
#endif

	/* 
	 * Look for data on the console, but first ignore any events
	 * that are not KEY_EVENTs 
	 */
        rc = DosRead(infoPtr->handle, infoPtr->buffer, CONSOLE_BUFFER_SIZE,
                     &infoPtr->bytesRead);
        if (rc == NO_ERROR) {
            if (infoPtr->bytesRead == 0) {
                /* Read from end of file */
		infoPtr->readFlags = CONSOLE_EOF;
            } else {
	        /*
	         * Data was stored in the buffer.
	         */
	    
	        infoPtr->readFlags |= CONSOLE_BUFFERED;
	    }
	}

	/*
	 * Signal the main thread by signalling the readable event and
	 * then waking up the notifier thread.
	 */

	rc = DosPostEventSem(infoPtr->readable);
#ifdef VERBOSE
        printf("ConsoleReaderThr: DosPostEventSem readable 0x%x returns %d\n",
               infoPtr->readable, rc);
#endif

	/*
	 * Alert the foreground thread.  Note that we need to treat this like
	 * a critical section so the foreground thread does not terminate
	 * this thread while we are holding a mutex in the notifier code.
	 */

	Tcl_MutexLock(&consoleMutex);
	Tcl_ThreadAlert(infoPtr->threadId);
	Tcl_MutexUnlock(&consoleMutex);
    }
    return;			/* NOT REACHED */
}

/*
 *----------------------------------------------------------------------
 *
 * ConsoleWriterThread --
 *
 *	This function runs in a separate thread and writes data
 *	onto a console.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Signals the main thread when an output operation is completed.
 *	May cause the main thread to wake up by posting a message.  
 *
 *----------------------------------------------------------------------
 */

static void
ConsoleWriterThread(void *arg)
{

    ConsoleInfo *infoPtr = (ConsoleInfo *)arg;
    ULONG count, toWrite;
    char *buf;
    APIRET rc;

    /* Other threads than the creator have to open the event semaphores */
    rc = DosOpenEventSem(NULL, &infoPtr->writable);
    rc = DosOpenEventSem(NULL, &infoPtr->startWriter);

    for (;;) {
	/*
	 * Wait for the main thread to signal before attempting to write.
	 */

#ifdef VERBOSE
        printf("ConsoleWriterThread: startWriter 0x%x writable 0x%x\n",
               infoPtr->startWriter, infoPtr->writable);
        fflush(stdout);
#endif
        DosWaitEventSem(infoPtr->startWriter, SEM_INDEFINITE_WAIT);
	DosResetEventSem(infoPtr->startWriter, &count);
#ifdef VERBOSE
        printf("ConsoleWriterThread: done startWriter 0x%x writable 0x%x\n",
               infoPtr->startWriter, infoPtr->writable);
        fflush(stdout);
#endif

	buf = infoPtr->writeBuf;
	toWrite = infoPtr->toWrite;

	/*
	 * Loop until all of the bytes are written or an error occurs.
	 */

	while (toWrite > 0) {
            rc = DosWrite(infoPtr->handle, (PVOID)buf, (ULONG) toWrite, &count);
            if (rc != NO_ERROR) {
		infoPtr->writeError = rc;
		break;
	    } else {
		toWrite -= count;
		buf += count;
	    }
	}

	/*
	 * Signal the main thread by signalling the writable event and
	 * then waking up the notifier thread.
	 */
	
	rc = DosPostEventSem(infoPtr->writable);
#ifdef VERBOSE
        printf("ConsoleWriterThr: DosPostEventSem writable 0x%x returns %d\n",
               infoPtr->writable, rc);
#endif

	/*
	 * Alert the foreground thread.  Note that we need to treat this like
	 * a critical section so the foreground thread does not terminate
	 * this thread while we are holding a mutex in the notifier code.
	 */

	Tcl_MutexLock(&consoleMutex);
	Tcl_ThreadAlert(infoPtr->threadId);
	Tcl_MutexUnlock(&consoleMutex);
    }
    return;			/* NOT REACHED */
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2OpenConsoleChannel --
 *
 *	Constructs a Console channel for the specified standard OS handle.
 *      This is a helper function to break up the construction of 
 *      channels into File, Console, or Serial.
 *
 * Results:
 *	Returns the new channel, or NULL.
 *
 * Side effects:
 *	May open the channel
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
TclOS2OpenConsoleChannel(handle, channelName, permissions)
    HFILE handle;
    char *channelName;
    int permissions;
{
    APIRET rc;
    char encoding[4 + TCL_INTEGER_SPACE];
    ConsoleInfo *infoPtr;
    ThreadSpecificData *tsdPtr;
    ULONG codePage, nrCodePages;
#ifdef VERBOSE
    printf("TclOS2OpenConsoleChannel handle %d\n", handle);
    fflush(stdout);
#endif

    tsdPtr = ConsoleInit();

    /*
     * See if a channel with this handle already exists.
     */
    
    infoPtr = (ConsoleInfo *) ckalloc((unsigned) sizeof(ConsoleInfo));
    memset(infoPtr, 0, sizeof(ConsoleInfo));

    infoPtr->validMask = permissions;
    infoPtr->handle = handle;

    rc = DosQueryCp(sizeof(codePage), &codePage, &nrCodePages);
#ifdef VERBOSE
    printf("DosQueryCp returns %d, codePage %d, nrCodePages %d\n", rc, codePage,
           nrCodePages);
    fflush(stdout);
#endif
    sprintf(encoding, "cp%d", (rc == NO_ERROR || rc == ERROR_CPLIST_TOO_SMALL)
                              ? (int) codePage : 850);
    
    /*
     * Use the pointer for the name of the result channel.
     * This keeps the channel names unique, since some may share
     * handles (stdin/stdout/stderr for instance).
     */

    sprintf(channelName, "file%d", (int) handle);
    
    infoPtr->channel = Tcl_CreateChannel(&consoleChannelType, channelName,
            (ClientData) infoPtr, permissions);

    infoPtr->threadId = Tcl_GetCurrentThread();

    if (permissions & TCL_READABLE) {
        /* manual reset, initially signalled */
        rc = DosCreateEventSem(NULL, &infoPtr->readable, 0L, TRUE);
        /* automatic reset, initially nonsignalled */
        rc = DosCreateEventSem(NULL, &infoPtr->startReader, 0L, FALSE);
        infoPtr->readThread = _beginthread(ConsoleReaderThread, NULL, 32768,
                                           (void *)infoPtr);
        /*
         * The Windows port sets the priority for the new thread to the
         * highest possible value. I don't think I agree...
        rc = DosSetPriority(PRTYS_THREAD, PRTYC_NOCHANGE, PRTYD_MAXIMUM,
                            infoPtr->readThread);
         */
    }

    if (permissions & TCL_WRITABLE) {
        /* manual reset, initially signalled */
        rc = DosCreateEventSem(NULL, &infoPtr->writable, 0L, TRUE);
        /* automatic reset, initially nonsignalled */
        rc = DosCreateEventSem(NULL, &infoPtr->startWriter, 0L, FALSE);
        infoPtr->writeThread = _beginthread(ConsoleWriterThread, NULL, 32768,
                                            (void *)infoPtr);
    }

    /*
     * Files have default translation of AUTO and ^Z eof char, which
     * means that a ^Z will be accepted as EOF when reading.
     */
    
    Tcl_SetChannelOption(NULL, infoPtr->channel, "-translation", "auto");
    Tcl_SetChannelOption(NULL, infoPtr->channel, "-eofchar", "\032 {}");
    Tcl_SetChannelOption(NULL, infoPtr->channel, "-encoding", encoding);

    return infoPtr->channel;
}

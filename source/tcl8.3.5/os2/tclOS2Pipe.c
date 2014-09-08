/* 
 * tclOS2Pipe.c -- This file implements the OS/2-specific pipeline exec
 *                 functions.
 *      
 *
 * Copyright (c) 1996 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tclOS2Int.h"

#define HF_STDIN  0
#define HF_STDOUT 1
#define HF_STDERR 2

/*
 * The pipeMutex locks around access to the initialized and procList variables,
 * and it is used to protect background threads from being terminated while
 * they are using APIs that hold locks.
 */

TCL_DECLARE_MUTEX(pipeMutex)

/*
 * The following defines identify the various types of applications that
 * run under OS/2.  There is special case code for the various types.
 */

#define APPL_NONE       0
#define APPL_DOS        1
#define APPL_WIN3X      2
#define APPL_WIN32      3
#define APPL_OS2WIN     4
#define APPL_OS2FS      5
#define APPL_OS2PM      6
#define APPL_OS2CMD     7

/*
 * The following constants and structures are used to encapsulate the state
 * of various types of files used in a pipeline.
 */

#define OS2_TMPFILE	1           /* OS/2 emulated temporary file. */
#define OS2_FILE	2           /* Basic OS/2 file. */
#define OS2_FIRST_TYPE	OS2_TMPFILE
#define OS2_LAST_TYPE	OS2_FILE

/*
 * This structure encapsulates the common state associated with all file
 * types used in a pipeline.
 */

typedef struct OS2File {
    int type;                   /* One of the file types defined above. */
    HFILE handle;               /* Open file handle. */
} OS2File;

/*
 * The following structure is used to keep track of temporary files
 * and delete the disk file when the open handle is closed.
 * The type field will be OS2_TMPFILE.
 */

typedef struct TmpFile {
    OS2File file;               /* Common part. */
    char *name;                 /* Name of temp file. */
} TmpFile;

/*
 * Bit masks used in the flags field of the PipeInfo structure below.
 */

#define PIPE_PENDING    (1<<0)  /* Message is pending in the queue. */
#define PIPE_ASYNC      (1<<1)  /* Channel is non-blocking. */

/*
 * Bit masks used in the sharedFlags field of the PipeInfo structure below.
 */

#define PIPE_EOF        (1<<2)  /* Pipe has reached EOF. */
#define PIPE_EXTRABYTE  (1<<3)  /* The reader thread has consumed one byte. */

/*
 * This structure describes per-instance data for a pipe based channel.
 */

typedef struct PipeInfo {
    struct PipeInfo *nextPtr;   /* Pointer to next registered pipe. */
    Tcl_Channel channel;        /* Pointer to channel structure. */
    int validMask;              /* OR'ed combination of TCL_READABLE,
                                 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
                                 * which operations are valid on the file. */
    int watchMask;              /* OR'ed combination of TCL_READABLE,
                                 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
                                 * which events should be reported. */
    int flags;                  /* State flags, see above for a list. */
    TclFile readFile;           /* Output from pipe. */
    TclFile writeFile;          /* Input from pipe. */
    TclFile errorFile;          /* Error output from pipe. */
    int numPids;                /* Number of processes attached to pipe. */
    Tcl_Pid *pidPtr;            /* Pids of attached processes. */
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
                                 * to write to the pipe. */
    HEV startReader;            /* Auto-reset event used by the main thread to
                                 * signal when the reader thread should attempt
                                 * to read from the pipe. */
    HEV stopReader;             /* Manual-reset event used to alert the reader
                                 * thread to fall-out and exit */
    HMUX readerMuxWaitSem;      /* Multiple Wait Semaphore for waiting on either
                                 * startReader or stopReader in
                                 * PipeReaderThread */
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
    char extraByte;             /* Buffer for extra character consumed by
                                 * reader thread.  This byte is shared with
                                 * the reader thread so access must be
                                 * synchronized with the readable object. */
} PipeInfo;

typedef struct ThreadSpecificData {
    /*
     * The following pointer refers to the head of the list of pipes
     * that are being watched for file events.
     */

    PipeInfo *firstPipePtr;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

/*
 * The following structure is what is added to the Tcl event queue when
 * pipe events are generated.
 */

typedef struct PipeEvent {
    Tcl_Event header;           /* Information that is standard for
                                 * all events. */
    PipeInfo *infoPtr;          /* Pointer to pipe info structure.  Note
                                 * that we still have to verify that the
                                 * pipe exists before dereferencing this
                                 * pointer. */
} PipeEvent;

/*
 * Declarations for functions used only in this file.
 */
static int            ApplicationType(Tcl_Interp *interp, const char *fileName,
                          char *fullName);
static int            PipeBlockModeProc(ClientData instanceData, int mode);
static void           PipeCheckProc (ClientData clientData, int flags);
static int            PipeClose2Proc(ClientData instanceData,
                          Tcl_Interp *interp, int flags);
static int            PipeEventProc(Tcl_Event *evPtr, int flags);
static void           PipeExitHandler(ClientData clientData);
static int            PipeGetHandleProc(ClientData instanceData, int direction,
                          ClientData *handlePtr);
static void           PipeInit(void);
static int            PipeInputProc(ClientData instanceData, char *buf,
                          int toRead, int *errorCode);
static int            PipeOutputProc(ClientData instanceData, char *buf,
                          int toWrite, int *errorCode);
static void           PipeReaderThread(void *arg);
static void           PipeSetupProc _ANSI_ARGS_((ClientData clientData,
                          int flags));
static void           PipeWatchProc(ClientData instanceData, int mask);
static void           PipeWriterThread(void *arg);
static void           TempFileCleanup _ANSI_ARGS_((ClientData clientData));
static int            WaitForRead(PipeInfo *infoPtr, int blocking);

/*
 * This structure describes the channel type structure for command pipe
 * based IO.
 */

static Tcl_ChannelType pipeChannelType = {
    "pipe",                     /* Type name. */
    TCL_CHANNEL_VERSION_2,      /* v2 channel */
    TCL_CLOSE2PROC,             /* Close proc. */
    PipeInputProc,              /* Input proc. */
    PipeOutputProc,             /* Output proc. */
    NULL,                       /* Seek proc. */
    NULL,                       /* Set option proc. */
    NULL,                       /* Get option proc. */
    PipeWatchProc,              /* Set up notifier to watch the channel. */
    PipeGetHandleProc,          /* Get an OS handle from channel. */
    PipeClose2Proc,             /* close2proc */
    PipeBlockModeProc,          /* Set blocking or non-blocking mode.*/
    NULL,                       /* flush proc. */
    NULL,                       /* handler proc. */
};
/*
 *----------------------------------------------------------------------
 *
 * PipeInit --
 *
 *      This function initializes the static variables for this file.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates a new event source.
 *
 *----------------------------------------------------------------------
 */

static void
PipeInit()
{
    ThreadSpecificData *tsdPtr;
#ifdef VERBOSE
    printf("PipeInit\n");
    fflush(stdout);
#endif

    tsdPtr = (ThreadSpecificData *)TclThreadDataKeyGet(&dataKey);
    if (tsdPtr == NULL) {
        tsdPtr = TCL_TSD_INIT(&dataKey);
        tsdPtr->firstPipePtr = NULL;
        Tcl_CreateEventSource(PipeSetupProc, PipeCheckProc, NULL);
        Tcl_CreateThreadExitHandler(PipeExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PipeExitHandler --
 *
 *      This function is called to cleanup the pipe module before
 *      Tcl is unloaded.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes the pipe event source.
 *
 *----------------------------------------------------------------------
 */

static void
PipeExitHandler(
    ClientData clientData)      /* Old window proc */
{
#ifdef VERBOSE
    printf("PipeExitHandler\n");
    fflush(stdout);
#endif

    Tcl_DeleteEventSource(PipeSetupProc, PipeCheckProc, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * PipeSetupProc --
 *
 *      This procedure is invoked before Tcl_DoOneEvent blocks waiting
 *      for an event.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Adjusts the block time if needed.
 *
 *----------------------------------------------------------------------
 */

void
PipeSetupProc(
    ClientData data,            /* Not used. */
    int flags)                  /* Event flags as passed to Tcl_DoOneEvent. */
{
    PipeInfo *infoPtr;
    Tcl_Time blockTime = { 0, 0 };
    int block = 1;
    OS2File *filePtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

#ifdef VERBOSE
    printf("PipeSetupProc\n");
    fflush(stdout);
#endif

    if (!(flags & TCL_FILE_EVENTS)) {
        return;
    }

    /*
     * Look to see if any events are already pending.  If they are, poll.
     */

    for (infoPtr = tsdPtr->firstPipePtr; infoPtr != NULL;
            infoPtr = infoPtr->nextPtr) {
        if (infoPtr->watchMask & TCL_WRITABLE) {
            filePtr = (OS2File*) infoPtr->writeFile;
            if (DosWaitEventSem(infoPtr->writable, 0) != ERROR_TIMEOUT) {
                block = 0;
            }
        }
        if (infoPtr->watchMask & TCL_READABLE) {
            filePtr = (OS2File*) infoPtr->readFile;
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
 * PipeCheckProc --
 *
 *      This procedure is called by Tcl_DoOneEvent to check the pipe
 *      event source for events.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May queue an event.
 *
 *----------------------------------------------------------------------
 */

static void
PipeCheckProc(
    ClientData data,            /* Not used. */
    int flags)                  /* Event flags as passed to Tcl_DoOneEvent. */
{
    PipeInfo *infoPtr;
    PipeEvent *evPtr;
    OS2File *filePtr;
    int needEvent;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

#ifdef VERBOSE
    printf("PipeCheckProc\n");
    fflush(stdout);
#endif

    if (!(flags & TCL_FILE_EVENTS)) {
        return;
    }

    /*
     * Queue events for any ready pipes that don't already have events
     * queued.
     */

    for (infoPtr = tsdPtr->firstPipePtr; infoPtr != NULL;
            infoPtr = infoPtr->nextPtr) {
        if (infoPtr->flags & PIPE_PENDING) {
            continue;
        }

        /*
         * Queue an event if the pipe is signaled for reading or writing.
         */

        needEvent = 0;
        filePtr = (OS2File*) infoPtr->writeFile;
        if ((infoPtr->watchMask & TCL_WRITABLE) &&
            (DosWaitEventSem(infoPtr->writable, 0) != ERROR_TIMEOUT)) {
            needEvent = 1;
        }

        filePtr = (OS2File*) infoPtr->readFile;
        if ((infoPtr->watchMask & TCL_READABLE) &&
                (WaitForRead(infoPtr, 0) >= 0)) {
            needEvent = 1;
        }

        if (needEvent) {
            infoPtr->flags |= PIPE_PENDING;
            evPtr = (PipeEvent *) ckalloc(sizeof(PipeEvent));
            evPtr->header.proc = PipeEventProc;
            evPtr->infoPtr = infoPtr;
            Tcl_QueueEvent((Tcl_Event *) evPtr, TCL_QUEUE_TAIL);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2MakeFile --
 *
 *      This function constructs a new TclFile from a given data and
 *      type value.
 *
 * Results:
 *      Returns a newly allocated OS2File as a TclFile.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

TclFile
TclOS2MakeFile(
    HFILE handle)	/* Type-specific data. */
{
    OS2File *filePtr;

#ifdef VERBOSE
    printf("TclOS2MakeFile handle [%d]\n", handle);
    fflush(stdout);
#endif

    filePtr = (OS2File *) ckalloc(sizeof(OS2File));
    if (filePtr != (OS2File *)NULL) {
        filePtr->type = OS2_FILE;
        filePtr->handle = handle;
    }

    return (TclFile)filePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpMakeFile --
 *
 *      Make a TclFile from a channel.
 *
 * Results:
 *      Returns a new TclFile or NULL on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

TclFile
TclpMakeFile(
    Tcl_Channel channel,        /* Channel to get file from. */
    int direction)              /* Either TCL_READABLE or TCL_WRITABLE. */
{
    HFILE handle;

#ifdef VERBOSE
    printf("TclpMakeFile\n");
    fflush(stdout);
#endif

    if (Tcl_GetChannelHandle(channel, direction,
            (ClientData *) &handle) == TCL_OK) {
        return TclOS2MakeFile(handle);
    } else {
        return (TclFile) NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpOpenFile --
 *
 *      This function opens files for use in a pipeline.
 *
 * Results:
 *      Returns a newly allocated TclFile structure containing the
 *      file handle.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

TclFile
TclpOpenFile(
    CONST char *path,           /* The name of the file to open. */
    int mode)                   /* In what mode to open the file? */
{
    APIRET rc;
    HFILE handle;
    ULONG accessMode, createMode, flags, exist, result;
    Tcl_DString ds;
    char *nativePath;

#ifdef VERBOSE
    printf("TclpOpenFile\n");
    fflush(stdout);
#endif

    /*
     * Map the access bits to the OS/2 access mode.
     */

    switch (mode & (O_RDONLY | O_WRONLY | O_RDWR)) {
        case O_RDONLY:
           accessMode = OPEN_ACCESS_READONLY;
           break;
       case O_WRONLY:
           accessMode = OPEN_ACCESS_WRITEONLY;
           break;
       case O_RDWR:
           accessMode = OPEN_ACCESS_READWRITE;
           break;
       default:
           TclOS2ConvertError(ERROR_INVALID_DATA);
           return NULL;
    }

    /*
     * Map the creation flags to the OS/2 open mode.
     */

    switch (mode & (O_CREAT | O_EXCL | O_TRUNC)) {
        case (O_CREAT | O_EXCL | O_TRUNC):
            createMode = OPEN_ACTION_CREATE_IF_NEW |
                         OPEN_ACTION_FAIL_IF_EXISTS;
            break;
        case (O_CREAT | O_EXCL):
            createMode = OPEN_ACTION_CREATE_IF_NEW |
                         OPEN_ACTION_FAIL_IF_EXISTS;
            break;
        case (O_CREAT | O_TRUNC):
            createMode = OPEN_ACTION_CREATE_IF_NEW |
                         OPEN_ACTION_REPLACE_IF_EXISTS;
            break;
        case O_CREAT:
            createMode = OPEN_ACTION_CREATE_IF_NEW |
                         OPEN_ACTION_OPEN_IF_EXISTS;
            break;
        case O_TRUNC:
        case (O_TRUNC | O_EXCL):
            createMode = OPEN_ACTION_FAIL_IF_NEW |
                         OPEN_ACTION_REPLACE_IF_EXISTS;
            break;
        default:
            createMode = OPEN_ACTION_FAIL_IF_NEW |
                         OPEN_ACTION_OPEN_IF_EXISTS;
    }

    nativePath = Tcl_UtfToExternalDString(NULL, path, -1, &ds);

    /*
     * If the file is not being created, use the existing file attributes.
     */

    flags = 0;
    if (!(mode & O_CREAT)) {
        FILESTATUS3 infoBuf;

        rc = DosQueryPathInfo(nativePath, FIL_STANDARD, &infoBuf,
                              sizeof(infoBuf));
        if (rc == NO_ERROR) {
            flags = infoBuf.attrFile;
        } else {
            flags = 0;
        }
    }


    /*
     * Set up the attributes so this file is not inherited by child processes.
     */

    accessMode |= OPEN_FLAGS_NOINHERIT;

    /*
     * Set up the file sharing mode.  We want to allow simultaneous access.
     */

    accessMode |= OPEN_SHARE_DENYNONE;

    /*
     * Now we get to create the file.
     */

    rc = DosOpen(nativePath, &handle, &exist, 0, flags, createMode, accessMode,
                 (PEAOP2)NULL);
    Tcl_DStringFree(&ds);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles++;
    printf("TclOpenFile: DosOpen [%s] returns [%d]\n", path, rc);
    fflush(stdout);
#endif
    if (rc != NO_ERROR) {
        ULONG err = 0;

        switch (rc) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                err = ERROR_FILE_NOT_FOUND;
                break;
            case ERROR_ACCESS_DENIED:
            case ERROR_INVALID_ACCESS:
            case ERROR_SHARING_VIOLATION:
            case ERROR_CANNOT_MAKE:
                err = (mode & O_CREAT) ? ERROR_FILE_EXISTS
                                       : ERROR_FILE_NOT_FOUND;
                break;
        }
        TclOS2ConvertError(err);
        return NULL;
    }

    /*
     * Seek to the end of file if we are writing.
     */

    if (mode & O_WRONLY) {
        rc = DosSetFilePtr(handle, 0, FILE_END, &result);
    }

    return TclOS2MakeFile(handle);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpCreateTempFile --
 *
 *      This function opens a unique file with the property that it
 *      will be deleted when Tcl exits.  The temporary
 *      file is created in the system temporary directory.
 *
 * Results:
 *      Returns a valid TclFile, or NULL on failure.
 *
 * Side effects:
 *      Creates a new temporary file.
 *
 *----------------------------------------------------------------------
 */

TclFile
TclpCreateTempFile(
    CONST char *contents)       /* String to write into temp file, or NULL. */
{
    APIRET rc = NO_ERROR;
    char *name;
    CONST char *native;
    Tcl_DString dstring;
    HFILE handle = NULLHANDLE;
    ULONG action, length, result;
    TmpFile *tmpFilePtr = NULL;

#ifdef VERBOSE
    APIRET rc2;
    PPIB processInfoBlockPtr;
    PTIB threadInfoBlockPtr;
    printf("TclpCreateTempFile\n");
    fflush(stdout);
#endif


    /*
     * tempnam allocates space for name with *malloc*
     * use free() when deleting the file
     * First argument is directory that is used when the directory in the
     * TMP environment variable doesn't exist or the variable isn't set.
     * Make sure we can "always" create a temp file => c:\ .
     */
    name = tempnam("C:\\", "Tcl");
    if (name == (char *)NULL) {
        goto error;
    }

    tmpFilePtr = (TmpFile *) ckalloc(sizeof(TmpFile));
    /*
     * See if we can get memory for later before creating the file to minimize
     * system interaction
     */
    if (tmpFilePtr == (TmpFile *)NULL) {
        /* We couldn't allocate memory, so free the tempname memory and abort */
        free((char *)name);
	goto error;
    }

    rc = DosOpen(name, &handle, &action, 0, FILE_NORMAL,
                 OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS,
                 OPEN_SHARE_DENYNONE | OPEN_ACCESS_READWRITE, NULL);

#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles++;
    rc2 = DosGetInfoBlocks(&threadInfoBlockPtr, &processInfoBlockPtr);
    printf("TclpCreateTempFile %x DosOpen [%s] h %d rc %d p %x t %d\n",
           tmpFilePtr, name, handle, rc, processInfoBlockPtr->pib_ulpid,
           threadInfoBlockPtr->tib_ptib2->tib2_ultid);
    fflush(stdout);
#endif
    if (rc != NO_ERROR) {
        goto error;
    }

    /*
     * Write the file out, doing line translations on the way.
     */

    if (contents != NULL) {
        CONST char *p;

        /*
         * Convert the contents from UTF to native encoding
         */
        native = Tcl_UtfToExternalDString(NULL, contents, -1, &dstring);

        for (p = native; *p != '\0'; p++) {
            if (*p == '\n') {
                length = p - native;
                if (length > 0) {
                    rc = DosWrite(handle, (PVOID)native, length, &result);
#ifdef VERBOSE
                    printf("DosWrite handle %d [%s] returned [%d]\n",
                           handle, contents, rc);
                    fflush(stdout);
#endif
                    if (rc != NO_ERROR) {
                        goto error;
                    }
                }
                if (DosWrite(handle, "\r\n", 2, &result) != NO_ERROR) {
                    goto error;
                }
                native = p+1;
            }
        }
        length = p - native;
        if (length > 0) {
            rc = DosWrite(handle, (PVOID)native, length, &result);
#ifdef VERBOSE
            printf("DosWrite handle %d [%s] returned [%d]\n",
                   handle, contents, rc);
            fflush(stdout);
#endif
            if (rc != NO_ERROR) {
                goto error;
            }
        }
        Tcl_DStringFree(&dstring);
    }

    rc = DosSetFilePtr(handle, 0, FILE_BEGIN, &result);
#ifdef VERBOSE
    printf("TclpCreateTempFile DosSetFilePtr BEGIN handle [%d]: %d\n",
           handle, rc);
    fflush(stdout);
#endif
    if (rc != NO_ERROR) {
        goto error;
    }

    /*
     * A file won't be deleted when it is closed, so we have to do it ourselves.
     * Therefore, we can't use the standard TclOS2MakeFile, that would not
     * make it an OS2_TMPFILE.
     */

    tmpFilePtr->file.type = OS2_TMPFILE;
    tmpFilePtr->file.handle = handle;
    tmpFilePtr->name = name;
    /* Queue undeleted files for removal on exiting Tcl */
    Tcl_CreateExitHandler(TempFileCleanup, (ClientData)tmpFilePtr);

    return (TclFile)tmpFilePtr;

  error:
    /* Free the native representation of the contents if necessary */
    if (contents != NULL) {
        Tcl_DStringFree(&dstring);
    }
    TclOS2ConvertError(rc);
    if (handle != NULLHANDLE) {
        rc = DosClose(handle);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles--;
#endif
    }
    if (name != (char *)NULL) {
        rc = DosDelete(name);
#ifdef VERBOSE
        printf("DosDelete [%s] was handle [%d] returns %d\n", name, handle,
               rc);
        fflush(stdout);
#endif
        /* NB! EMX has allocated name with malloc, use free! */
        free((char *)name);
    }
    if (tmpFilePtr != NULL) {
#ifdef VERBOSE
        printf("ckfreeing tmpFilePtr %x\n", tmpFilePtr);
        fflush(stdout);
#endif
        ckfree((char *)tmpFilePtr);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpCreatePipe --
 *
 *      Creates an anonymous pipe.
 *
 * Results:
 *      Returns 1 on success, 0 on failure. 
 *
 * Side effects:
 *      Creates a pipe.
 *
 *----------------------------------------------------------------------
 */

int
TclpCreatePipe(readPipe, writePipe)
    TclFile *readPipe;         /* Location to store file handle for
                                * read side of pipe. */
    TclFile *writePipe;        /* Location to store file handle for
                                * write side of pipe. */
{
    APIRET rc;
    HFILE readHandle, writeHandle;

#ifdef VERBOSE
    printf("TclpCreatePipe\n");
    fflush(stdout);
#endif

    /*
     * Using 1024 makes for processes hanging around until their output was
     * read, which doesn't always happen (eg. the "defs" file in the test
     * suite). The Control Program Reference gives 4096 in an example, which
     * "happens" to be the page size of the Intel x86.
     */
    rc = DosCreatePipe(&readHandle, &writeHandle, 4096);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles += 2;
    printf("DosCreatePipe returned [%d], read [%d], write [%d]\n",
           rc, readHandle, writeHandle);
    fflush(stdout);
#endif

    if (rc == NO_ERROR) {
        *readPipe = TclOS2MakeFile(readHandle);
        *writePipe = TclOS2MakeFile(writeHandle);
        return 1;
    }

    TclOS2ConvertError(rc);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpCloseFile --
 *
 *      Closes a pipeline file handle.  These handles are created by
 *      TclpOpenFile, TclpCreatePipe, or TclpMakeFile.
 *
 * Results:
 *      0 on success, -1 on failure.
 *
 * Side effects:
 *      The file is closed and deallocated.
 *
 *----------------------------------------------------------------------
 */

int
TclpCloseFile(
    TclFile file)       /* The file to close. */
{
    APIRET rc;
    OS2File *filePtr = (OS2File *) file;

#ifdef VERBOSE
    printf("TclpCloseFile [%d] type %d\n", filePtr->handle, filePtr->type);
    fflush(stdout);
#endif
    if (filePtr->type < OS2_FIRST_TYPE || filePtr->type > OS2_LAST_TYPE) {
        panic("Tcl_CloseFile: unexpected file type");
    }

    /*
     * Do not close standard channels while in thread-exit to prevent one
     * thread killing the stdio of another.
     */

    if (!TclInExit()
        || ((filePtr->handle != 0)
            && (filePtr->handle != 1)
            && (filePtr->handle != 2))) {
        if (filePtr->handle != NULLHANDLE) {
            rc = DosClose(filePtr->handle);
#ifdef VERBOSE
            if (rc == NO_ERROR) openedFiles--;
            printf("TclpCloseFile DosClose [%d] returns %d\n", filePtr->handle,
                   rc);
            fflush(stdout);
#endif
            if (rc != NO_ERROR) {
                TclOS2ConvertError(rc);
#ifdef VERBOSE
                printf("TclpCloseFile ckfreeing 1 filePtr %x\n", filePtr);
                fflush(stdout);
#endif
                /* If this is an OS2_TMPFILE, we have to remove it too */
                if (filePtr->type == OS2_TMPFILE) {
                    rc = DosDelete((PSZ)((TmpFile*)filePtr)->name);
                    if (rc == NO_ERROR) {
#ifdef VERBOSE
                        printf("    DosDelete OK\n");
                        fflush(stdout);
#endif
                        if (((TmpFile*)filePtr)->name != NULL) {
                            /* Watch it! name was *malloc*ed by tempnam */
                            free((char *)((TmpFile*)filePtr)->name);
                        }
                        ckfree((char *)filePtr);
                        /* Succesful deletion, remove exithandler */
                        Tcl_DeleteExitHandler(TempFileCleanup,
                                              (ClientData)filePtr);
#ifdef VERBOSE
                    } else {
                        /* Not succesful, keep the exit handler, don't free */
                        printf("    DosDelete ERROR %d\n", rc);
                        fflush(stdout);
#endif
                    }
                } else {
                    ckfree((char *) filePtr);
                }
                return -1;
            }
        }
    }

#ifdef VERBOSE
    printf("TclpCloseFile ckfreeing 2 filePtr %x\n", filePtr);
    fflush(stdout);
#endif
    /* If this is an OS2_TMPFILE, we have to remove the exit handler */
    if (filePtr->type == OS2_TMPFILE) {
        rc = DosDelete((PSZ)((TmpFile*)filePtr)->name);
        if (rc == NO_ERROR) {
#ifdef VERBOSE
            printf("    DosDelete OK\n");
            fflush(stdout);
#endif
            if (((TmpFile*)filePtr)->name != NULL) {
                /* Watch it! name was *malloc*ed by tempnam */
                free((char *)((TmpFile*)filePtr)->name);
            }
            ckfree((char *)filePtr);
            /* Succesful deletion, remove exithandler */
            Tcl_DeleteExitHandler(TempFileCleanup, (ClientData)filePtr);
#ifdef VERBOSE
        } else {
            /* Not succesful, keep the exit handler, don't free filePtr */
            printf("    DosDelete ERROR %d\n", rc);
            fflush(stdout);
#endif
        }
    } else {
        ckfree((char *) filePtr);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpCreateProcess --
 *
 *      Create a child process that has the specified files as its
 *      standard input, output, and error.  The child process runs
 *      asynchronously and runs with the same environment variables
 *      as the creating process.
 *
 *      The complete OS/2 search path is searched to find the specified
 *      executable.  If an executable by the given name is not found,
 *      automatically tries appending ".com", ".exe", and ".bat" to the
 *      executable name.
 *
 * Results:
 *      The return value is TCL_ERROR and an error message is left in
 *      the interp's result if there was a problem creating the child
 *      process.  Otherwise, the return value is TCL_OK and *pidPtr is
 *      filled with the process id of the child process.
 *
 * Side effects:
 *      A process is created.
 *
 *----------------------------------------------------------------------
 */

int
TclpCreateProcess(
    Tcl_Interp *interp,         /* Interpreter in which to leave errors that
                                 * occurred when creating the child process.
                                 * Error messages from the child process
                                 * itself are sent to errorFile. */
    int argc,                   /* Number of arguments in following array. */
    char **argv,                /* Array of argument strings.  argv[0]
                                 * contains the name of the executable
                                 * converted to native format (using the
                                 * Tcl_TranslateFileName call).  Additional
                                 * arguments have not been converted. */
    TclFile inputFile,          /* If non-NULL, gives the file to use as
                                 * input for the child process.  If inputFile
                                 * file is not readable or is NULL, the child
                                 * will receive no standard input. */
    TclFile outputFile,         /* If non-NULL, gives the file that
                                 * receives output from the child process.  If
                                 * outputFile file is not writeable or is
                                 * NULL, output from the child will be
                                 * discarded. */
    TclFile errorFile,          /* If non-NULL, gives the file that
                                 * receives errors from the child process.  If
                                 * errorFile file is not writeable or is NULL,
                                 * errors from the child will be discarded.
                                 * errorFile may be the same as outputFile. */
    Tcl_Pid *pidPtr)            /* If this procedure is successful, pidPtr
                                 * is filled with the process id of the child
                                 * process. */
{
    APIRET rc;
    int result, applType, nextArg, count, mode;
    HFILE inputHandle, outputHandle, errorHandle;
    HFILE stdIn = HF_STDIN, stdOut = HF_STDOUT, stdErr = HF_STDERR;
    HFILE orgIn = NEW_HANDLE, orgOut = NEW_HANDLE, orgErr = NEW_HANDLE;
    BOOL stdinChanged, stdoutChanged, stderrChanged;
    char execPath[MAX_PATH];
    OS2File *filePtr;
    ULONG action;
    char *arguments[256];

#ifdef VERBOSE
    int remember[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    printf("TclpCreateProcess\n");
    fflush(stdout);
#endif

    PipeInit();

    applType = ApplicationType(interp, argv[0], execPath);
#ifdef VERBOSE
    printf("argv[0] %s, execPath %s: applType %d\n", argv[0], execPath,
           applType);
    fflush(stdout);
#endif
    if (applType == APPL_NONE) {
        return TCL_ERROR;
    }

    result = TCL_ERROR;

#ifdef VERBOSE
    printf("1 opened files %d\n", openedFiles);;
    fflush(stdout);
#endif
    /* Backup original stdin, stdout, stderr by Dup-ing to new handle */
    rc = DosDupHandle(stdIn, &orgIn);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles++;
    printf("2 opened files %d\n", openedFiles);;
    printf("DosDupHandle stdIn %d returns %d orgIn %d\n", stdIn, rc, orgIn);
    fflush(stdout);
#endif
    rc = DosDupHandle(stdOut, &orgOut);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles++;
    printf("3 opened files %d\n", openedFiles);;
    printf("DosDupHandle stdOut %d returns %d, orgOut %d\n", stdOut, rc,
           orgOut);
    fflush(stdout);
#endif
    rc = DosDupHandle(stdErr, &orgErr);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles++;
    printf("4 opened files %d\n", openedFiles);;
    printf("DosDupHandle stdErr %d returns %d orgErr %d\n", stdErr, rc,
           orgErr);
    fflush(stdout);
#endif

    /*
     * We have to check the type of each file, since we cannot duplicate
     * some file types.
     */

    inputHandle = NEW_HANDLE;
    if (inputFile != NULL) {
        filePtr = (OS2File *)inputFile;
        if (filePtr->type >= OS2_FIRST_TYPE || filePtr->type <= OS2_LAST_TYPE) {
            inputHandle = filePtr->handle;
        }
    }
    outputHandle = NEW_HANDLE;
    if (outputFile != NULL) {
        filePtr = (OS2File *)outputFile;
        if (filePtr->type >= OS2_FIRST_TYPE || filePtr->type <= OS2_LAST_TYPE) {
            outputHandle = filePtr->handle;
        }
    }
    errorHandle = NEW_HANDLE;
    if (errorFile != NULL) {
        filePtr = (OS2File *)errorFile;
        if (filePtr->type >= OS2_FIRST_TYPE || filePtr->type <= OS2_LAST_TYPE) {
            errorHandle = filePtr->handle;
        }
    }
#ifdef VERBOSE
    printf("    inputHandle [%d]\n", inputHandle);
    printf("    outputHandle [%d]\n", outputHandle);
    printf("    errorHandle [%d]\n", errorHandle);
    fflush(stdout);
#endif

    /*
     * Duplicate all the handles which will be passed off as stdin, stdout
     * and stderr of the child process. The duplicate handles are set to
     * be inheritable, so the child process can use them.
     */

    stdinChanged = stdoutChanged = stderrChanged = FALSE;
    if (inputHandle == NEW_HANDLE) {
        /*
         * If handle was not set, open NUL as input.
         */
#ifdef VERBOSE
        printf("Opening NUL as input\n");
        fflush(stdout);
#endif
        rc = DosOpen((PSZ)"NUL", &inputHandle, &action, 0, FILE_NORMAL,
                     OPEN_ACTION_CREATE_IF_NEW,
                     OPEN_SHARE_DENYNONE | OPEN_ACCESS_READONLY, NULL);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles++;
        printf("5 opened files %d\n", openedFiles);;
        fflush(stdout);
#endif
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            Tcl_AppendResult(interp, "couldn't open NUL as input handle: ",
                    Tcl_PosixError(interp), (char *) NULL);
#ifdef VERBOSE
            printf("1 goto end\n");;
            fflush(stdout);
#endif
            goto end;
        }
    }
    if (inputHandle != stdIn) {
        /* Duplicate to standard input handle */
        rc = DosDupHandle(inputHandle, &stdIn);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles++;
        printf("6 opened files %d\n", openedFiles);;
        printf("DosDupHandle inputHandle [%d] returned [%d], handle [%d]\n",
               inputHandle, rc, stdIn);
        fflush(stdout);
#endif
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            Tcl_AppendResult(interp, "couldn't duplicate input handle: ",
                    Tcl_PosixError(interp), (char *) NULL);
#ifdef VERBOSE
            printf("2 goto end\n");;
    fflush(stdout);
#endif
            goto end;
        }
        stdinChanged = TRUE;
    }

    if (outputHandle == NEW_HANDLE) {
        /*
         * If handle was not set, open NUL as output.
         */
#ifdef VERBOSE
        printf("Opening NUL as output\n");
        fflush(stdout);
#endif
        rc = DosOpen((PSZ)"NUL", &outputHandle, &action, 0, FILE_NORMAL,
                     OPEN_ACTION_CREATE_IF_NEW,
                     OPEN_SHARE_DENYNONE | OPEN_ACCESS_WRITEONLY, NULL);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles++;
        printf("7 opened files %d\n", openedFiles);;
        fflush(stdout);
#endif
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            Tcl_AppendResult(interp, "couldn't open NUL as output handle: ",
                    Tcl_PosixError(interp), (char *) NULL);
#ifdef VERBOSE
            printf("3 goto end\n");;
            fflush(stdout);
#endif
            goto end;
        }
    }
    if (outputHandle != stdOut) {
        /* Duplicate to standard output handle */
        rc = DosDupHandle(outputHandle, &stdOut);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles++;
        printf("8 opened files %d\n", openedFiles);;
        printf("DosDupHandle outputHandle [%d] returned [%d], handle [%d]\n",
               outputHandle, rc, stdOut);
        fflush(stdout);
#endif
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            Tcl_AppendResult(interp, "couldn't duplicate output handle: ",
                    Tcl_PosixError(interp), (char *) NULL);
#ifdef VERBOSE
            printf("4 goto end\n");;
            fflush(stdout);
#endif
            goto end;
        }
        stdoutChanged = TRUE;
    }

    if (errorHandle == NEW_HANDLE) {
        /*
         * If handle was not set, open NUL as output.
         */
#ifdef VERBOSE
        printf("Opening NUL as error\n");
        fflush(stdout);
#endif
        rc = DosOpen((PSZ)"NUL", &errorHandle, &action, 0, FILE_NORMAL,
                     OPEN_ACTION_CREATE_IF_NEW,
                     OPEN_SHARE_DENYNONE | OPEN_ACCESS_WRITEONLY, NULL);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles++;
        printf("9 opened files %d\n", openedFiles);;
        fflush(stdout);
#endif
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            Tcl_AppendResult(interp, "couldn't open NUL as error handle: ",
                    Tcl_PosixError(interp), (char *) NULL);
#ifdef VERBOSE
            printf("5 goto end\n");;
            fflush(stdout);
#endif
            goto end;
        }
    }
    if (errorHandle != stdErr) {
        /* Duplicate to standard error handle */
        rc = DosDupHandle(errorHandle, &stdErr);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles++;
        printf("10 opened files %d\n", openedFiles);;
        printf("DosDupHandle errorHandle [%d] returned [%d], handle [%d]\n",
               errorHandle, rc, stdErr);
        fflush(stdout);
#endif
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            Tcl_AppendResult(interp, "couldn't duplicate error handle: ",
                    Tcl_PosixError(interp), (char *) NULL);
#ifdef VERBOSE
            printf("6 goto end\n");;
            fflush(stdout);
#endif
            goto end;
        }
        stderrChanged = TRUE;
    }

    /*
     * EMX's spawnv handles all the nitty-gritty DosStartSession stuff (like
     * session/no-session, building environment and arguments with/without
     * quoting etc.) for us, so we'll just use that and keep ourselves to
     * it's arguments like P_WAIT, P_PM, ....
     */

    /*
     * Run DOS (incl. .bat) and .cmd files via cmd.exe.
     */

    mode = P_SESSION;
    nextArg = 0;

    switch (applType) {
    case APPL_NONE:
    case APPL_DOS:
    case APPL_OS2CMD:
    case APPL_WIN3X:
        arguments[0] = "cmd.exe";
        arguments[1] = "/c";
        nextArg = 2;
        mode |= P_DEFAULT | P_MINIMIZE | P_BACKGROUND;
        break;
    case APPL_OS2WIN:
        mode |= P_DEFAULT | P_MINIMIZE | P_BACKGROUND;
        break;
    case APPL_OS2FS:
        mode |= P_FULLSCREEN | P_BACKGROUND;
        break;
    case APPL_OS2PM:
        if (TclOS2GetUsePm()) {
            mode = P_PM | P_BACKGROUND;
        } else {
            mode |= P_PM | P_BACKGROUND;
        }
        break;
    default:
        mode |= P_DEFAULT | P_MINIMIZE | P_BACKGROUND;
    }
    for (count = 0; count < argc && nextArg < 256; count++) {
        arguments[nextArg] = argv[count];
        nextArg++;
    }
    arguments[nextArg] = '\0';

    *pidPtr = (Tcl_Pid) spawnv(mode,
                               arguments[0], arguments);
    if (*pidPtr == (Tcl_Pid) -1) {
        TclOS2ConvertError(rc);
        Tcl_AppendResult(interp, "couldn't execute \"", arguments[0],
                "\": ", Tcl_PosixError(interp), (char *) NULL);
#ifdef VERBOSE
            printf("1 goto end\n");;
    fflush(stdout);
#endif
        goto end;
    }
#ifdef VERBOSE
    printf("spawned pid %d\n", *pidPtr);
    fflush(stdout);
#endif

    result = TCL_OK;


    end:

    /* Restore original stdin, stdout, stderr by Dup-ing from new handle */
    stdIn = HF_STDIN; stdOut = HF_STDOUT; stdErr = HF_STDERR;
#ifdef VERBOSE
    remember[0] = stdinChanged;
    remember[1] = stdoutChanged;
    remember[2] = stderrChanged;
#endif
    if (stdinChanged) {
        rc = DosClose(stdIn);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles--;
        printf("11 opened files %d\n", openedFiles);;
        remember[3] = rc;
    fflush(stdout);
#endif
        rc = DosDupHandle(orgIn, &stdIn);
#ifdef VERBOSE
        remember[4] = rc;
#endif
    }
    rc = DosClose(orgIn);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles--;
    printf("12 opened files %d\n", openedFiles);;
    remember[5] = rc;
    fflush(stdout);
#endif

    if (stdoutChanged) {
        rc = DosClose(stdOut);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles--;
        printf("13 opened files %d\n", openedFiles);;
        remember[6] = rc;
    fflush(stdout);
#endif
        rc = DosDupHandle(orgOut, &stdOut);
#ifdef VERBOSE
        remember[7] = rc;
#endif
    }
    rc = DosClose(orgOut);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles--;
    printf("14 opened files %d\n", openedFiles);;
    remember[8] = rc;
    printf("stdinChanged %d, stdoutChanged %d, stderrChanged %d\n",
           remember[0], remember[1], remember[2]);
    if (remember[0]) {
        printf("DosClose \"new\" stdIn [%d] returned [%d]\n", stdIn,
               remember[3]);
        printf("DosDupHandle orgIn [%d] returned [%d]\n", orgIn,
               remember[4]);
        printf("DosClose orgIn [%d] returned [%d]\n", orgIn,
               remember[5]);
    }
    if (remember[1]) {
        printf("DosClose \"new\" stdOut [%d] returned [%d]\n", stdOut,
               remember[6]);
        printf("DosDupHandle orgOut [%d] returned [%d]\n", orgOut,
               remember[7]);
        printf("DosClose orgOut [%d] returned [%d]\n", orgOut,
               remember[8]);
    }
    fflush(stdout);
#endif

    if (stderrChanged) {
        rc = DosClose(stdErr);
#ifdef VERBOSE
        if (rc == NO_ERROR) openedFiles--;
        printf("15 opened files %d\n", openedFiles);;
        printf("DosClose \"new\" stdErr [%d] returned [%d]\n", stdErr, rc);
        fflush(stdout);
#endif
        rc = DosDupHandle(orgErr, &stdErr);
#ifdef VERBOSE
        printf("DosDupHandle orgErr [%d] returned [%d]\n", orgErr, rc);
        fflush(stdout);
#endif
    }
    rc = DosClose(orgErr);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles--;
    printf("16 opened files %d\n", openedFiles);;
    fflush(stdout);
#endif

    return result;
}

/*
 *--------------------------------------------------------------------
 *
 * ApplicationType --
 *
 *      Search for the specified program and identify if it refers to a DOS,
 *      Windows 3.x, OS/2 Windowable, OS/2 Full-Screen, OS/2 PM program.
 *      Used to determine how to invoke a program (if it can even be invoked).
 * Results:
 *      The return value is one of APPL_DOS, APPL_WIN3X, or APPL_WIN32
 *      if the filename referred to the corresponding application type.
 *      If the file name could not be found or did not refer to any known
 *      application type, APPL_NONE is returned and an error message is
 *      left in interp.  .bat files are identified as APPL_DOS.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ApplicationType(interp, originalName, fullPath)
    Tcl_Interp *interp;         /* Interp, for error message. */
    const char *originalName;   /* Name of the application to find. */
    char fullPath[MAX_PATH];    /* Filled with complete path to
                                 * application. */
{
    APIRET rc = NO_ERROR;
    int applType, i;
    char *ext;
    static char *extensions[] = {"", ".cmd", ".exe", ".bat", ".com", NULL};
    char tmpPath[MAX_PATH];
    FILESTATUS3 filestat;
    ULONG flags;

#ifdef VERBOSE
    printf("ApplicationType\n");
    fflush(stdout);
#endif

    applType = APPL_NONE;
    for (i = 0; extensions[i] != NULL; i++) {
        strncpy((char *)tmpPath, originalName, MAX_PATH - 5);
#ifdef VERBOSE
        printf("after strncpy, tmpPath %s, originalName %s\n", tmpPath,
                originalName);
        fflush(stdout);
#endif
        strcat(tmpPath, extensions[i]);
#ifdef VERBOSE
        printf("after strcat, tmpPath %s, extensions[%d] [%s]\n", tmpPath,
                i, extensions[i]);
        fflush(stdout);
#endif

        if (tmpPath[1] != ':' && tmpPath[0] != '\\') {
            rc = DosSearchPath(SEARCH_ENVIRONMENT | SEARCH_CUR_DIRECTORY |
                               SEARCH_IGNORENETERRS, "PATH", tmpPath,
                               (PBYTE)fullPath, MAXPATH);
            if (rc != NO_ERROR) {
#ifdef VERBOSE
                printf("DosSearchPath %s ERROR %d\n", tmpPath, rc);
                fflush(stdout);
#endif
                continue;
            }
#ifdef VERBOSE
            printf("DosSearchPath %s OK (%s)\n", tmpPath, fullPath);
            fflush(stdout);
#endif
        } else {
            strcpy(fullPath, tmpPath);
        }

        /*
         * Ignore matches on directories or data files, return if identified
         * a known type.
         */

        rc = DosQueryPathInfo(fullPath, FIL_STANDARD, &filestat,
                              sizeof(FILESTATUS3));
        if (rc != NO_ERROR || filestat.attrFile & FILE_DIRECTORY) {
#ifdef VERBOSE
            printf("DosQueryPathInfo %s returns (%d)\n", fullPath, rc);
            fflush(stdout);
#endif
            continue;
        }

        /*
         * I'd have guessed that DosQueryAppType would return FAPPTYP_NOTSPEC
         * for e.g. .cmd, .bat. Alas, it returns an error 193 for a .cmd
         * ERROR_BAD_EXE_FORMAT (which, according to the docs, cannot be
         * returned by DosQueryAppType, but is sounds logical enough).
         * So, don't use this function on batch extensions.
         * OS/2 .com (eg. keyb.com) return WINDOWCOMPAT.
         */
        if (extensions[i] == ".cmd") {
            applType = APPL_OS2CMD;
                break;
        }
        if (extensions[i] == ".bat") {
            applType = APPL_DOS;
            break;
        }

        rc = DosQueryAppType(fullPath, &flags);
        if (rc != NO_ERROR) {
#ifdef VERBOSE
            printf("DosQueryAppType %s returns %d\n", fullPath, rc);
            fflush(stdout);
#endif
            continue;
        }

        if ((flags & FAPPTYP_DLL) || (flags & FAPPTYP_PHYSDRV) ||
            (flags & FAPPTYP_VIRTDRV) || (flags & FAPPTYP_PROTDLL)) {
            /* No executable */
#ifdef VERBOSE
            printf("No executable\n");
            fflush(stdout);
#endif
            continue;
        } 

        if (flags & FAPPTYP_NOTSPEC) {

            /* Still not recognized, might be a Win32 PE-executable */
            applType = APPL_NONE;
            break;
        }

        /*
         * NB! Some bozo defined FAPPTYP_WINDOWAPI as 0x0003 instead of 0x0004,
         * thereby causing it to have bits in common with both
         * FAPPTYP_NOTWINDOWCOMPAT (0x0001) and FAPPTYP_WINDOWCOMPAT (0x0002),
         * which means that for any OS/2 app, you get "PM app" as answer if
         * you don't take extra measures.
         * This is found in EMX 0.9c, 0.9b *AND* in IBM's own Visual Age C++
         * 3.0, so I must assume Eberhard Mattes was forced to follow the
         * drunk that defined these defines in the LX format....
         */
        if (flags & FAPPTYP_NOTWINDOWCOMPAT) {
            applType = APPL_OS2FS;
        }
        if (flags & FAPPTYP_WINDOWCOMPAT) {
            applType = APPL_OS2WIN;
        }
        /*
         * This won't work:
        if (flags & FAPPTYP_WINDOWAPI) {
            applType = APPL_OS2PM;
        }
         * Modified version:
         */
        if ((flags & FAPPTYP_NOTWINDOWCOMPAT)
            && (flags & FAPPTYP_WINDOWCOMPAT)) {
            applType = APPL_OS2PM;
        }
        if (flags & FAPPTYP_DOS) {
            applType = APPL_DOS;
        }
        if ((flags & FAPPTYP_WINDOWSREAL) || (flags & FAPPTYP_WINDOWSPROT)
            || (flags & FAPPTYP_WINDOWSPROT31)) {
            applType = APPL_WIN3X;
        }

        break;
    }

    if (applType == APPL_NONE) {
#ifdef VERBOSE
        printf("ApplicationType: APPL_NONE\n");
    fflush(stdout);
#endif
        TclOS2ConvertError(rc);
        Tcl_AppendResult(interp, "couldn't execute \"", originalName,
                "\": ", Tcl_PosixError(interp), (char *) NULL);
        return APPL_NONE;
    }
    return applType;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpCreateCommandChannel --
 *
 *      This function is called by Tcl_OpenCommandChannel to perform
 *      the platform specific channel initialization for a command
 *      channel.
 *
 * Results:
 *      Returns a new channel or NULL on failure.
 *
 * Side effects:
 *      Allocates a new channel.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
TclpCreateCommandChannel(
    TclFile readFile,           /* If non-null, gives the file for reading. */
    TclFile writeFile,          /* If non-null, gives the file for writing. */
    TclFile errorFile,          /* If non-null, gives the file where errors
                                 * can be read. */
    int numPids,                /* The number of pids in the pid array. */
    Tcl_Pid *pidPtr)            /* An array of process identifiers. */
{
    APIRET rc;
    char channelName[16 + TCL_INTEGER_SPACE];
    int channelId;
    PipeInfo *infoPtr = (PipeInfo *) ckalloc((unsigned) sizeof(PipeInfo));

#ifdef VERBOSE
    printf("TclpCreateCommandChannel infoPtr %x\n", infoPtr);
    fflush(stdout);
#endif

    PipeInit();

    infoPtr->watchMask = 0;
    infoPtr->flags = 0;
    infoPtr->readFlags = 0;
    infoPtr->readFile = readFile;
#ifdef VERBOSE
    if (readFile == NULL) {
        printf("TclpCreateCommandChannel assigning NULL readFile\n");
        fflush(stdout);
    }
#endif
    infoPtr->writeFile = writeFile;
#ifdef VERBOSE
    if (writeFile == NULL) {
        printf("TclpCreateCommandChannel assigning NULL writeFile\n");
        fflush(stdout);
    }
#endif
    infoPtr->errorFile = errorFile;
#ifdef VERBOSE
    if (errorFile == NULL) {
        printf("TclpCreateCommandChannel assigning NULL errorFile\n");
        fflush(stdout);
    }
#endif
    infoPtr->numPids = numPids;
    infoPtr->pidPtr = pidPtr;
    infoPtr->writeBuf = 0;
    infoPtr->writeBufLen = 0;
    infoPtr->writeError = 0;

    /*
     * Use one of the fds associated with the channel as the
     * channel id.
     */

    if (readFile) {
        channelId = (int) ((OS2File*)readFile)->handle;
    } else if (writeFile) {
        channelId = (int) ((OS2File*)writeFile)->handle;
    } else if (errorFile) {
        channelId = (int) ((OS2File*)errorFile)->handle;
    } else {
        channelId = 0;
    }

    infoPtr->validMask = 0;

    infoPtr->threadId = Tcl_GetCurrentThread();

    if (readFile != NULL) {
        /*
         * Start the background reader thread.
         */
        SEMRECORD muxWaitSem[2] = { {(HSEM)0, 0}, {(HSEM)0, 1} };

        rc = DosCreateEventSem(NULL, &infoPtr->readable, 0L, TRUE);
/**** WIN port uses a non-manual Event object for startReader */
        rc = DosCreateEventSem(NULL, &infoPtr->startReader, 0L, FALSE);
        rc = DosCreateEventSem(NULL, &infoPtr->stopReader, 0L, FALSE);
        muxWaitSem[0].hsemCur = (HSEM) infoPtr->startReader;
        muxWaitSem[1].hsemCur = (HSEM) infoPtr->stopReader;
        rc = DosCreateMuxWaitSem(NULL, &infoPtr->readerMuxWaitSem, 2,
                                 muxWaitSem, DCMW_WAIT_ANY);
        infoPtr->readThread = _beginthread(PipeReaderThread, NULL, 32768,
                                           (PVOID)infoPtr);
        /*
         * The Windows port sets the priority for the new thread to the
         * highest possible value. I don't think I agree...
        rc = DosSetPriority(PRTYS_THREAD, PRTYC_NOCHANGE, PRTYD_MAXIMUM,
                            infoPtr->readThread);
         */
        infoPtr->validMask |= TCL_READABLE;
    } else {
        infoPtr->readThread = 0;
    }
    if (writeFile != NULL) {
        /*
         * Start the background writer thread.
         */

        rc = DosCreateEventSem(NULL, &infoPtr->writable, 0L, TRUE);
/**** WIN port uses a non-manual Event object fot startWriter */
        rc = DosCreateEventSem(NULL, &infoPtr->startWriter, 0L, FALSE);
        infoPtr->writeThread = _beginthread(PipeWriterThread, NULL, 32768,
                                            (PVOID)infoPtr);
        /*
         * The Windows port sets the priority for the new thread to the
         * highest possible value. I don't think I agree...
        rc = DosSetPriority(PRTYS_THREAD, PRTYC_NOCHANGE, PRTYD_MAXIMUM,
                            infoPtr->writeThread);
         */
        infoPtr->validMask |= TCL_WRITABLE;
    }

    /*
     * For backward compatibility with previous versions of Tcl, we
     * use "file%d" as the base name for pipes even though it would
     * be more natural to use "pipe%d".
     */

    sprintf(channelName, "file%d", channelId);
    infoPtr->channel = Tcl_CreateChannel(&pipeChannelType, channelName,
            (ClientData) infoPtr, infoPtr->validMask);

    /*
     * Pipes have AUTO translation mode on OS/2 and ^Z eof char, which
     * means that a ^Z will be appended to them at close. This is needed
     * for programs that expect a ^Z at EOF.
     */

    Tcl_SetChannelOption((Tcl_Interp *) NULL, infoPtr->channel,
            "-translation", "auto");
    Tcl_SetChannelOption((Tcl_Interp *) NULL, infoPtr->channel,
            "-eofchar", "\032 {}");
    return infoPtr->channel;
}

/*
 *----------------------------------------------------------------------
 *
 * TclGetAndDetachPids --
 *
 *      Stores a list of the command PIDs for a command channel in
 *      interp->result.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies interp->result.
 *
 *----------------------------------------------------------------------
 */

void
TclGetAndDetachPids(interp, chan)
    Tcl_Interp *interp;
    Tcl_Channel chan;
{
    PipeInfo *pipePtr;
    Tcl_ChannelType *chanTypePtr;
    int i;
    char buf[20];

#ifdef VERBOSE
    printf("TclGetAndDetachPids\n");
    fflush(stdout);
#endif

    /*
     * Punt if the channel is not a command channel.
     */

    chanTypePtr = Tcl_GetChannelType(chan);
    if (chanTypePtr != &pipeChannelType) {
        return;
    }

    pipePtr = (PipeInfo *) Tcl_GetChannelInstanceData(chan);
    for (i = 0; i < pipePtr->numPids; i++) {
        sprintf(buf, "%lu", (unsigned long) pipePtr->pidPtr[i]);
        Tcl_AppendElement(interp, buf);
        Tcl_DetachPids(1, &(pipePtr->pidPtr[i]));
    }
    if (pipePtr->numPids > 0) {
        ckfree((char *) pipePtr->pidPtr);
        pipePtr->numPids = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PipeBlockModeProc --
 *
 *      Set blocking or non-blocking mode on channel.
 *
 * Results:
 *      0 if successful, errno when failed.
 *
 * Side effects:
 *      Sets the device into blocking or non-blocking mode.
 *
 *----------------------------------------------------------------------
 */

static int
PipeBlockModeProc(instanceData, mode)
    ClientData instanceData;    /* Instance data for channel. */
    int mode;                   /* TCL_MODE_BLOCKING or
                                 * TCL_MODE_NONBLOCKING. */
{
    PipeInfo *infoPtr = (PipeInfo *) instanceData;

#ifdef VERBOSE
    printf("PipeBlockModeProc\n");
    fflush(stdout);
#endif

    /*
     * Unnamed pipes on OS/2 can not be switched between blocking and
     * nonblocking, hence we have to emulate the behavior. This is done in
     * the input function by checking against a bit in the state. We set or
     * unset the bit here to cause the input function to emulate the correct
     * behavior.
     */

    if (mode == TCL_MODE_NONBLOCKING) {
        infoPtr->flags |= PIPE_ASYNC;
    } else {
        infoPtr->flags &= ~(PIPE_ASYNC);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * PipeClose2Proc --
 *
 *      Closes a pipe based IO channel.
 *
 * Results:
 *      0 on success, errno otherwise.
 *
 * Side effects:
 *      Closes the physical channel.
 *
 *----------------------------------------------------------------------
 */

static int
PipeClose2Proc(
    ClientData instanceData,    /* Pointer to PipeInfo structure. */
    Tcl_Interp *interp,         /* For error reporting. */
    int flags)                  /* Flags that indicate which side to close. */
{
    PipeInfo *pipePtr = (PipeInfo *) instanceData;
    Tcl_Channel errChan;
    int errorCode, result;
    PipeInfo *infoPtr, **nextPtrPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
    APIRET rc;

#ifdef VERBOSE
    printf("PipeClose2Proc pipePtr %x\n", pipePtr);
    fflush(stdout);
#endif

    errorCode = 0;
    if ((!flags || (flags == TCL_CLOSE_READ))
        && (pipePtr->readFile != NULL)) {
        /*
         * Clean up the background thread if necessary.  Note that this
         * must be done before we can close the file, since the
         * thread may be blocking trying to read from the pipe.
         */

        if (pipePtr->readThread) {
            /*
             * The thread may already have closed on it's own.  Check for
             * that.
             */

            rc = DosWaitThread(&pipePtr->readThread, DCWW_NOWAIT);
            if (rc == ERROR_THREAD_NOT_TERMINATED) {
                /*
                 * Set the stop event so that if the reader thread is blocked
                 * in PipeReaderThread on DosWaitMuxWaitSem, it will exit
                 * cleanly.
                 */

                DosPostEventSem(pipePtr->stopReader);

                /*
                 * Wait at most 10 milliseconds for the reader thread to close.
                 */

                DosSleep(10);
                rc = DosWaitThread(&pipePtr->readThread, DCWW_NOWAIT);

                if (rc == ERROR_THREAD_NOT_TERMINATED) {
                    /*
                     * The thread must be blocked waiting for the pipe to
                     * become readable in DosRead().  There isn't a clean way
                     * to exit the thread from this condition.  We should
                     * terminate the child process instead to get the reader
                     * thread to fall out of DosRead with a FALSE.  (below) is
                     * not the correct way to do this, but will stay here until
                     * a better solution is found.
                     *
                     * Note that we need to guard against terminating the
                     * thread while it is in the middle of Tcl_ThreadAlert
                     * because it won't be able to release the notifier lock.
                     */

                    Tcl_MutexLock(&pipeMutex);

                    /*
                     * BUG: this leaks memory
                     * ERROR_BUSY signifies the thread is executing 16bit code. 
                     * This function will not terminate a thread that is
                     * suspended; it will be terminated when it resumes
                     * execution. So we get to choose between trying to close
                     * handles still busy for the suspended thread or waiting
                     * potentially indefinitely for it to end. I choose the
                     * former.
                     */
                    rc = DosKillThread(pipePtr->readThread);

                    /*
                     * DON'T Wait for the thread to terminate.
                    rc = DosWaitThread(&pipePtr->readThread, DCWW_WAIT);
                     */

                    Tcl_MutexUnlock(&pipeMutex);
                }
            }

            pipePtr->readThread = NULLHANDLE;
            DosCloseEventSem(pipePtr->readable);
            DosCloseEventSem(pipePtr->startReader);
            DosCloseEventSem(pipePtr->stopReader);
            DosCloseMuxWaitSem(pipePtr->readerMuxWaitSem);
            pipePtr->readThread = NULLHANDLE;
        }
#ifdef VERBOSE
        printf("PipeClose2Proc closing readFile\n");
        fflush(stdout);
#endif
        if (TclpCloseFile(pipePtr->readFile) != 0) {
            errorCode = errno;
        }
        pipePtr->validMask &= ~TCL_READABLE;
        pipePtr->readFile = NULL;
    }
    if ((!flags || (flags == TCL_CLOSE_READ)) &&
        (pipePtr->writeFile != NULL)) {
        /*
         * Wait for the writer thread to finish the current buffer, then
         * terminate the thread and close the handles.  If the channel is
         * nonblocking, there should be no pending write operations.
         */

        if (pipePtr->writeThread) {
            DosWaitEventSem(pipePtr->writable, SEM_INDEFINITE_WAIT);

            /*
             * Forcibly terminate the background thread.  We cannot rely on the
             * thread to cleanly terminate itself because we have no way of
             * closing the pipe handle without blocking in the case where the
             * thread is in the middle of an I/O operation.  Note that we need
             * to guard against terminating the thread while it is in the
             * middle of Tcl_ThreadAlert because it won't be able to release
             * the notifier lock.
             * DosKillThread will not terminate a thread that is suspended;
             * it will be terminated when it resumes execution. So we get to
             * choose between trying to close handles still busy for the
             * suspended thread or waiting potentially indefinitely for it to
             * end. I choose the former.
             */

            Tcl_MutexLock(&pipeMutex);
            DosKillThread(pipePtr->writeThread);

            /*
             * DON'T Wait for the thread to terminate.
             * This would ensure that we are completely cleaned up before we
             * leave this function, but we might hang here forever...
            rc = DosWaitThread(&pipePtr->writeThread, DCWW_WAIT);
             */

            Tcl_MutexUnlock(&pipeMutex);


            DosCloseEventSem(pipePtr->writable);
            DosCloseEventSem(pipePtr->startWriter);
            pipePtr->writeThread = NULLHANDLE;
        }
#ifdef VERBOSE
        printf("PipeClose2Proc closing writeFile\n");
        fflush(stdout);
#endif
        if (TclpCloseFile(pipePtr->writeFile) != 0) {
            if (errorCode == 0) {
                errorCode = errno;
            }
        }
        pipePtr->validMask &= ~TCL_WRITABLE;
        pipePtr->writeFile = NULL;
    }

    pipePtr->watchMask &= pipePtr->validMask;

    /*
     * Don't free the channel if any of the flags were set.
     */

    if (flags) {
        return errorCode;
    }

    /*
     * Remove the file from the list of watched files.
     */

    for (nextPtrPtr = &(tsdPtr->firstPipePtr), infoPtr = *nextPtrPtr;
            infoPtr != NULL;
            nextPtrPtr = &infoPtr->nextPtr, infoPtr = *nextPtrPtr) {
        if (infoPtr == (PipeInfo *)pipePtr) {
            *nextPtrPtr = infoPtr->nextPtr;
            break;
        }
    }

    /*
     * Wrap the error file into a channel and give it to the cleanup
     * routine.
     */

    if (pipePtr->errorFile) {
        OS2File *filePtr;
        filePtr = (OS2File *)pipePtr->errorFile;
        errChan = Tcl_MakeFileChannel((ClientData) filePtr->handle,
                TCL_READABLE);
#ifdef VERBOSE
        printf("PipeClose2Proc ckfreeing filePtr %x\n", filePtr);
        fflush(stdout);
#endif
        /* If this is an OS2_TMPFILE, we have to remove the exit handler */
        if (filePtr->type == OS2_TMPFILE) {
            rc = DosDelete((PSZ)((TmpFile*)filePtr)->name);
            if (rc == NO_ERROR) {
#ifdef VERBOSE
                printf("    DosDelete OK\n");
                fflush(stdout);
#endif
                if (((TmpFile*)filePtr)->name != NULL) {
                    /* Watch it! name was *malloc*ed by tempnam */
                    free((char *)((TmpFile*)filePtr)->name);
                }
                ckfree((char *)filePtr);
                /* Succesful deletion, remove exithandler */
                Tcl_DeleteExitHandler(TempFileCleanup, (ClientData)filePtr);
#ifdef VERBOSE
            } else {
                /* Not succesful, keep the exit handler, don't free filePtr */
                printf("    DosDelete ERROR %d\n", rc);
                fflush(stdout);
#endif
            }
        } else {
            ckfree((char *) filePtr);
        }
    } else {
        errChan = NULL;
    }
    result = TclCleanupChildren(interp, pipePtr->numPids, pipePtr->pidPtr,
            errChan);

    if (pipePtr->numPids > 0) {
        ckfree((char *) pipePtr->pidPtr);
    }

    if (pipePtr->writeBuf != NULL) {
        ckfree(pipePtr->writeBuf);
    }

    ckfree((char*) pipePtr);

    if (errorCode == 0) {
        return result;
    }
    return errorCode;
}

/*
 *----------------------------------------------------------------------
 *
 * PipeInputProc --
 *
 *      Reads input from the IO channel into the buffer given. Returns
 *      count of how many bytes were actually read, and an error indication.
 *
 * Results:
 *      A count of how many bytes were read is returned and an error
 *      indication is returned in an output argument.
 *
 * Side effects:
 *      Reads input from the actual channel.
 *
 *----------------------------------------------------------------------
 */

static int
PipeInputProc(
    ClientData instanceData,            /* Pipe state. */
    char *buf,                          /* Where to store data read. */
    int bufSize,                        /* How much space is available
                                         * in the buffer? */
    int *errorCode)                     /* Where to store error code. */
{
    APIRET rc;
    PipeInfo *infoPtr = (PipeInfo *) instanceData;
    OS2File *filePtr = (OS2File*) infoPtr->readFile;
    ULONG count, bytesRead = 0;
    int result;

#ifdef VERBOSE
    printf("PipeInputProc\n");
    fflush(stdout);
#endif

    *errorCode = 0;
    /*
     * Synchronize with the reader thread.
     */

    result = WaitForRead(infoPtr, (infoPtr->flags & PIPE_ASYNC) ? 0 : 1);

    /*
     * If an error occurred, return immediately.
     */

    if (result == -1) {
        *errorCode = errno;
        return -1;
    }

    if (infoPtr->readFlags & PIPE_EXTRABYTE) {
        /*
         * The reader thread consumed 1 byte as a side effect of
         * waiting so we need to move it into the buffer.
         */

        *buf = infoPtr->extraByte;
        infoPtr->readFlags &= ~PIPE_EXTRABYTE;
        buf++;
        bufSize--;
        bytesRead = 1;

        /*
         * If further read attempts would block, return what we have.
         */

        if (result == 0) {
            return bytesRead;
        }
    }

    /*
     * Attempt to read bufSize bytes.  The read will return immediately
     * if there is any data available.  Otherwise it will block until
     * at least one byte is available or an EOF occurs.
     */

    rc = DosRead(filePtr->handle, (PVOID) buf, (ULONG) bufSize, &count);
#ifdef VERBOSE
    { int i;
    printf("DosRead handle [%d] returns [%d], bytes read [%d]\n",
           filePtr->handle, rc, bytesRead);
    fflush(stdout);
    }
#endif
    if (rc == NO_ERROR) {
        return bytesRead + count;
    } else if (bytesRead) {
        /*
         * Ignore errors if we have data to return.
         */

        return bytesRead;
    }

    TclOS2ConvertError(rc);
    if (errno == EPIPE) {
        infoPtr->readFlags |= PIPE_EOF;
        return 0;
    }
    *errorCode = errno;
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * PipeOutputProc --
 *
 *      Writes the given output on the IO channel. Returns count of how
 *      many characters were actually written, and an error indication.
 *
 * Results:
 *      A count of how many characters were written is returned and an
 *      error indication is returned in an output argument.
 *
 * Side effects:
 *      Writes output on the actual channel.
 *
 *----------------------------------------------------------------------
 */

static int
PipeOutputProc(
    ClientData instanceData,            /* Pipe state. */
    char *buf,                          /* The data buffer. */
    int toWrite,                        /* How many bytes to write? */
    int *errorCode)                     /* Where to store error code. */
{
    APIRET rc;
    PipeInfo *infoPtr = (PipeInfo *) instanceData;
    OS2File *filePtr = (OS2File *) infoPtr->writeFile;
    ULONG bytesWritten, timeout;

#ifdef VERBOSE
    printf("PipeOutputProc infoPtr %x filePtr %x\n", infoPtr, filePtr);
    fflush(stdout);
#endif

    *errorCode = 0;
    timeout = (infoPtr->flags & PIPE_ASYNC) ? 0 : SEM_INDEFINITE_WAIT;
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

    if (infoPtr->flags & PIPE_ASYNC) {
        ULONG postCount;

        /*
         * The pipe is non-blocking, so copy the data into the output
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
           infoPtr->writeBuf = ckalloc((unsigned int) toWrite);
        }
        memcpy(infoPtr->writeBuf, buf, (size_t) toWrite);
        infoPtr->toWrite = toWrite;
        DosResetEventSem(infoPtr->writable, &postCount);
        DosPostEventSem(infoPtr->startWriter);
        bytesWritten = toWrite;
    } else {
        /*
         * In the blocking case, just try to write the buffer directly.
         * This avoids an unnecessary copy.
         */

        if (filePtr == NULL) {
            rc = ERROR_INVALID_HANDLE;
#ifdef VERBOSE
            printf("PipeOutputProc filePtr NULL => rc= ERROR_INVALID_HANDLE\n");
            fflush(stdout);
#endif
        } else {
            rc = DosWrite(filePtr->handle, (PVOID) buf, (ULONG) toWrite,
                          &bytesWritten);
        }
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
 * PipeEventProc --
 *
 *      This function is invoked by Tcl_ServiceEvent when a file event
 *      reaches the front of the event queue.  This procedure invokes
 *      Tcl_NotifyChannel on the pipe.
 *
 * Results:
 *      Returns 1 if the event was handled, meaning it should be removed
 *      from the queue.  Returns 0 if the event was not handled, meaning
 *      it should stay on the queue.  The only time the event isn't
 *      handled is if the TCL_FILE_EVENTS flag bit isn't set.
 *
 * Side effects:
 *      Whatever the notifier callback does.
 *
 *----------------------------------------------------------------------
 */

static int
PipeEventProc(
    Tcl_Event *evPtr,           /* Event to service. */
    int flags)                  /* Flags that indicate what events to
                                 * handle, such as TCL_FILE_EVENTS. */
{
    PipeEvent *pipeEvPtr = (PipeEvent *)evPtr;
    PipeInfo *infoPtr;
    OS2File *filePtr;
    int mask;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

#ifdef VERBOSE
    printf("PipeEventProc\n");
    fflush(stdout);
#endif

    if (!(flags & TCL_FILE_EVENTS)) {
        return 0;
    }

    /*
     * Search through the list of watched pipes for the one whose handle
     * matches the event.  We do this rather than simply dereferencing
     * the handle in the event so that pipes can be deleted while the
     * event is in the queue.
     */

    for (infoPtr = tsdPtr->firstPipePtr; infoPtr != NULL;
            infoPtr = infoPtr->nextPtr) {
        if (pipeEvPtr->infoPtr == infoPtr) {
            infoPtr->flags &= ~(PIPE_PENDING);
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
     * Check to see if the pipe is readable.
     * Note that if we can't tell if a pipe is writable, we always report it
     * as being writable unless we have detected EOF.
     */

    filePtr = (OS2File*) ((PipeInfo*)infoPtr)->readFile;
    mask = 0;
    if ((infoPtr->watchMask & TCL_WRITABLE) &&
        (DosWaitEventSem(infoPtr->writable, SEM_IMMEDIATE_RETURN)
         != ERROR_TIMEOUT)) {
        mask = TCL_WRITABLE;
    }

    filePtr = (OS2File*) ((PipeInfo*)infoPtr)->readFile;
    if ((infoPtr->watchMask & TCL_READABLE) &&
            (WaitForRead(infoPtr, 0) >= 0)) {
        if (infoPtr->readFlags & PIPE_EOF) {
            mask = TCL_READABLE;
        } else {
            mask |= TCL_READABLE;
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
 * PipeWatchProc --
 *
 *      Called by the notifier to set up to watch for events on this
 *      channel.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
PipeWatchProc(
    ClientData instanceData,            /* Pipe state. */
    int mask)                           /* What events to watch for; OR-ed
                                         * combination of TCL_READABLE,
                                         * TCL_WRITABLE and TCL_EXCEPTION. */
{
    PipeInfo **nextPtrPtr, *ptr;
    PipeInfo *infoPtr = (PipeInfo *) instanceData;
    int oldMask = infoPtr->watchMask;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

#ifdef VERBOSE
    printf("PipeWatchProc\n");
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
            infoPtr->nextPtr = tsdPtr->firstPipePtr;
            tsdPtr->firstPipePtr = infoPtr;
        }
        Tcl_SetMaxBlockTime(&blockTime);
    } else {
        if (oldMask) {
            /*
             * Remove the pipe from the list of watched pipes.
             */

            for (nextPtrPtr = &(tsdPtr->firstPipePtr), ptr = *nextPtrPtr;
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
 * PipeGetHandleProc --
 *
 *      Called from Tcl_GetChannelHandle to retrieve OS handles from
 *      inside a command pipeline based channel.
 *
 * Results:
 *      Returns TCL_OK with the fd in handlePtr, or TCL_ERROR if
 *      there is no handle for the specified direction.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
PipeGetHandleProc(
    ClientData instanceData,    /* The pipe state. */
    int direction,              /* TCL_READABLE or TCL_WRITABLE */
    ClientData *handlePtr)      /* Where to store the handle.  */
{
    PipeInfo *infoPtr = (PipeInfo *) instanceData;
    OS2File *filePtr;

#ifdef VERBOSE
    printf("PipeGetHandleProc\n");
    fflush(stdout);
#endif

    if (direction == TCL_READABLE && infoPtr->readFile) {
        filePtr = (OS2File*) infoPtr->readFile;
        *handlePtr = (ClientData) filePtr->handle;
        return TCL_OK;
    }
    if (direction == TCL_WRITABLE && infoPtr->writeFile) {
        filePtr = (OS2File*) infoPtr->writeFile;
        *handlePtr = (ClientData) filePtr->handle;
        return TCL_OK;
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_WaitPid --
 *
 *      Emulates the waitpid system call.
 *
 * Results:
 *      Returns 0 if the process is still alive, -1 on an error, or
 *      the pid on a clean close.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Pid
Tcl_WaitPid(
    Tcl_Pid pid,
    int *statPtr,
    int options)
{
    APIRET rc;
    ULONG flags;

#ifdef VERBOSE
    printf("Tcl_WaitPid\n");
    fflush(stdout);
#endif

    PipeInit();

    if (options & WNOHANG) {
        flags = DCWW_NOWAIT;
    } else {
        flags = DCWW_WAIT;
    }

#ifdef VERBOSE
    printf("Waiting for PID %d (%s)", pid,
           options & WNOHANG ? "WNOHANG" : "WAIT");
    fflush(stdout);
#endif
    rc = waitpid((int)pid, statPtr, options);
#ifdef VERBOSE
    printf(", returns %d (*statPtr %x) %s %d\n", rc, *statPtr,
           WIFEXITED(*statPtr) ? "WIFEXITED" :
           (WIFSIGNALED(*statPtr) ? "WIFSIGNALED" :
            (WIFSTOPPED(*statPtr) ? "WIFSTOPPED" : "unknown")),
           WIFEXITED(*statPtr) ? WEXITSTATUS(*statPtr) :
           (WIFSIGNALED(*statPtr) ? WTERMSIG(*statPtr) :
            (WIFSTOPPED(*statPtr) ? WSTOPSIG(*statPtr) : 0)));
    fflush(stdout);
#endif
    return (Tcl_Pid)rc;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_PidObjCmd --
 *
 *      This procedure is invoked to process the "pid" Tcl command.
 *      See the user documentation for details on what it does.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */

        /* ARGSUSED */
int
Tcl_PidObjCmd(
    ClientData dummy,           /* Not used. */
    Tcl_Interp *interp,         /* Current interpreter. */
    int objc,                   /* Number of arguments. */
    Tcl_Obj *CONST *objv)       /* Argument strings. */
{
    Tcl_Channel chan;
    Tcl_ChannelType *chanTypePtr;
    PipeInfo *pipePtr;
    int i;
    Tcl_Obj *resultPtr;
    char buf[TCL_INTEGER_SPACE];

#ifdef VERBOSE
    printf("Tcl_PidObjCmd\n");
    fflush(stdout);
#endif

    if (objc > 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?channelId?");
        return TCL_ERROR;
    }
    if (objc == 1) {
        resultPtr = Tcl_GetObjResult(interp);
        sprintf(buf, "%lu", (unsigned long) getpid());
        Tcl_SetStringObj(resultPtr, buf, -1);
    } else {
        chan = Tcl_GetChannel(interp, Tcl_GetStringFromObj(objv[1], NULL),
                NULL);
        if (chan == (Tcl_Channel) NULL) {
            return TCL_ERROR;
        }
        chanTypePtr = Tcl_GetChannelType(chan);
        if (chanTypePtr != &pipeChannelType) {
            return TCL_OK;
        }

        pipePtr = (PipeInfo *) Tcl_GetChannelInstanceData(chan);
        resultPtr = Tcl_GetObjResult(interp);
        for (i = 0; i < pipePtr->numPids; i++) {
            sprintf(buf, "%lu", (unsigned long)pipePtr->pidPtr[i]);
            Tcl_ListObjAppendElement(/*interp*/ NULL, resultPtr,
                    Tcl_NewStringObj(buf, -1));
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TempFileCleanup
 *
 *      This procedure is a Tcl_ExitProc used to clean up the left-over
 *      temporary files made by TclpCreateTempFile (IF they still exist).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Closes/deletes files in, and deallocates storage used by list.
 *
 *----------------------------------------------------------------------
 */

static void
TempFileCleanup(clientData)
    ClientData clientData;      /* Address of TmpFile structure */
{
    APIRET rc;
    TmpFile *deleteFile = (TmpFile *)clientData;
#ifdef VERBOSE
    printf("TempFileCleanup %x t %d\n", clientData,(int)Tcl_GetCurrentThread());
    fflush(stdout);
    printf("TempFileCleanup %x [%s] (was handle [%d])\n", clientData,
           ((TmpFile*)deleteFile)->name, deleteFile->file.handle);
    fflush(stdout);
#endif
    rc = DosDelete((PSZ)((TmpFile*)deleteFile)->name);
#ifdef VERBOSE
    if (rc != NO_ERROR) {
        printf("    DosDelete ERROR %d\n", rc);
    } else {
        printf("    DosDelete OK\n");
    }
    fflush(stdout);
#endif
    if (deleteFile->name != NULL) {
        /* Watch it! name was *malloc*ed by tempnam, so don't use ckfree */
        free((char *)deleteFile->name);
    }
#ifdef VERBOSE
    printf("TempFileCleanup ckfreeing tmpFilePtr %x\n", deleteFile);
    fflush(stdout);
#endif
    ckfree((char *)deleteFile);
}

/*
 *----------------------------------------------------------------------
 *
 * WaitForRead --
 *
 *      Wait until some data is available, the pipe is at
 *      EOF or the reader thread is blocked waiting for data (if the
 *      channel is in non-blocking mode).
 *
 * Results:
 *      Returns 1 if pipe is readable.  Returns 0 if there is no data
 *      on the pipe, but there is buffered data.  Returns -1 if an
 *      error occurred.  If an error occurred, the threads may not
 *      be synchronized.
 *
 * Side effects:
 *      Updates the shared state flags and may consume 1 byte of data
 *      from the pipe.  If no error occurred, the reader thread is
 *      blocked waiting for a signal from the main thread.
 *
 *----------------------------------------------------------------------
 */

static int
WaitForRead(
    PipeInfo *infoPtr,          /* Pipe state. */
    int blocking)               /* Indicates whether call should be
                                 * blocking or not. */
{
    ULONG timeout, count;
    HFILE handle = ((OS2File *) infoPtr->readFile)->handle;
    AVAILDATA avail;
    ULONG state;
    APIRET rc;

    while (1) {
        /*
         * Synchronize with the reader thread.
         */

        timeout = blocking ? SEM_INDEFINITE_WAIT : 0;
        if (DosWaitEventSem(infoPtr->readable, timeout) == ERROR_TIMEOUT) {
            /*
             * The reader thread is blocked waiting for data and the channel
             * is in non-blocking mode.
             */

            errno = EAGAIN;
            return -1;
        }

        /*
         * At this point, the two threads are synchronized, so it is safe
         * to access shared state.
         */

        /*
         * If the pipe has hit EOF, it is always readable.
         */

        if (infoPtr->readFlags & PIPE_EOF) {
            return 1;
        }

        /*
         * Check to see if there is any data sitting in the pipe.
         */

        /*
         * How do we peek, we don't have a named pipe.
         * just assume the pipe is readable and return 1
         */
        return 1;

        rc = DosPeekNPipe(handle, (PVOID) NULL, 0L, &count, &avail, &state);
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            /*
             * Check to see if the peek failed because of EOF.
             */

            if (errno == EPIPE && state == NP_STATE_CLOSING) {
                infoPtr->readFlags |= PIPE_EOF;
                return 1;
            }

            /*
             * Ignore errors if there is data in the buffer.
             */

            if (infoPtr->readFlags & PIPE_EXTRABYTE) {
                return 0;
            } else {
                return -1;
            }
        }

        /*
         * We found some data in the pipe, so it must be readable.
         */

        if (count > 0) {
            return 1;
        }

        /*
         * The pipe isn't readable, but there is some data sitting
         * in the buffer, so return immediately.
         */

        if (infoPtr->readFlags & PIPE_EXTRABYTE) {
            return 0;
        }

        /*
         * There wasn't any data available, so reset the thread and
         * try again.
         */

        DosResetEventSem(infoPtr->readable, &count /* not used anymore */);
        DosPostEventSem(infoPtr->startReader);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PipeReaderThread --
 *
 *      This function runs in a separate thread and waits for input
 *      to become available on a pipe.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Signals the main thread when input become available.  May
 *      cause the main thread to wake up by posting a message.  May
 *      consume one byte from the pipe for each wait operation.  Will
 *      cause a memory leak of ~4k, if forcefully terminated with
 *      DosKillThread().
 *
 *----------------------------------------------------------------------
 */

static void
PipeReaderThread(void *arg)
{
    APIRET rc;
    PipeInfo *infoPtr = (PipeInfo *)arg;
    HFILE handle = ((OS2File *) infoPtr->readFile)->handle;
    ULONG count, whichEventSem;
    int done = 0;
    APIRET retVal;

    while (!done) {
        /*
         * Wait for the main thread to signal before attempting to wait
         * on the pipe becoming readable.
         */

        retVal = DosWaitMuxWaitSem(infoPtr->readerMuxWaitSem,
                                   SEM_INDEFINITE_WAIT, &whichEventSem);

        /* whichEventSem is 0 for startReader, 1 for stopReader */
        if (retVal != NO_ERROR || whichEventSem != 0) {
            /*
             * The start event was not signaled.  It might be the stop event
             * or an error, so exit.
             */

            return;
        }
        DosResetEventSem(infoPtr->startReader, &count);

        /*
         * Check to see if there is any data sitting in the pipe.
         */

        rc = DosRead(handle, (PVOID) &(infoPtr->extraByte), 1L, &count);
        if (rc == NO_ERROR) {
            /*
             * One byte was consumed as a side effect of waiting
             * for the pipe to become readable.
             */

            infoPtr->readFlags |= PIPE_EXTRABYTE;
        } else {
            if (rc == ERROR_BROKEN_PIPE) {
                /*
                 * The error is a result of an EOF condition, so set the
                 * EOF bit before signalling the main thread.
                 */

                infoPtr->readFlags |= PIPE_EOF;
               done = 1;
           } else if (rc == ERROR_INVALID_HANDLE) {
               break;
           }
       }


       /*
        * Signal the main thread by signalling the readable event and
        * then waking up the notifier thread.
        */

       DosPostEventSem(infoPtr->readable);

        /*
         * Alert the foreground thread.  Note that we need to treat this like
         * a critical section so the foreground thread does not terminate
         * this thread while we are holding a mutex in the notifier code.
         */

        Tcl_MutexLock(&pipeMutex);
        Tcl_ThreadAlert(infoPtr->threadId);
        Tcl_MutexUnlock(&pipeMutex);
    }
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * PipeWriterThread --
 *
 *      This function runs in a separate thread and writes data
 *      onto a pipe.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Signals the main thread when an output operation is completed.
 *      May cause the main thread to wake up by posting a message.
 *
 *----------------------------------------------------------------------
 */

static void
PipeWriterThread(void *arg)
{
    APIRET rc;
    PipeInfo *infoPtr = (PipeInfo *)arg;
    HFILE handle = ((OS2File *) infoPtr->writeFile)->handle;
    ULONG count, toWrite;
    PVOID buf;
    int done = 0;

    while (!done) {
        /*
         * Wait for the main thread to signal before attempting to write.
         */

        DosWaitEventSem(infoPtr->startWriter, SEM_INDEFINITE_WAIT);
        DosResetEventSem(infoPtr->startWriter, &count);

        buf = (PVOID) infoPtr->writeBuf;
        toWrite = infoPtr->toWrite;

        /*
         * Loop until all of the bytes are written or an error occurs.
         */

        while (toWrite > 0) {
            rc = DosWrite(handle, buf, toWrite, &count);
            if (rc != NO_ERROR) {
                infoPtr->writeError = rc;
                done = 1;
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

        DosPostEventSem(infoPtr->writable);

        /*
         * Alert the foreground thread.  Note that we need to treat this like
         * a critical section so the foreground thread does not terminate
         * this thread while we are holding a mutex in the notifier code.
         */

        Tcl_MutexLock(&pipeMutex);
        Tcl_ThreadAlert(infoPtr->threadId);
        Tcl_MutexUnlock(&pipeMutex);
    }
    return;
}

/*
 *--------------------------------------------------------------------------
 *
 * TclpGetPid --
 *
 *      Given a HANDLE to a child process, return the process id for that
 *      child process.
 *
 * Results:
 *      Returns the process id for the child process.  If the pid was not
 *      known by Tcl, either because the pid was not created by Tcl or the
 *      child process has already been reaped, -1 is returned.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

unsigned long
TclpGetPid(
    Tcl_Pid pid)                /* The HANDLE of the child process. */
{
    return (unsigned long) pid;
}

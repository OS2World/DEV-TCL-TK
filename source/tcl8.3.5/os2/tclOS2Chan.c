/* 
 * tclOS2Chan.c
 *
 *	Common channel driver for OS/2 channels based on files, command
 *	pipes and TCP sockets (EMX).
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1999-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

/*
 * To get the DosDevIOCtl declarations, we need to define INCL_DOSDEVIOCTL
 * before including os2.h, ie. before including tclOS2Int.h.
 */

#define INCL_DOSDEVIOCTL
#include        "tclOS2Int.h"
#undef INCL_DOSDEVIOCTL

/*
 * State flags used in the info structures below.
 */

#define FILE_PENDING    (1<<0)  /* Message is pending in the queue. */
#define FILE_ASYNC      (1<<1)  /* Channel is non-blocking. */
#define FILE_APPEND     (1<<2)  /* File is in append mode. */

/*
 * Additional file types; OS/2 defines HANDTYPE_FILE, HANDTYPE_DEVICE,
 * HANDTYPE_PIPE, HANDTYPE_PROTECTED and HANDTYPE_NETWORK as flags.
 * Watch out, HANDTYPE_FILE is 0x0000...
 */
#define HANDTYPE_SERIAL  (HANDTYPE_PIPE+1)
#define HANDTYPE_CONSOLE (HANDTYPE_PIPE+2)

/* Structure for determining serial port speed */
typedef struct
{
  ULONG ulCurrentRate;
  BYTE  bCurrentFract;
  ULONG ulMinimumRate;
  BYTE  bMinimumFract;
  ULONG ulMaximumRate;
  BYTE  bMaximumFract;
} EXTBAUDRATE;
typedef EXTBAUDRATE *PEXTBAUDRATE;

/*
 * The following structure contains per-instance data for a file based channel
 */

typedef struct FileInfo {
    Tcl_Channel channel;        /* Pointer to channel structure. */
    int validMask;              /* OR'ed combination of TCL_READABLE,
                                 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
                                 * which operations are valid on the file. */
    int watchMask;              /* OR'ed combination of TCL_READABLE,
                                 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
                                 * which events should be reported. */
    int flags;                  /* State flags, see above for a list. */
    HFILE handle;               /* Input/output file. */
    struct FileInfo *nextPtr;   /* Pointer to next registered file. */
    int dirty;                  /* Boolean flag. Set if the OS may have data
                                 * pending on the channel */
} FileInfo;

typedef struct ThreadSpecificData {
    /*
     * List of all file channels currently open.
     */

    FileInfo *firstFilePtr;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

/*
 * The following structure is what is added to the Tcl event queue when
 * file events are generated.
 */

typedef struct FileEvent {
    Tcl_Event header;           /* Information that is standard for
                                 * all events. */
    FileInfo *infoPtr;          /* Pointer to file info structure.  Note
                                 * that we still have to verify that the
                                 * file exists before dereferencing this
                                 * pointer. */
} FileEvent;

/*
 * Static routines for this file:
 */

static int		FileBlockModeProc _ANSI_ARGS_((
    			    ClientData instanceData, int mode));
static void             FileChannelExitHandler _ANSI_ARGS_((
                            ClientData clientData));
static void             FileCheckProc _ANSI_ARGS_((ClientData clientData,
                            int flags));
static int		FileCloseProc _ANSI_ARGS_((ClientData instanceData,
			    Tcl_Interp *interp));
static int              FileEventProc _ANSI_ARGS_((Tcl_Event *evPtr,
                            int flags));
static int		FileGetHandleProc _ANSI_ARGS_((ClientData instanceData,
		            int direction, ClientData *handlePtr));
static ThreadSpecificData *FileInit _ANSI_ARGS_((void));
static int		FileInputProc _ANSI_ARGS_((ClientData instanceData,
		            char *buf, int toRead, int *errorCode));
static int		FileOutputProc _ANSI_ARGS_((
			    ClientData instanceData, char *buf, int toWrite,
                            int *errorCode));
static int		FileSeekProc _ANSI_ARGS_((ClientData instanceData,
			    long offset, int mode, int *errorCode));
static void             FileSetupProc _ANSI_ARGS_((ClientData clientData,
                            int flags));
static void		FileWatchProc _ANSI_ARGS_((ClientData instanceData,
		            int mask));

/*
 * This structure describes the channel type structure for file based IO:
 */

static Tcl_ChannelType fileChannelType = {
    "file",				/* Type name. */
    TCL_CHANNEL_VERSION_2,      /* v2 channel */
    FileCloseProc,              /* Close proc. */
    FileInputProc,              /* Input proc. */
    FileOutputProc,             /* Output proc. */
    FileSeekProc,               /* Seek proc. */
    NULL,                       /* Set option proc. */
    NULL,                       /* Get option proc. */
    FileWatchProc,              /* Set up the notifier to watch the channel. */
    FileGetHandleProc,          /* Get an OS handle from channel. */
    NULL,                       /* close2proc. */
    FileBlockModeProc,          /* Set blocking or non-blocking mode.*/
    NULL,                       /* flush proc. */
    NULL,                       /* handler proc. */
};

/*
 *----------------------------------------------------------------------
 *
 * FileBlockModeProc --
 *
 *	Helper procedure to set blocking and nonblocking modes on a
 *	file based channel. Invoked by generic IO level code.
 *
 * Results:
 *	0 if successful, errno when failed.
 *
 * Side effects:
 *	Sets the device into blocking or non-blocking mode.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
static int
FileBlockModeProc(instanceData, mode)
    ClientData instanceData;		/* File state. */
    int mode;				/* The mode to set. Can be one of
                                         * TCL_MODE_BLOCKING or
                                         * TCL_MODE_NONBLOCKING. */
{
    FileInfo *infoPtr = (FileInfo *) instanceData;

    /*
     * Files on OS/2 can not be switched between blocking and nonblocking,
     * hence we have to emulate the behavior. This is done in the input
     * function by checking against a bit in the state. We set or unset the
     * bit here to cause the input function to emulate the correct behavior.
     */

    if (mode == TCL_MODE_NONBLOCKING) {
        infoPtr->flags |= FILE_ASYNC;
    } else {
        infoPtr->flags &= ~(FILE_ASYNC);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * FileChannelExitHandler --
 *
 *      This function is called to cleanup the channel driver before
 *      Tcl is unloaded.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys the communication window.
 *
 *----------------------------------------------------------------------
 */

static void
FileChannelExitHandler(clientData)
    ClientData clientData;      /* Old window proc */
{
    Tcl_DeleteEventSource(FileSetupProc, FileCheckProc, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * FileCheckProc --
 *
 *      This procedure is called by Tcl_DoOneEvent to check the file
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
FileCheckProc(data, flags)
    ClientData data;            /* Not used. */
    int flags;                  /* Event flags as passed to Tcl_DoOneEvent. */
{
    FileEvent *evPtr;
    FileInfo *infoPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (!(flags & TCL_FILE_EVENTS)) {
        return;
    }

    /*
     * Queue events for any ready files that don't already have events
     * queued.
     */

    for (infoPtr = tsdPtr->firstFilePtr; infoPtr != NULL;
            infoPtr = infoPtr->nextPtr) {
        if (infoPtr->watchMask && !(infoPtr->flags & FILE_PENDING)) {
            infoPtr->flags |= FILE_PENDING;
            evPtr = (FileEvent *) ckalloc(sizeof(FileEvent));
            evPtr->header.proc = FileEventProc;
            evPtr->infoPtr = infoPtr;
            Tcl_QueueEvent((Tcl_Event *) evPtr, TCL_QUEUE_TAIL);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FileCloseProc --
 *
 *	This procedure is called from the generic IO level to perform
 *	channel-type-specific cleanup when a file based channel is closed.
 *
 * Results:
 *	0 if successful, errno if failed.
 *
 * Side effects:
 *	Closes the device of the channel.
 *
 *----------------------------------------------------------------------
 */

static int
FileCloseProc(instanceData, interp)
    ClientData instanceData;	/* File state. */
    Tcl_Interp *interp;		/* For error reporting - unused. */
{
    FileInfo *fileInfoPtr = (FileInfo *) instanceData;
    FileInfo **nextPtrPtr;
    int errorCode = 0;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    /*
     * Remove the file from the watch list.
     */

    FileWatchProc(instanceData, 0);

    /*
     * Don't close the OS/2 handle if the handle is a standard channel
     * during the exit process.  Otherwise, one thread may kill the stdio
     * of another.
     */
    if (!TclInExit() || ((fileInfoPtr->handle != HF_STDIN)
                         && (fileInfoPtr->handle != HF_STDOUT)
                         && (fileInfoPtr->handle != HF_STDERR))) {
        rc = DosClose(fileInfoPtr->handle);
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            errorCode = errno;
        }
#ifdef VERBOSE
          else {
            openedFiles--;
        }
#endif
    }
    for (nextPtrPtr = &(tsdPtr->firstFilePtr); (*nextPtrPtr) != NULL;
         nextPtrPtr = &((*nextPtrPtr)->nextPtr)) {
        if ((*nextPtrPtr) == fileInfoPtr) {
            (*nextPtrPtr) = fileInfoPtr->nextPtr;
            break;
        }
    }
    ckfree((char *)fileInfoPtr);
    return errorCode;
}

/*----------------------------------------------------------------------
 *
 * FileEventProc --
 *
 *      This function is invoked by Tcl_ServiceEvent when a file event
 *      reaches the front of the event queue.  This procedure invokes
 *      Tcl_NotifyChannel on the file.
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
FileEventProc(evPtr, flags)
    Tcl_Event *evPtr;           /* Event to service. */
    int flags;                  /* Flags that indicate what events to
                                 * handle, such as TCL_FILE_EVENTS. */
{
    FileEvent *fileEvPtr = (FileEvent *)evPtr;
    FileInfo *infoPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (!(flags & TCL_FILE_EVENTS)) {
        return 0;
    }

    /*
     * Search through the list of watched files for the one whose handle
     * matches the event.  We do this rather than simply dereferencing
     * the handle in the event so that files can be deleted while the
     * event is in the queue.
     */

    for (infoPtr = tsdPtr->firstFilePtr; infoPtr != NULL;
            infoPtr = infoPtr->nextPtr) {
        if (fileEvPtr->infoPtr == infoPtr) {
            infoPtr->flags &= ~(FILE_PENDING);
            Tcl_NotifyChannel(infoPtr->channel, infoPtr->watchMask);
            break;
        }
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * FileGetHandleProc --
 *
 *	Called from Tcl_GetChannelHandle to retrieve OS handles from
 *	a file based channel.
 *
 * Results:
 *	Returns TCL_OK with the handle in handlePtr, or TCL_ERROR if
 *	there is no handle for the specified direction. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
FileGetHandleProc(instanceData, direction, handlePtr)
    ClientData instanceData;	/* The file state. */
    int direction;		/* TCL_READABLE or TCL_WRITABLE */
    ClientData *handlePtr;	/* Where to store the handle.  */
{
    FileInfo *infoPtr = (FileInfo *) instanceData;

    if (direction & infoPtr->validMask) {
        *handlePtr = (ClientData) infoPtr->handle;
        return TCL_OK;
    } else {
        return TCL_ERROR;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FileInit --
 *
 *      This function creates the window used to simulate file events.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates a new window and creates an exit handler.
 *
 *----------------------------------------------------------------------
 */

static ThreadSpecificData *
FileInit()
{
    ThreadSpecificData *tsdPtr =
        (ThreadSpecificData *)TclThreadDataKeyGet(&dataKey);
    if (tsdPtr == NULL) {
        tsdPtr = TCL_TSD_INIT(&dataKey);
        tsdPtr->firstFilePtr = NULL;
        Tcl_CreateEventSource(FileSetupProc, FileCheckProc, NULL);
        Tcl_CreateThreadExitHandler(FileChannelExitHandler, NULL);
    }
    return tsdPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FileInputProc --
 *
 *	This procedure is invoked from the generic IO level to read
 *	input from a file based channel.
 *
 * Results:
 *	The number of bytes read is returned or -1 on error. An output
 *	argument contains a POSIX error code if an error occurs, or zero.
 *
 * Side effects:
 *	Reads input from the input device of the channel.
 *
 *----------------------------------------------------------------------
 */

static int
FileInputProc(instanceData, buf, toRead, errorCodePtr)
    ClientData instanceData;		/* File state. */
    char *buf;				/* Where to store data read. */
    int toRead;				/* How much space is available
                                         * in the buffer? */
    int *errorCodePtr;			/* Where to store error code. */
{
    FileInfo *infoPtr = (FileInfo *) instanceData;
    ULONG bytesRead;			/* How many bytes were actually
                                         * read from the input device? */

    *errorCodePtr = 0;
    
    /*
     * Note that we will block on reads from a console buffer until a
     * full line has been entered.  The only way I know of to get
     * around this is to write a console driver.  We should probably
     * do this at some point, but for now, we just block.  The same
     * problem exists for files being read over the network.
     */

#ifdef VERBOSE
    printf("FileInputProc to read %d handle %x\n", toRead, infoPtr->handle);
    fflush(stdout);
#endif
    rc = DosRead(infoPtr->handle, (PVOID) buf, (ULONG) toRead, &bytesRead);
#ifdef VERBOSE
    printf("FileInputProc DosRead handle %x returns %d, bytes read [%d]\n",
           infoPtr->handle, rc, bytesRead);
    fflush(stdout);
#endif
    if (rc == NO_ERROR) {
        return bytesRead;
    }

    TclOS2ConvertError(rc);
    *errorCodePtr = errno;
    if (errno == EPIPE) {
        return 0;
    }
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * FileOutputProc--
 *
 *	This procedure is invoked from the generic IO level to write
 *	output to a file channel.
 *
 * Results:
 *	The number of bytes written is returned or -1 on error. An
 *	output argument	contains a POSIX error code if an error occurred,
 *	or zero.
 *
 * Side effects:
 *	Writes output on the output device of the channel.
 *
 *----------------------------------------------------------------------
 */

static int
FileOutputProc(instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;		/* File state. */
    char *buf;				/* The data buffer. */
    int toWrite;			/* How many bytes to write? */
    int *errorCodePtr;			/* Where to store error code. */
{
    FileInfo *infoPtr = (FileInfo *) instanceData;
    ULONG bytesWritten;
    ULONG newPos;

    /*
     * If we are writing to a file that was opened with O_APPEND, we need to
     * seek to the end of the file before writing the current buffer.
     */

    if (infoPtr->flags & FILE_APPEND) {
        DosSetFilePtr(infoPtr->handle, 0, FILE_END, &newPos);
    }

    rc = DosWrite(infoPtr->handle, (PVOID) buf, (ULONG) toWrite, &bytesWritten);
    if (rc != NO_ERROR) {
        TclOS2ConvertError(rc);
        if (errno == EPIPE) {
            return 0;
        }
        *errorCodePtr = errno;
        return -1;
    }
    infoPtr->dirty = 1;
    return bytesWritten;
}

/*
 *----------------------------------------------------------------------
 *
 * FileSeekProc --
 *
 *	This procedure is called by the generic IO level to move the
 *	access point in a file based channel.
 *
 * Results:
 *	-1 if failed, the new position if successful. An output
 *	argument contains the POSIX error code if an error occurred,
 *	or zero.
 *
 * Side effects:
 *	Moves the location at which the channel will be accessed in
 *	future operations.
 *
 *----------------------------------------------------------------------
 */

static int
FileSeekProc(instanceData, offset, mode, errorCodePtr)
    ClientData instanceData;			/* File state. */
    long offset;				/* Offset to seek to. */
    int mode;					/* Relative to where
                                                 * should we seek? Can be
                                                 * one of SEEK_START,
                                                 * SEEK_SET or SEEK_END. */
    int *errorCodePtr;				/* To store error code. */
{
    FileInfo *infoPtr = (FileInfo *) instanceData;
    ULONG moveMethod;
    ULONG newPos;

    *errorCodePtr = 0;
    if (mode == SEEK_SET) {
        moveMethod = FILE_BEGIN;
    } else if (mode == SEEK_CUR) {
        moveMethod = FILE_CURRENT;
    } else {
        moveMethod = FILE_END;
    }

    rc = DosSetFilePtr(infoPtr->handle, offset, moveMethod, &newPos);
    if (rc != NO_ERROR) {
#ifdef VERBOSE
        printf("FileSeekProc: DosSetFilePtr handle [%x] ERROR %d\n",
               infoPtr->handle, rc);
        fflush(stdout);
#endif
        TclOS2ConvertError(rc);
        *errorCodePtr = errno;
        return -1;
    }
#ifdef VERBOSE
    printf("FileSeekProc: DosSetFilePtr handle [%x] newPos [%d] OK\n",
           infoPtr->handle, newPos);
    fflush(stdout);
#endif
    return newPos;
}

/*
 *----------------------------------------------------------------------
 *
 * FileSetupProc --
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
FileSetupProc(data, flags)
    ClientData data;            /* Not used. */
    int flags;                  /* Event flags as passed to Tcl_DoOneEvent. */
{
    FileInfo *infoPtr = NULL;
    Tcl_Time blockTime = { 0, 0 };
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (!(flags & TCL_FILE_EVENTS)) {
        return;
    }

    /*
     * Check to see if there is a ready file.  If so, poll.
     */

    for (infoPtr = tsdPtr->firstFilePtr; infoPtr != NULL;
            infoPtr = infoPtr->nextPtr) {
        if (infoPtr->watchMask) {
            Tcl_SetMaxBlockTime(&blockTime);
            break;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FileWatchProc --
 *
 *      Called by the notifier to set up to watch for events on this
 *      channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
FileWatchProc(instanceData, mask)
    ClientData instanceData;		/* The file state. */
    int mask;				/* Events of interest; an OR-ed
                                         * combination of TCL_READABLE,
                                         * TCL_WRITABLE and TCL_EXCEPTION. */
{
    FileInfo *infoPtr = (FileInfo *) instanceData;
    Tcl_Time blockTime = { 0, 0 };

    /*
     * Since the file is always ready for events, we set the block time
     * to zero so we will poll.
     */

    infoPtr->watchMask = mask & infoPtr->validMask;
    if (infoPtr->watchMask) {
        Tcl_SetMaxBlockTime(&blockTime);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpOpenFileChannel --
 *
 *	Open an file based channel on OS/2 systems.
 *
 * Results:
 *	The new channel or NULL. If NULL, the output argument
 *	errorCodePtr is set to a POSIX error and an error message is
 *	left in interp->result if interp is not NULL.
 *
 * Side effects:
 *	May open the channel and may cause creation of a file on the
 *	file system.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
TclpOpenFileChannel(interp, fileName, modeString, permissions)
    Tcl_Interp *interp;			/* Interpreter for error reporting;
                                         * can be NULL. */
    char *fileName;			/* Name of file to open. */
    char *modeString;			/* A list of POSIX open modes or
                                         * a string such as "rw". */
    int permissions;			/* If the open involves creating a
                                         * file, with what modes to create
                                         * it? */
{
    Tcl_Channel channel = 0;
    int seekFlag, mode, channelPermissions = 0;
    HFILE handle;
    ULONG accessMode = 0, createMode, flags, exist, type, attr;
    BOOL readonly = FALSE;
    char *nativeName;
    Tcl_DString ds, buffer;
    char channelName[16 + TCL_INTEGER_SPACE];
    TclFile readFile = NULL;
    TclFile writeFile = NULL;

    mode = TclGetOpenMode(interp, modeString, &seekFlag);
    if (mode == -1) {
        return NULL;
    }

    nativeName = Tcl_TranslateFileName(interp, fileName, &buffer);
    if (nativeName == NULL) {
        return NULL;
    }
    nativeName = Tcl_UtfToExternalDString(NULL, nativeName, -1, &ds);

    /*
     * Hack for compatibility with Windows-oriented scripts: Windows uses
     * eg. "COM1:" for the first serial port, while OS/2 uses the reserved
     * name "COM1" (without ':'). Map the first to the latter.
     * If people have more than 9 comports they can sure make their script
     * have a special case for OS/2.
     */
    if ((nativeName[0] == 'C' || nativeName[0] == 'c') && 
        (stricmp(nativeName, "COM1:")== 0 || stricmp(nativeName, "COM2:")== 0 ||
         stricmp(nativeName, "COM3:")== 0 || stricmp(nativeName, "COM4:")== 0 ||
         stricmp(nativeName, "COM5:")== 0 || stricmp(nativeName, "COM6:")== 0 ||
         stricmp(nativeName, "COM7:")== 0 || stricmp(nativeName, "COM8:")== 0 ||
         stricmp(nativeName, "COM9:")== 0)
       ) {
#ifdef VERBOSE
        printf("Mapping Windows comport %s to OS/2's ", nativeName);
        fflush(stdout);
#endif
        nativeName[4] = '\0';
#ifdef VERBOSE
        printf("%s\n", nativeName);
        fflush(stdout);
#endif
    }

    switch (mode & (O_RDONLY | O_WRONLY | O_RDWR)) {
        case O_RDONLY:
            accessMode = OPEN_ACCESS_READONLY;
            readonly = TRUE; /* Needed because O_A_R is 0 */
            channelPermissions = TCL_READABLE;
            break;
        case O_WRONLY:
            accessMode = OPEN_ACCESS_WRITEONLY;
            channelPermissions = TCL_WRITABLE;
            break;
        case O_RDWR:
            accessMode = OPEN_ACCESS_READWRITE;
            channelPermissions = (TCL_READABLE | TCL_WRITABLE);
            break;
        default:
            panic("TclpOpenFileChannel: invalid mode value");
            break;
    }

    /*
     * Map the creation flags to the OS/2 open mode.
     */

    switch (mode & (O_CREAT | O_EXCL | O_TRUNC)) {
        case (O_CREAT | O_EXCL | O_TRUNC):
            createMode = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_FAIL_IF_EXISTS;
            break;
        case (O_CREAT | O_EXCL):
            createMode = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_FAIL_IF_EXISTS;
            break;
        case (O_CREAT | O_TRUNC):
            createMode = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS;
            break;
        case O_CREAT:
            createMode = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
            break;
        case O_TRUNC:
        case (O_TRUNC | O_EXCL):
            createMode = OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS;
            break;
        default:
            createMode = OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
            break;
    }

    /*
     * If the file is being created, get the file attributes from the
     * permissions argument, else use the existing file attributes.
     */

    if (mode & O_CREAT) {
        if (permissions & S_IWRITE) {
            flags = FILE_NORMAL;
        } else {
            flags = FILE_READONLY;
        }
    } else {
        FILESTATUS3 infoBuf;

        if (DosQueryPathInfo(nativeName, FIL_STANDARD, &infoBuf,
	                     sizeof(infoBuf)) == NO_ERROR) {
            flags = infoBuf.attrFile;
        } else {
            flags = 0;
        }
    }

    /*
     * Set up the file sharing mode.  We want to allow simultaneous access.
     */

    accessMode |= OPEN_SHARE_DENYNONE;

    /*
     * Now we get to create the file.
     */

    rc = DosOpen(nativeName, &handle, &exist, 0, flags, createMode,
                  accessMode, (PEAOP2)NULL);
#ifdef VERBOSE
    if (rc == NO_ERROR) openedFiles++;
    printf("DosOpen [%s]: handle [%x], rc [%d] (create [%x] access [%x])\n",
           nativeName, handle, rc, createMode, accessMode);
    fflush(stdout);
#endif

    if (rc != NO_ERROR) {
        ULONG err = ERROR_SIGNAL_REFUSED;

        switch (rc) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                err = ERROR_FILE_NOT_FOUND;
                break;
            case ERROR_ACCESS_DENIED:
            case ERROR_INVALID_ACCESS:
            case ERROR_SHARING_VIOLATION:
            case ERROR_CANNOT_MAKE:
            case ERROR_OPEN_FAILED:
                err = (mode & O_CREAT) ? ERROR_FILE_EXISTS
                                       : ERROR_FILE_NOT_FOUND;
                break;
        }
        TclOS2ConvertError(err);
        if (interp != (Tcl_Interp *) NULL) {
            Tcl_AppendResult(interp, "couldn't open \"", fileName, "\": ",
                    Tcl_PosixError(interp), (char *) NULL);
        }
        Tcl_DStringFree(&buffer);
        return NULL;
    }

    rc = DosQueryHType(handle, &type, &attr);
    if (rc != NO_ERROR) return NULL;
    switch (type & (HANDTYPE_FILE | HANDTYPE_DEVICE | HANDTYPE_PIPE)) {
    case HANDTYPE_DEVICE:
        type = HANDTYPE_DEVICE;
        break;
    case HANDTYPE_FILE:
        type = HANDTYPE_FILE;
        break;
    case HANDTYPE_PIPE:
        type = HANDTYPE_PIPE;
        break;
    default:
        return NULL;
    }

    /*
     * If the file is a character device, we need to try to figure out
     * whether it is a serial port, a console, or something else. 
     * The source of EMX (emx\src\os2\fileio.c, function new_handle) shows we
     * can find a console (KBD or VIO) by checking for a device driver
     * attribute flag of 3 with a type of HANDTYPE_DEVICE.
     */

    if (type == HANDTYPE_DEVICE) {
        if (attr & 3) {
            type = HANDTYPE_CONSOLE;
        } else {
            /*
             * EMX considers a file a serial port (ASYNC) when the DosDevIOCtl
             * for ASYNC_EXTGETBAUDRATE, ASYNC_GETLINECTRL and ASYNC_GETDCBINFO
             * are all succesful (function query_async in file
             * emx\src\os2\termio.c). These are also necessary to get all the
             * settings you can get in Windows with GetCommState, which the
             * Windows version uses to determine if this is a serial port.
             */
            APIRET ret, ret2, ret3;
            EXTBAUDRATE speed;
            LINECONTROL lineControl;
            DCBINFO dcb;
            ULONG dummy;
            
            dummy = sizeof(speed);
            ret = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_EXTGETBAUDRATE,
                              NULL, 0, NULL, &speed, dummy, &dummy);
            dummy = sizeof(lineControl);
            ret2 = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_GETLINECTRL,
                               NULL, 0, NULL, &lineControl, dummy, &dummy);
            dummy = sizeof(dcb);
            ret3 = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_GETDCBINFO,
                               NULL, 0, NULL, &dcb, dummy, &dummy);
            if ((ret == NO_ERROR) && (ret2 == NO_ERROR) && (ret3 == NO_ERROR)) {
                type = HANDTYPE_SERIAL;
            }
        }
    }

    channel = NULL;

    switch (type)
    {
    case HANDTYPE_SERIAL:
        channel = TclOS2OpenSerialChannel(handle, channelName,
                                          channelPermissions);
        break;
    case HANDTYPE_CONSOLE:
        channel = TclOS2OpenConsoleChannel(handle, channelName,
                                           channelPermissions);
        break;
    case HANDTYPE_PIPE:
        if (channelPermissions & TCL_READABLE)
        {
            readFile = TclOS2MakeFile(handle);
        }
        if (channelPermissions & TCL_WRITABLE)
        {
            writeFile = TclOS2MakeFile(handle);
        }
        channel = TclpCreateCommandChannel(readFile, writeFile, NULL, 0, NULL);
        break;
    case HANDTYPE_DEVICE:
    default:
        channel = TclOS2OpenFileChannel(handle, channelName,
                                        channelPermissions,
                                        (mode & O_APPEND) ? FILE_APPEND : 0);
        break;

    }
    Tcl_DStringFree(&buffer);
    Tcl_DStringFree(&ds);

    if (channel != NULL) {
        if (seekFlag) {
            if (Tcl_Seek(channel, 0, SEEK_END) < 0) {
                if (interp != (Tcl_Interp *) NULL) {
                    Tcl_AppendResult(interp,
                                     "could not seek to end of file on \"",
                                     channelName, "\": ",
                                     Tcl_PosixError(interp), (char *) NULL);
                }
                Tcl_Close(NULL, channel);
                return NULL;
            }
        }
    }

    return channel;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_MakeFileChannel --
 *
 *	Makes a Tcl_Channel from an existing platform specific file handle.
 *
 * Results:
 *	The Tcl_Channel created around the preexisting file.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
Tcl_MakeFileChannel(rawHandle, mode)
    ClientData rawHandle;	/* OS level handle. */
    int mode;			/* ORed combination of TCL_READABLE and
                                 * TCL_WRITABLE to indicate file mode. */
{
    char channelName[16 + TCL_INTEGER_SPACE];
    Tcl_Channel channel = NULL;
    HFILE handle = (HFILE) rawHandle;
    ULONG attr;
    ULONG type;
    TclFile readFile = NULL;
    TclFile writeFile = NULL;

    if (mode == 0) {
        return NULL;
    }

    rc = DosQueryHType(handle, &type, &attr);
    if (rc != NO_ERROR) return NULL;
    switch (type & (HANDTYPE_FILE | HANDTYPE_DEVICE | HANDTYPE_PIPE)) {
    case HANDTYPE_DEVICE:
        type = HANDTYPE_DEVICE;
        break;
    case HANDTYPE_FILE:
        type = HANDTYPE_FILE;
        break;
    case HANDTYPE_PIPE:
        type = HANDTYPE_PIPE;
        break;
    default:
        return NULL;
    }

    /*
     * If the file is a character device, we need to try to figure out
     * whether it is a serial port, a console, or something else. 
     * The source of EMX (emx\src\os2\fileio.c, function new_handle) shows we
     * can find a console (KBD or VIO) by checking for a device driver
     * attribute flag of 3 with a type of HANDTYPE_DEVICE.
     */

    if (type == HANDTYPE_DEVICE) {
        if (attr & 3) {
            type = HANDTYPE_CONSOLE;
        } else {
            /*
             * EMX considers a file a serial port (ASYNC) when the DosDevIOCtl
             * for ASYNC_GETEXTBAUDRATE, ASYNC_GETLINECTRL and ASYNC_GETDCBINFO
             * are all succesful (function query_async in file
             * emx\src\os2\termio.c). These are also necessary to get all the
             * settings you can get in Windows with GetCommState, which the
             * Windows version uses to determine if this is a serial port.
             */
            APIRET ret, ret2, ret3;
            EXTBAUDRATE speed;
            LINECONTROL lineControl;
            DCBINFO dcb;
            ULONG dummy;
            
            dummy = sizeof(speed);
            ret = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_EXTGETBAUDRATE,
                              NULL, 0, NULL, &speed, dummy, &dummy);
            dummy = sizeof(lineControl);
            ret2 = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_GETLINECTRL,
                               NULL, 0, NULL, &lineControl, dummy, &dummy);
            dummy = sizeof(dcb);
            ret3 = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_GETDCBINFO,
                               NULL, 0, NULL, &dcb, dummy, &dummy);
            if ((ret == NO_ERROR) && (ret2 == NO_ERROR) && (ret3 == NO_ERROR)) {
                type = HANDTYPE_SERIAL;
            }
        }
    }

    switch (type)
    {
    case HANDTYPE_SERIAL:
        channel = TclOS2OpenSerialChannel(handle, channelName, mode);
        break;
    case HANDTYPE_CONSOLE:
        channel = TclOS2OpenConsoleChannel(handle, channelName, mode);
        break;
    case HANDTYPE_PIPE:
        if (mode & TCL_READABLE)
        {
            readFile = TclOS2MakeFile(handle);
        }
        if (mode & TCL_WRITABLE)
        {
            writeFile = TclOS2MakeFile(handle);
        }
        channel = TclpCreateCommandChannel(readFile, writeFile, NULL, 0, NULL);
        break;
    case HANDTYPE_DEVICE:
    default:
        channel = TclOS2OpenFileChannel(handle, channelName, mode, 0);
        break;

    }

    return channel;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpGetDefaultStdChannel --
 *
 *	Constructs a channel for the specified standard OS handle.
 *
 * Results:
 *	Returns the specified default standard channel, or NULL.
 *
 * Side effects:
 *	May cause the creation of a standard channel and the underlying
 *	file.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
TclpGetDefaultStdChannel(type)
    int type;			/* One of TCL_STDIN, TCL_STDOUT, TCL_STDERR. */
{
    Tcl_Channel channel = NULL;
    HFILE handle = (HFILE)0L;
    int mode = 0;
    char *bufMode = NULL;

    switch (type) {
        case TCL_STDIN:
            handle = HF_STDIN;
            mode = TCL_READABLE;
            bufMode = "line";
            break;
        case TCL_STDOUT:
            handle = HF_STDOUT;
            mode = TCL_WRITABLE;
            bufMode = "line";
            break;
        case TCL_STDERR:
            handle = HF_STDERR;
            mode = TCL_WRITABLE;
            bufMode = "none";
            break;
        default:
            panic("TclGetDefaultStdChannel: Unexpected channel type");
            break;
    }

    channel = Tcl_MakeFileChannel((ClientData)handle, mode);

    if (channel == NULL) {
        return (Tcl_Channel) NULL;
    }

    /*
     * Set up the normal channel options for stdio handles.
     */

    if ((Tcl_SetChannelOption((Tcl_Interp *) NULL, channel, "-translation",
            "auto") == TCL_ERROR)
            || (Tcl_SetChannelOption((Tcl_Interp *) NULL, channel, "-eofchar",
                    "\032 {}") == TCL_ERROR)
            || (Tcl_SetChannelOption((Tcl_Interp *) NULL, channel,
                    "-buffering", bufMode) == TCL_ERROR)) {
        Tcl_Close((Tcl_Interp *) NULL, channel);
        return (Tcl_Channel) NULL;
    }
    return channel;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2OpenFileChannel --
 *
 *      Constructs a File channel for the specified standard OS handle.
 *      This is a helper function to break up the construction of
 *      channels into File, Console, or Serial.
 *
 * Results:
 *      Returns the new channel, or NULL.
 *
 * Side effects:
 *      May open the channel and may cause creation of a file on the
 *      file system.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
TclOS2OpenFileChannel(handle, channelName, permissions, appendMode)
    HFILE handle;
    char *channelName;
    int permissions;
    int appendMode;
{
    FileInfo *infoPtr;
    ThreadSpecificData *tsdPtr;

    tsdPtr = FileInit();

    /*
     * See if a channel with this handle already exists.
     */

    for (infoPtr = tsdPtr->firstFilePtr; infoPtr != NULL;
            infoPtr = infoPtr->nextPtr) {
        if (infoPtr->handle == (HFILE) handle) {
            return (permissions == infoPtr->validMask)? infoPtr->channel : NULL;
        }
    }

    infoPtr = (FileInfo *) ckalloc((unsigned) sizeof(FileInfo));
    infoPtr->nextPtr = tsdPtr->firstFilePtr;
    tsdPtr->firstFilePtr = infoPtr;
    infoPtr->validMask = permissions;
    infoPtr->watchMask = 0;
    infoPtr->flags = appendMode;
    infoPtr->handle = handle;
    infoPtr->dirty = 0;

    sprintf(channelName, "file%lx", (long) infoPtr);

    infoPtr->channel = Tcl_CreateChannel(&fileChannelType, channelName,
            (ClientData) infoPtr, permissions);

    /*
     * Files have default translation of AUTO and ^Z eof char, which
     * means that a ^Z will be accepted as EOF when reading.
     */

    Tcl_SetChannelOption(NULL, infoPtr->channel, "-translation", "auto");
    Tcl_SetChannelOption(NULL, infoPtr->channel, "-eofchar", "\032 {}");

    return infoPtr->channel;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2WaitForFile --
 *
 *	This procedure waits synchronously for a file to become readable
 *	or writable, with an optional timeout.
 *
 * Results:
 *	The return value is an OR'ed combination of TCL_READABLE,
 *	TCL_WRITABLE, and TCL_EXCEPTION, indicating the conditions
 *	that are present on file at the time of the return.  This
 *	procedure will not return until either "timeout" milliseconds
 *	have elapsed or at least one of the conditions given by mask
 *	has occurred for file (a return value of 0 means that a timeout
 *	occurred).  No normal events will be serviced during the
 *	execution of this procedure.
 *
 * Side effects:
 *	Time passes.
 *
 *----------------------------------------------------------------------
 */

int
TclOS2WaitForFile(handle, mask, timeout)
    HFILE handle;		/* Handle for file on which to wait. */
    int mask;			/* What to wait for: OR'ed combination of
				 * TCL_READABLE, TCL_WRITABLE, and
				 * TCL_EXCEPTION. */
    int timeout;		/* Maximum amount of time to wait for one
				 * of the conditions in mask to occur, in
				 * milliseconds.  A value of 0 means don't
				 * wait at all, and a value of -1 means
				 * wait forever. */
{
    Tcl_Time abortTime, now;
    struct timeval blockTime, *timeoutPtr;
    int numFound, result = 0;
    static fd_set readyMasks[3];
				/* This array reflects the readable/writable
				 * conditions that were found to exist by the
				 * last call to select. */

    /*
     * If there is a non-zero finite timeout, compute the time when
     * we give up.
     */

    if (timeout > 0) {
	TclpGetTime(&now);
	abortTime.sec = now.sec + timeout/1000;
	abortTime.usec = now.usec + (timeout%1000)*1000;
	if (abortTime.usec >= 1000000) {
	    abortTime.usec -= 1000000;
	    abortTime.sec += 1;
	}
	timeoutPtr = &blockTime;
    } else if (timeout == 0) {
	timeoutPtr = &blockTime;
	blockTime.tv_sec = 0;
	blockTime.tv_usec = 0;
    } else {
	timeoutPtr = NULL;
    }

    /*
     * Initialize the ready masks and compute the mask offsets.
     */

    if ((int)handle >= FD_SETSIZE) {
	panic("TclWaitForFile can't handle file id %d", (int)handle);
    }
    FD_ZERO(&readyMasks[0]);
    FD_ZERO(&readyMasks[1]);
    FD_ZERO(&readyMasks[2]);
    
    /*
     * Loop in a mini-event loop of our own, waiting for either the
     * file to become ready or a timeout to occur.
     */

    while (1) {
	if (timeout > 0) {
	    blockTime.tv_sec = abortTime.sec - now.sec;
	    blockTime.tv_usec = abortTime.usec - now.usec;
	    if (blockTime.tv_usec < 0) {
		blockTime.tv_sec -= 1;
		blockTime.tv_usec += 1000000;
	    }
	    if (blockTime.tv_sec < 0) {
		blockTime.tv_sec = 0;
		blockTime.tv_usec = 0;
	    }
	}
	
	/*
	 * Set the appropriate bit in the ready masks for the handle.
	 */

	if (mask & TCL_READABLE) {
	    FD_SET((int)handle, &readyMasks[0]);
	}
	if (mask & TCL_WRITABLE) {
	    FD_SET((int)handle, &readyMasks[1]);
	}
	if (mask & TCL_EXCEPTION) {
	    FD_SET((int)handle, &readyMasks[2]);
	}

	/*
	 * Wait for the event or a timeout.
	 */

	numFound = select(((int)handle)+1, &readyMasks[0], &readyMasks[1],
	                  &readyMasks[2], timeoutPtr);
	if (numFound == 1) {
	    if (FD_ISSET((int)handle, &readyMasks[0])) {
		result |= TCL_READABLE;
	    }
	    if (FD_ISSET((int)handle, &readyMasks[1])) {
		result |= TCL_WRITABLE;
	    }
	    if (FD_ISSET((int)handle, &readyMasks[2])) {
		result |= TCL_EXCEPTION;
	    }
	    result &= mask;
	    if (result) {
		break;
	    }
	}
	if (timeout == 0) {
	    break;
	}

	/*
	 * The select returned early, so we need to recompute the timeout.
	 */

	TclpGetTime(&now);
	if ((abortTime.sec < now.sec)
		|| ((abortTime.sec == now.sec)
		&& (abortTime.usec <= now.usec))) {
	    break;
	}
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2FlushDirtyChannels --
 *
 *      FLush all channels which are dirty, i.e. may have data pending
 *      in the OS.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Flushes file system buffers.
 *
 *----------------------------------------------------------------------
 */

void
TclOS2FlushDirtyChannels ()
{
    FileInfo *infoPtr;
    ThreadSpecificData *tsdPtr;

    tsdPtr = FileInit();

    /*
     * Flush all channels which are dirty, i.e. may have data pending
     * in the OS
     */

    for (infoPtr = tsdPtr->firstFilePtr;
         infoPtr != NULL;
         infoPtr = infoPtr->nextPtr) {
        if (infoPtr->dirty) {
            DosResetBuffer(infoPtr->handle);
            infoPtr->dirty = 0;
        }
    }
}

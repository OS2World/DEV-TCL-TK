/* 
 * tclOS2Serial.c --
 *
 *	This file implements the OS/2-specific serial port functions,
 *	and the "serial" channel driver.
 *
 * Copyright (c) 1999 by Scriptics Corp.
 * Copyright (c) 2002 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tclOS2Serial.c,v 1.4 1999/04/21 21:50:34 rjohnson Exp $
 */

/*
 * Serial ports are manipulated via DosDevIOCtl, category 1h ASYNC (RS232-C)
 * Other categories: 03h (Video), 04h (Keyboard), 05h (Parallel Port),
 * 07h (Mouse), 08h (logical Disk), 09h (Physical Disk), 0Ah (Character Device),
 * 0Bh (General Device), 0Ch (Advanced Power Management), 80h (Screen Control,
 * OEMHLP, Adapter Presence-Check Services, Resource Manager and CD-ROM Drive
 * and Disc) and 81h (Touch Device-Dependent Driver).
 * Summary of Category 01h IOCtl Commands (symbol for category IOCTL_ASYNC):
 * Func  Description                                Symbol
 *  14h  Reserved
 *  34h  Reserved
 *  41h  Set Bit Rate                                 ASYNC_SETBAUDRATE
 *  42h  Set Line Characteristics (stop/parity/data)  ASYNC_SETLINECTRL
 *  43h  Extended Set Bit Rate                        ASYNC_EXTSETBAUDRATE
 *  44h  Transmit Byte Immediate                      ASYNC_TRANSMITIMM
 *  45h  Set Break OFF                                ASYNC_SETBREAKOFF
 *  46h  Set Modem Control Signals                    ASYNC_SETMODEMCTRL
 *  47h  Behave as if XOFF received (Stop transmit)   ASYNC_STOPTRANSMIT
 *  48h  Behave as if XON received (Start transmit)   ASYNC_STARTTRANSMIT
 *  49h  Reserved
 *  4Bh  Set Break ON                                 ASYNC_SETBREAKON
 *  53h  Set Device Control Block (DCB) Parameters    ASYNC_SETDCBINFO
 *  54h  Set Enhanced Mode Parameters                 ASYNC_SETENHANCEDMODEPARMS
 *  61h  Query Current Bit Rate                       ASYNC_GETBAUDRATE
 *  62h  Query Line Characteristics                   ASYNC_GETLINECTRL
 *  63h  Extended Query Bit Rate                      ASYNC_EXTGETBAUDRATE
 *  64h  Query COM Status                             ASYNC_GETCOMMSTATUS
 *  65h  Query Transmit Data Status                   ASYNC_GETLINESTATUS
 *  66h  Query Modem Control Output Signals           ASYNC_GETMODEMOUTPUT
 *  67h  Query Current Modem Input Signals            ASYNC_GETMODEMINPUT
 *  68h  Query Nr of Characters in Receive Queue      ASYNC_GETINQUECOUNT
 *  69h  Query Nr of Characters in Transmit Queue     ASYNC_GETOUTQUECOUNT
 *  6Dh  Query COM Error                              ASYNC_GETCOMMERROR
 *  72h  Query COM Event Information                  ASYNC_GETCOMMEVENT
 *  73h  Query Device Control Block (DCB) Parms       ASYNC_GETDCBINFO
 *  74h  Query Enhanced Mode Parameters               ASYNC_GETENHANCEDMODEPARMS
 *
 * To get the DosDevIOCtl declarations, we need to define INCL_DOSDEVIOCTL
 * before including os2.h, ie. before including tclOS2Int.h.
 */

#define INCL_DOSDEVIOCTL
#include        "tclOS2Int.h"
#undef INCL_DOSDEVIOCTL

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

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
 * The following variable is used to tell whether this module has been
 * initialized.
 */

static int initialized = 0;

/*
 * Bit masks used in the flags field of the SerialInfo structure below.
 */

#define SERIAL_PENDING	(1<<0)	/* Message is pending in the queue. */
#define SERIAL_ASYNC	(1<<1)	/* Channel is non-blocking. */

/*
 * Bit masks used in the sharedFlags field of the SerialInfo structure below.
 */

#define SERIAL_EOF	 (1<<2)  /* Serial has reached EOF. */
#define SERIAL_ERROR     (1<<4)
#define SERIAL_WRITE     (1<<5)  /* enables fileevent writable
                                  * one time after write operation */

/*
 * Default time to block between checking status on the serial port.
 */
#define SERIAL_DEFAULT_BLOCKTIME    10  /* 10 msec */

/*
 * Define OS/2 read/write error masks returned by ASYNC_GETCOMMERROR
 */
#define SERIAL_READ_ERRORS      ( RX_QUE_OVERRUN | RX_HARDWARE_OVERRUN \
                                | PARITY_ERROR | FRAMING_ERROR )
#define SERIAL_WRITE_ERRORS     ( 0 )
    
/*
 * This structure describes per-instance data for a serial based channel.
 */

typedef struct SerialInfo {
    HFILE handle;
    struct SerialInfo *nextPtr;	/* Pointer to next registered serial. */
    Tcl_Channel channel;	/* Pointer to channel structure. */
    int validMask;		/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
				 * which operations are valid on the file. */
    int watchMask;		/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, or TCL_EXCEPTION: indicates
				 * which events should be reported. */
    int flags;			/* State flags, see above for a list. */
    int writable;               /* flag that the channel is readable */
    int readable;               /* flag that the channel is readable */
    int blockTime;              /* max. blocktime in msec */
    USHORT error;               /* pending error code from DosDevIOCtl 
                                 * ASYNC_GETCOMMERROR */
    USHORT lastError;           /* last error code, can be fetched with
                                 * fconfigure chan -lasterror */
    
} SerialInfo;

typedef struct ThreadSpecificData {
    /*
     * The following pointer refers to the head of the list of serials
     * that are being watched for file events.
     */
    
    SerialInfo *firstSerialPtr;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

/*
 * The following structure is what is added to the Tcl event queue when
 * serial events are generated.
 */

typedef struct SerialEvent {
    Tcl_Event header;		/* Information that is standard for
				 * all events. */
    SerialInfo *infoPtr;	/* Pointer to serial info structure.  Note
				 * that we still have to verify that the
				 * serial exists before dereferencing this
				 * pointer. */
} SerialEvent;

/*
 * Declarations for functions used only in this file.
 */

static int		SerialBlockProc(ClientData instanceData, int mode);
static void		SerialCheckProc(ClientData clientData, int flags);
static int		SerialCloseProc(ClientData instanceData,
			    Tcl_Interp *interp);
static int		SerialEventProc(Tcl_Event *evPtr, int flags);
static void		SerialExitHandler(ClientData clientData);
static int		SerialGetHandleProc(ClientData instanceData,
			    int direction, ClientData *handlePtr);
static ThreadSpecificData *SerialInit(void);
static int		SerialInputProc(ClientData instanceData, char *buf,
			    int toRead, int *errorCode);
static int		SerialOutputProc(ClientData instanceData, char *buf,
			    int toWrite, int *errorCode);
static void		SerialSetupProc(ClientData clientData, int flags);
static void		SerialWatchProc(ClientData instanceData, int mask);
static void		ProcExitHandler(ClientData clientData);
static void		SerialBlockTime(int msec);
static int	     SerialGetOptionProc _ANSI_ARGS_((ClientData instanceData, 
			    Tcl_Interp *interp, char *optionName,
			    Tcl_DString *dsPtr));
static int	     SerialSetOptionProc _ANSI_ARGS_((ClientData instanceData,
			    Tcl_Interp *interp, char *optionName, 
			    char *value));

/*
 * This structure describes the channel type structure for command serial
 * based IO.
 */

static Tcl_ChannelType serialChannelType = {
    "serial",                   /* Type name. */
    TCL_CHANNEL_VERSION_2,      /* v2 channel */
    SerialCloseProc,            /* Close proc. */
    SerialInputProc,            /* Input proc. */
    SerialOutputProc,           /* Output proc. */
    NULL,                       /* Seek proc. */
    SerialSetOptionProc,        /* Set option proc. */
    SerialGetOptionProc,        /* Get option proc. */
    SerialWatchProc,            /* Set up notifier to watch the channel. */
    SerialGetHandleProc,        /* Get an OS handle from channel. */
    NULL,                       /* close2proc. */
    SerialBlockProc,            /* Set blocking or non-blocking mode.*/
    NULL,                       /* flush proc. */
    NULL,                       /* handler proc. */
};

/*
 *----------------------------------------------------------------------
 *
 * SerialInit --
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
SerialInit()
{
    ThreadSpecificData *tsdPtr;

    /*
     * Check the initialized flag first, then check it again in the mutex.
     * This is a speed enhancement.
     */
    
    if (!initialized) {
        initialized = 1;
        Tcl_CreateExitHandler(ProcExitHandler, NULL);
    }

    tsdPtr = (ThreadSpecificData *)TclThreadDataKeyGet(&dataKey);
    if (tsdPtr == NULL) {
	tsdPtr = TCL_TSD_INIT(&dataKey);
	tsdPtr->firstSerialPtr = NULL;
	Tcl_CreateEventSource(SerialSetupProc, SerialCheckProc, NULL);
	Tcl_CreateThreadExitHandler(SerialExitHandler, NULL);
    }
    return tsdPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * SerialExitHandler --
 *
 *	This function is called to cleanup the serial module before
 *	Tcl is unloaded.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the serial event source.
 *
 *----------------------------------------------------------------------
 */

static void
SerialExitHandler(
    ClientData clientData)	/* Old window proc */
{
    Tcl_DeleteEventSource(SerialSetupProc, SerialCheckProc, NULL);
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
    initialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * SerialBlockTime --
 *
 *  Wrapper to set Tcl's block time in msec
 *
 * Results:
 *  None.
 *----------------------------------------------------------------------
 */

void
SerialBlockTime(
    int msec)          /* milli-seconds */
{
    Tcl_Time blockTime;

    blockTime.sec  =  msec / 1000;
    blockTime.usec = (msec % 1000) * 1000;
    Tcl_SetMaxBlockTime(&blockTime);
}

/*
 *----------------------------------------------------------------------
 *
 * SerialSetupProc --
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
SerialSetupProc(
    ClientData data,		/* Not used. */
    int flags)			/* Event flags as passed to Tcl_DoOneEvent. */
{
    SerialInfo *infoPtr;
    int block = 1;
    int msec = INT_MAX; /* min. found block time */
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (!(flags & TCL_FILE_EVENTS)) {
	return;
    }
    
    /*
     * Look to see if any events are already pending.  If they are, poll.
     */

    for (infoPtr = tsdPtr->firstSerialPtr; infoPtr != NULL; 
	    infoPtr = infoPtr->nextPtr) {

        if (infoPtr->watchMask & (TCL_WRITABLE | TCL_READABLE) ) {
            block = 0;
            if (infoPtr->blockTime < msec) {
                msec = infoPtr->blockTime;
            }
        }
    }
    if (!block) {
	SerialBlockTime(msec);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SerialCheckProc --
 *
 *	This procedure is called by Tcl_DoOneEvent to check the serial
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
SerialCheckProc(
    ClientData data,		/* Not used. */
    int flags)			/* Event flags as passed to Tcl_DoOneEvent. */
{
    SerialInfo *infoPtr;
    SerialEvent *evPtr;
    int needEvent;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
    ULONG dummy;

    if (!(flags & TCL_FILE_EVENTS)) {
	return;
    }
    
    /*
     * Queue events for any ready serials that don't already have events
     * queued.
     */

    for (infoPtr = tsdPtr->firstSerialPtr; infoPtr != NULL; 
	    infoPtr = infoPtr->nextPtr) {
	if (infoPtr->flags & SERIAL_PENDING) {
	    continue;
	}
	
	needEvent = 0;

        /*
         * If any READABLE or WRITABLE watch mask is set
         * clear communications errors, errors are ignored here
         */

        if (infoPtr->watchMask & (TCL_WRITABLE | TCL_READABLE) ) {
            dummy = sizeof(infoPtr->error);
            if (DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_GETCOMMERROR,
                            NULL,0,NULL, (PVOID)&infoPtr->error, dummy, &dummy)
                == NO_ERROR) {
                /*
                 * Look for empty output buffer.  If empty, poll.
                 */

                if (infoPtr->watchMask & TCL_WRITABLE ) {
                    APIRET ret;
                    RXQUEUE outQueue;
                    /*
                     * force fileevent after serial write error
                     */
                    dummy = sizeof(outQueue);
                    ret= DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC,
                                     ASYNC_GETOUTQUECOUNT, NULL, 0, NULL,
                                     (PVOID)&outQueue, dummy, &dummy);
                    if (((infoPtr->flags & SERIAL_WRITE) != 0) &&
                            ((outQueue.cch == 0) ||
                                    (infoPtr->error & ERROR_OCCURRED))) {
                        /*
                         * allow only one fileevent after each callback
                         */

                        infoPtr->flags &= ~SERIAL_WRITE;
                        infoPtr->writable = 1;
                        needEvent = 1;
                    }
                }

                /*
                 * Look for characters already pending in windows queue.
                 * If they are, poll.
                 */

		if (infoPtr->watchMask & TCL_READABLE ) {
                    APIRET ret;
                    RXQUEUE inQueue;
                    /*
                     * force fileevent after serial read error
                     */
                    dummy = sizeof(inQueue);
                    ret= DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC,
                                     ASYNC_GETINQUECOUNT, NULL, 0, NULL,
                                     (PVOID)&inQueue, dummy, &dummy);
                    if ((inQueue.cch == 0) ||
                            (infoPtr->error & SERIAL_READ_ERRORS) ) {
                        infoPtr->readable = 1;
                        needEvent = 1;
                    }
                }
            }
        }

        /*
         * Queue an event if the serial is signaled for reading or writing.
         */

        if (needEvent) {
            infoPtr->flags |= SERIAL_PENDING;
            evPtr = (SerialEvent *) ckalloc(sizeof(SerialEvent));
            evPtr->header.proc = SerialEventProc;
            evPtr->infoPtr = infoPtr;
            Tcl_QueueEvent((Tcl_Event *) evPtr, TCL_QUEUE_TAIL);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SerialBlockProc --
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
SerialBlockProc(
    ClientData instanceData,	/* Instance data for channel. */
    int mode)			/* TCL_MODE_BLOCKING or
                                 * TCL_MODE_NONBLOCKING. */
{
    DCBINFO dcbInfo;
    ULONG dummy;
    APIRET ret;
    int errorCode = 0;

    SerialInfo *infoPtr = (SerialInfo *) instanceData;
    
    dummy = sizeof(dcbInfo);
    DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_GETDCBINFO, NULL, 0, NULL,
                &dcbInfo, dummy, &dummy);
    if (mode == TCL_MODE_NONBLOCKING) {
	infoPtr->flags |= SERIAL_ASYNC;
        /*
         * Set 0 timeout for async, timeout 0 means .01 seconds
         * First unset whatever's set in the mode bits
         * This requires knowledge of the symbol that uses all bits.
         */
        dcbInfo.fbTimeout &= ~(MODE_NO_WRITE_TIMEOUT|MODE_NOWAIT_READ_TIMEOUT);
        /* Use write timeout for timeout value */
        dcbInfo.fbTimeout &= ~MODE_NO_WRITE_TIMEOUT;
        dcbInfo.usWriteTimeout = 0;
        /*
         * No-wait timeout processing causes the PDD not to wait for any
         * data to become available.
         */
        dcbInfo.fbTimeout |= MODE_NOWAIT_READ_TIMEOUT;
    } else {
	infoPtr->flags &= ~(SERIAL_ASYNC);
        /*
         * Set maximum timeout for sync
         * First unset whatever's set in the mode bits
         * This requires knowledge of the symbol that uses all bits.
         */
        dcbInfo.fbTimeout &= ~(MODE_NO_WRITE_TIMEOUT|MODE_NOWAIT_READ_TIMEOUT);
        /* Infinite timeout, regardless of write timeout value */
        dcbInfo.fbTimeout |= MODE_NO_WRITE_TIMEOUT;
        /*
         * No infinite (ie. blocking) read timeout available use max.
         * Normal timeout processing restarts timeout every time something
         * has been read (with some exceptions). Wait-for-something starts
         * the timeout when no-wait would return but will never wait longer
         * than this one timeout => use normal processing.
         */
        dcbInfo.fbTimeout |= MODE_READ_TIMEOUT;
        dcbInfo.usReadTimeout = USHRT_MAX;
    }
    dummy = sizeof(dcbInfo);
    ret = DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_SETDCBINFO,
                      &dcbInfo, dummy, &dummy, NULL, 0, NULL);
    if (ret != NO_ERROR) {
        TclOS2ConvertError(ret);
        errorCode = errno;
    }
    return errorCode;
}

/*
 *----------------------------------------------------------------------
 *
 * SerialCloseProc --
 *
 *	Closes a serial based IO channel.
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
SerialCloseProc(
    ClientData instanceData,	/* Pointer to SerialInfo structure. */
    Tcl_Interp *interp)		/* For error reporting. */
{
    APIRET ret;
    SerialInfo *serialPtr = (SerialInfo *) instanceData;
    int errorCode, result = 0;
    SerialInfo *infoPtr, **nextPtrPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    errorCode = 0;
    serialPtr->validMask &= ~TCL_READABLE;
    serialPtr->validMask &= ~TCL_WRITABLE;

    /*
     * Do not close standard channels while in thread-exit to prevent one
     * thread killing the stdio of another.
     */

    if (!TclInExit()
        || ((serialPtr->handle != 0)
            && (serialPtr->handle != 1)
            && (serialPtr->handle != 2))) {
        if (serialPtr->handle != NULLHANDLE) {
            ret = DosClose(serialPtr->handle);
#ifdef VERBOSE
            printf("SerialCloseProc DosClose[%d] %d\n", serialPtr->handle, ret);
            fflush(stdout);
#endif
            if (ret != NO_ERROR) {
                TclOS2ConvertError(ret);
                errorCode = errno;
            }
        }
    }
    
    serialPtr->watchMask &= serialPtr->validMask;

    /*
     * Remove the file from the list of watched files.
     */

    for (nextPtrPtr = &(tsdPtr->firstSerialPtr), infoPtr = *nextPtrPtr;
	    infoPtr != NULL;
	    nextPtrPtr = &infoPtr->nextPtr, infoPtr = *nextPtrPtr) {
	if (infoPtr == (SerialInfo *)serialPtr) {
	    *nextPtrPtr = infoPtr->nextPtr;
	    break;
	}
    }

    /*
     * Wrap the error file into a channel and give it to the cleanup
     * routine. 
     */

    ckfree((char*) serialPtr);

    if (errorCode == 0) {
        return result;
    }
    return errorCode;
}

/*
 *----------------------------------------------------------------------
 *
 * SerialInputProc --
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
SerialInputProc(
    ClientData instanceData,		/* Serial state. */
    char *buf,				/* Where to store data read. */
    int bufSize,			/* How much space is available
                                         * in the buffer? */
    int *errorCode)			/* Where to store error code. */
{
    APIRET ret;
    SerialInfo *infoPtr = (SerialInfo *) instanceData;
    ULONG bytesRead = 0;
    ULONG dummy;

    *errorCode = 0;

    /*
     * Check if there is a CommError pending from SerialCheckProc
     */
    if (infoPtr->error & SERIAL_READ_ERRORS ){
        goto commError;
    }

    /*
     * Look for characters already pending in windows queue.
     * This is the mainly restored good old code from Tcl8.0
     */

    dummy = sizeof(infoPtr->error);
    if (DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_GETCOMMERROR,
                    NULL, 0, NULL, (PVOID)&infoPtr->error, dummy, &dummy)
        == NO_ERROR) {
        RXQUEUE inQueue;
        /*
         * Check for errors here, but not in the evSetup/Check procedures
         */

        if (infoPtr->error & SERIAL_READ_ERRORS) {
            goto commError;
        }
        dummy = sizeof(inQueue);
        ret= DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_GETINQUECOUNT,
                         NULL, 0, NULL, (PVOID)&inQueue, dummy, &dummy);
        if (infoPtr->flags & SERIAL_ASYNC) {
            /*
             * NON_BLOCKING mode:
             * Avoid blocking by reading more bytes than available
             * in input buffer
             */

            if (inQueue.cch > 0) {
                if ((ULONG)bufSize > inQueue.cch) {
                    bufSize = inQueue.cch;
                }
            } else {
                errno = *errorCode = EAGAIN;
                return -1;
            }
        } else {
            /*
             * BLOCKING mode:
             * Tcl trys to read a full buffer of 4 kBytes here
             */

            if (inQueue.cch > 0) {
                if ((ULONG)bufSize > inQueue.cch) {
                    bufSize = inQueue.cch;
                }
            } else {
                bufSize = 1;
            }
        }
    }

    if (bufSize == 0) {
        return bytesRead = 0;
    }
    
    ret = DosRead(infoPtr->handle, (PVOID) buf, (ULONG) bufSize, &bytesRead);
    if (ret != NO_ERROR) {
	if (ret != ERROR_MORE_DATA) {
	    goto error;
	}
    }
	
    return bytesRead;
	
    error:
    TclOS2ConvertError(ret);
    *errorCode = errno;
    return -1;

    commError:
    infoPtr->lastError = infoPtr->error;  /* save last error code */
    infoPtr->error = 0;                   /* reset error code */
    *errorCode = EIO;                     /* to return read-error only once */
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * SerialOutputProc --
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
SerialOutputProc(
    ClientData instanceData,		/* Serial state. */
    char *buf,				/* The data buffer. */
    int toWrite,			/* How many bytes to write? */
    int *errorCode)			/* Where to store error code. */
{
    APIRET ret;
    SerialInfo *infoPtr = (SerialInfo *) instanceData;
    ULONG bytesWritten;

    *errorCode = 0;

    /*
     * Check if there is a CommError pending from SerialCheckProc
     */
    if (infoPtr->error & SERIAL_WRITE_ERRORS ){
        infoPtr->lastError = infoPtr->error;  /* save last error code */
        infoPtr->error = 0;                   /* reset error code */
        *errorCode = EIO;               /* to return read-error only once */
        return -1;
    }

    /*
     * Check for a background error on the last write.
     * Allow one write-fileevent after each callback
     */

    if (toWrite) {
        infoPtr->flags |= SERIAL_WRITE;
    }

    ret = DosWrite(infoPtr->handle,(PVOID) buf, (ULONG) toWrite, &bytesWritten);
    if (ret != NO_ERROR) {
        if (ret != ERROR_MORE_DATA) {
            TclOS2ConvertError(ret);
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
 * SerialEventProc --
 *
 *	This function is invoked by Tcl_ServiceEvent when a file event
 *	reaches the front of the event queue.  This procedure invokes
 *	Tcl_NotifyChannel on the serial.
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
SerialEventProc(
    Tcl_Event *evPtr,		/* Event to service. */
    int flags)			/* Flags that indicate what events to
				 * handle, such as TCL_FILE_EVENTS. */
{
    SerialEvent *serialEvPtr = (SerialEvent *)evPtr;
    SerialInfo *infoPtr;
    int mask;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (!(flags & TCL_FILE_EVENTS)) {
	return 0;
    }

    /*
     * Search through the list of watched serials for the one whose handle
     * matches the event.  We do this rather than simply dereferencing
     * the handle in the event so that serials can be deleted while the
     * event is in the queue.
     */

    for (infoPtr = tsdPtr->firstSerialPtr; infoPtr != NULL;
	    infoPtr = infoPtr->nextPtr) {
	if (serialEvPtr->infoPtr == infoPtr) {
	    infoPtr->flags &= ~(SERIAL_PENDING);
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
     * Check to see if the serial is readable.  Note
     * that we can't tell if a serial is writable, so we always report it
     * as being writable unless we have detected EOF.
     */

    mask = 0;
    if (infoPtr->watchMask & TCL_WRITABLE) {
        if (infoPtr->writable) {
            mask |= TCL_WRITABLE;
            infoPtr->writable = 0;
        }
    }

    if (infoPtr->watchMask & TCL_READABLE) {
        if (infoPtr->readable) {
            mask |= TCL_READABLE;
            infoPtr->readable = 0;
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
 * SerialWatchProc --
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
SerialWatchProc(
    ClientData instanceData,		/* Serial state. */
    int mask)				/* What events to watch for, OR-ed
                                         * combination of TCL_READABLE,
                                         * TCL_WRITABLE and TCL_EXCEPTION. */
{
    SerialInfo **nextPtrPtr, *ptr;
    SerialInfo *infoPtr = (SerialInfo *) instanceData;
    int oldMask = infoPtr->watchMask;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    /*
     * Since the file is always ready for events, we set the block time
     * to zero so we will poll.
     */

    infoPtr->watchMask = mask & infoPtr->validMask;
    if (infoPtr->watchMask) {
	if (!oldMask) {
	    infoPtr->nextPtr = tsdPtr->firstSerialPtr;
	    tsdPtr->firstSerialPtr = infoPtr;
	}
	SerialBlockTime(infoPtr->blockTime);
    } else {
	if (oldMask) {
	    /*
	     * Remove the serial port from the list of watched serial ports.
	     */

	    for (nextPtrPtr = &(tsdPtr->firstSerialPtr), ptr = *nextPtrPtr;
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
 * SerialGetHandleProc --
 *
 *	Called from Tcl_GetChannelHandle to retrieve OS handles from
 *	inside a command serial port based channel.
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
SerialGetHandleProc(
    ClientData instanceData,	/* The serial state. */
    int direction,		/* TCL_READABLE or TCL_WRITABLE */
    ClientData *handlePtr)	/* Where to store the handle.  */
{
    SerialInfo *infoPtr = (SerialInfo *) instanceData;

    *handlePtr = (ClientData) infoPtr->handle;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2OpenSerialChannel --
 *
 *	Constructs a Serial port channel for the specified standard OS handle.
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
TclOS2OpenSerialChannel(handle, channelName, permissions)
    HFILE handle;
    char *channelName;
    int permissions;
{
    APIRET ret;
    SerialInfo *infoPtr;
    DCBINFO dcb;
    ULONG parmLen;
    ThreadSpecificData *tsdPtr;

    tsdPtr = SerialInit();

    /*
     * Initialize the com port.
     * GETDCBINFO should be used before SETDCBINFO to set presently
     * reserved fields correctly when the application is not aware of them.
     */

    parmLen = sizeof(dcb);
    ret = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_GETDCBINFO, NULL, 0L, NULL,
                      (PULONG)&dcb, sizeof(dcb), &parmLen);
    /*
     * The Windows port set the following:
     * Monitoring of events: character received and put in input buffer.
     * Set (request) input and output buffers to 4096 bytes.
     * Terminate all outstanding read and write operations and clear any
     * input and output buffer.
     * Maximum time (ms) allowed between arrival of two characters: MAXDWORD
     * Multiplier (ms) per byte to be read to determine total time-out: MAXDWORD
     * Constant (ms), added to multiplier * nr. of bytes to be read: 1
     * No timeout for write (blocking).
     */

#ifdef VERBOSE
    printf("previous fbTimeout %x read %d write %d\n", dcb.fbTimeout,
           dcb.usReadTimeout, dcb.usWriteTimeout);
    fflush(stdout);
#endif
    /*
     * First unset whatever's set in the mode bits
     * This requires knowledge of the symbol that uses all bits.
     */
    dcb.fbTimeout &= ~(MODE_NO_WRITE_TIMEOUT | MODE_NOWAIT_READ_TIMEOUT);
    /* Now choose the new bits */
    dcb.fbTimeout |= MODE_NO_WRITE_TIMEOUT | MODE_READ_TIMEOUT;
    dcb.usReadTimeout = USHRT_MAX;

    parmLen = sizeof(dcb);
    ret = DosDevIOCtl(handle, IOCTL_ASYNC, ASYNC_SETDCBINFO,
                      (PULONG)&dcb, sizeof(dcb), &parmLen, NULL, 0L, NULL);
    
    infoPtr = (SerialInfo *) ckalloc((unsigned) sizeof(SerialInfo));
    memset(infoPtr, 0, sizeof(SerialInfo));
    
    infoPtr->validMask = permissions;
    infoPtr->handle = handle;
	
    /*
     * Use the pointer to keep the channel names unique, in case
     * the handles are shared between multiple channels (stdin/stdout).
     */

    sprintf(channelName, "file%lx", (long int) infoPtr);
	
    infoPtr->channel = Tcl_CreateChannel(&serialChannelType, channelName,
            (ClientData) infoPtr, permissions);

    infoPtr->readable = infoPtr->writable = 0;
    infoPtr->blockTime = SERIAL_DEFAULT_BLOCKTIME;
    infoPtr->lastError = infoPtr->error = 0;

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
 * SerialErrorStr --
 *
 *  Converts an OS/2 serial error code to a list of readable errors
 *
 *----------------------------------------------------------------------
 */
static void
SerialErrorStr(error, dsPtr)
    USHORT error;          /* OS/2 serial error code */
    Tcl_DString *dsPtr;    /* Where to store string */
{
    if( (error & RX_QUE_OVERRUN) != 0) {
        Tcl_DStringAppendElement(dsPtr, "QUE_OVERRUN");
    }
    if( (error & RX_HARDWARE_OVERRUN) != 0) {
        Tcl_DStringAppendElement(dsPtr, "HARDWARE_OVERRUN");
    }
    if( (error & PARITY_ERROR) != 0) {
        Tcl_DStringAppendElement(dsPtr, "PARITY_ERROR");
    }
    if( (error & FRAMING_ERROR) != 0) {
        Tcl_DStringAppendElement(dsPtr, "FRAMING_ERROR");
    }
    if( (error & ~(SERIAL_READ_ERRORS | SERIAL_WRITE_ERRORS)) != 0) {
        char buf[TCL_INTEGER_SPACE + 1];
        sprintf(buf, "%d", error);
        Tcl_DStringAppendElement(dsPtr, buf);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SerialSetOptionProc --
 *
 *	Sets an option on a channel.
 *
 * Results:
 *	A standard Tcl result. Also sets the interp's result on error if
 *	interp is not NULL.
 *
 * Side effects:
 *	May modify an option on a device.
 *
 *----------------------------------------------------------------------
 */

static int		
SerialSetOptionProc(instanceData, interp, optionName, value)
    ClientData instanceData;	/* File state. */
    Tcl_Interp *interp;		/* For error reporting - can be NULL. */
    char *optionName;		/* Which option to set? */
    char *value;		/* New value for option. */
{
    APIRET ret, ret2;
    SerialInfo *infoPtr;
    ULONG dummy;
    int len, result;
    Tcl_DString ds;
    LINECONTROL lineControl;
    EXTBAUDRATE speed;

    infoPtr = (SerialInfo *) instanceData;

    len = strlen(optionName);
    if ((len > 1) && (strncmp(optionName, "-mode", len) == 0)) {
        dummy = sizeof(speed);
        ret = DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_EXTGETBAUDRATE,
                          NULL, 0, NULL, &speed, dummy, &dummy);
        dummy = sizeof(lineControl);
        ret2 = DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_GETLINECTRL,
                           NULL, 0, NULL, &lineControl, dummy, &dummy);
        if (ret == NO_ERROR && ret2 == NO_ERROR) {
            char *native;
            char parity;
            float stopBits;
            /*
             * value contains string of form
             *     1200,N,8,1
             * extract them en set them via DosDevIOCtl's
             * ASYNC_EXTSETBAUDRATE and ASYNC_SETLINECTRL.
             */
	    native = Tcl_UtfToExternalDString(NULL, value, -1, &ds);
	    result = sscanf(native, "%ld,%c,%hd,%f", &(speed.ulCurrentRate),
                            &parity, (unsigned short*) &(lineControl.bDataBits),
                            &stopBits);
	    Tcl_DStringFree(&ds);

	    if (result == EOF) {
                if (interp) {
                    Tcl_AppendResult(interp, "bad value for -mode: should be ",
			    "baud,parity,data,stop", NULL);
		}
		return TCL_ERROR;
	    } else {
                switch (tolower(parity)) {
                    case 'n': lineControl.bParity= 0x0; break;
                    case 'o': lineControl.bParity= 0x1; break;
                    case 'e': lineControl.bParity= 0x2; break;
                    case 'm': lineControl.bParity= 0x3; break;
                    case 's': lineControl.bParity= 0x4; break;
                    default:
                        if (interp) {
                            Tcl_AppendResult(interp, "bad parity value for ",
                                    " -mode: should be 'n', 'o', 'e', 'm' or ",
                                    "'s'", NULL);
		        }
		        return TCL_ERROR;
                }
	        lineControl.bStopBits = (stopBits == 2) ? 0x2 : 
		                         (stopBits == 1.5) ? 0x1 : 0x0;
                dummy = sizeof(speed);
                ret = DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC,
                                  ASYNC_EXTSETBAUDRATE, &speed, dummy, &dummy,
                                  NULL, 0, NULL);
                dummy = sizeof(lineControl);
                ret2 = DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC,
                                   ASYNC_SETLINECTRL,
                                   &lineControl, dummy, &dummy, NULL, 0, NULL);
	        if (ret != NO_ERROR || ret2 != NO_ERROR) {
	            if (interp) {
		        Tcl_AppendResult(interp, "can't set comm state", NULL);
	            }
		    return TCL_ERROR;
	        } else {
		    return TCL_OK;
	        }
	    }
	} else {
	    if (interp) {
		Tcl_AppendResult(interp, "can't get comm state", NULL);
	    }
	    return TCL_ERROR;
	}
    } else {
	return Tcl_BadChannelOption(interp, optionName, "mode");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SerialGetOptionProc --
 *
 *	Gets a mode associated with an IO channel. If the optionName arg
 *	is non NULL, retrieves the value of that option. If the optionName
 *	arg is NULL, retrieves a list of alternating option names and
 *	values for the given channel.
 *
 * Results:
 *	A standard Tcl result. Also sets the supplied DString to the
 *	string value of the option(s) returned.
 *
 * Side effects:
 *	The string returned by this function is in static storage and
 *	may be reused at any time subsequent to the call.
 *
 *----------------------------------------------------------------------
 */
static int		
SerialGetOptionProc(instanceData, interp, optionName, dsPtr)
    ClientData instanceData;	/* File state. */
    Tcl_Interp *interp;          /* For error reporting - can be NULL. */
    char *optionName;		/* Option to get. */
    Tcl_DString *dsPtr;		/* Where to store value(s). */
{
    APIRET ret, ret2;
    SerialInfo *infoPtr;
    LINECONTROL lineControl;
    EXTBAUDRATE speed;
    size_t len;
    int valid = 0;  /* flag if valid option parsed */
    ULONG dummy;

    infoPtr = (SerialInfo *) instanceData;

    if (optionName == NULL) {
	Tcl_DStringAppendElement(dsPtr, "-mode");
	len = 0;
    } else {
	len = strlen(optionName);
    }
    if ((len == 0) || 
	    ((len > 1) && (strncmp(optionName, "-mode", len) == 0))) {
        dummy = sizeof(speed);
        ret = DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_EXTGETBAUDRATE,
                          NULL, 0, NULL, &speed, dummy, &dummy);
        dummy = sizeof(lineControl);
        ret2 = DosDevIOCtl(infoPtr->handle, IOCTL_ASYNC, ASYNC_GETLINECTRL,
                           NULL, 0, NULL, &lineControl, dummy, &dummy);
        if (ret != NO_ERROR || ret2 != NO_ERROR) {
	    /*
	     * shouldn't we flag an error instead ? 
	     */

	    Tcl_DStringAppendElement(dsPtr, "");

	} else {
	    char parity;
	    char *stop;
	    char buf[2 * TCL_INTEGER_SPACE + 16];

	    parity = 'n';
	    if (lineControl.bParity < 4) {
		parity = "noems"[lineControl.bParity];
	    }

	    stop = (lineControl.bStopBits == 0x2) ? "2" : 
		    (lineControl.bStopBits == 0x1) ? "1.5" : "1";

	    sprintf(buf, "%ld,%c,%d,%s", speed.ulCurrentRate, parity,
                    lineControl.bDataBits, stop);
	    Tcl_DStringAppendElement(dsPtr, buf);
	}
    }

    /*
     * get option -pollinterval
     */

    if (len == 0) {
        Tcl_DStringAppendElement(dsPtr, "-pollinterval");
    }
    if ((len == 0) ||
        ((len > 1) && (strncmp(optionName, "-pollinterval", len) == 0))) {
        char buf[TCL_INTEGER_SPACE + 1];

        valid = 1;
        sprintf(buf, "%d", infoPtr->blockTime);
        Tcl_DStringAppendElement(dsPtr, buf);
    }

    /*
     * get option -lasterror
     * option is readonly and returned by [fconfigure chan -lasterror]
     * but not returned by unnamed [fconfigure chan]
     */

    if ( (len > 1) && (strncmp(optionName, "-lasterror", len) == 0) ) {
        valid = 1;
        SerialErrorStr(infoPtr->lastError, dsPtr);
    }

    if (valid) {
        return TCL_OK;
    } else {
        return Tcl_BadChannelOption(interp, optionName,
                "mode pollinterval lasterror");
    }
}

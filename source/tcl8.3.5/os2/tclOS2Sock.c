/* 
 * tclOS2Sock.c --
 *
 *	This file contains OS/2-specific socket related code.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tclOS2Int.h"

/*
 * There is no portable macro for the maximum length
 * of host names returned by gethostbyname().  We should only
 * trust SYS_NMLN if it is at least 255 + 1 bytes to comply with DNS
 * host name limits.
 *
 * Note:  SYS_NMLN is a restriction on "uname" not on gethostbyname!
 *
 * For example HP-UX 10.20 has SYS_NMLN == 9,  while gethostbyname()
 * can return a fully qualified name from DNS of up to 255 bytes.
 *
 * Fix suggested by Viktor Dukhovni (viktor@esm.com)
 */

#if defined(SYS_NMLN) && SYS_NMLEN >= 256
#define TCL_HOSTNAME_LEN SYS_NMLEN
#else
#define TCL_HOSTNAME_LEN 256
#endif

/*
 * The following variable holds the network name of this host.
 */

static char hostname[TCL_HOSTNAME_LEN + 1];
static int  hostnameInited = 0;

/*
 * This structure describes per-instance state of a tcp (socket) based channel.
 */

typedef struct TcpState {
    Tcl_Channel channel;           /* Channel associated with this file. */
    int fd;                        /* The socket itself. */
    int flags;                     /* ORed combination of the bitfields
                                    * defined below. */
    int watchEvents;               /* OR'ed combination of FD_READ, FD_WRITE,
                                    * FD_CLOSE, FD_ACCEPT and FD_CONNECT that
                                    * indicate which events are interesting. */
    int readyEvents;               /* OR'ed combination of FD_READ, FD_WRITE,
                                    * FD_CLOSE, FD_ACCEPT and FD_CONNECT that
                                    * indicate which events have occurred. */
    int selectEvents;              /* OR'ed combination of FD_READ, FD_WRITE,
                                    * FD_CLOSE, FD_ACCEPT and FD_CONNECT that
                                    * indicate which events are currently
                                    * being selected. */
    int acceptEventCount;          /* Count of the current number of FD_ACCEPTs
                                    * that have arrived and not processed. */
    Tcl_TcpAcceptProc *acceptProc; /* Proc to call on accept. */
    ClientData acceptProcData;     /* The data for the accept proc. */
    int lastError;                 /* Error code from last message. */
    struct TcpState *nextPtr;      /* The next socket on the global socket
                                    * list. */
} TcpState;

/*
 * These bits may be ORed together into the "flags" field of a TcpState
 * structure.
 */

#define TCP_ASYNC_SOCKET        (1<<0)  /* Asynchronous socket. */
#define TCP_ASYNC_CONNECT       (1<<1)  /* Async connect in progress. */

/*
 * The following defines the maximum length of the listen queue. This is
 * the number of outstanding yet-to-be-serviced requests for a connection
 * on a server socket, more than this number of outstanding requests and
 * the connection request will fail.
 */

#ifndef SOMAXCONN
#define SOMAXCONN       100
#endif

#if     (SOMAXCONN < 100)
#undef  SOMAXCONN
#define SOMAXCONN       100
#endif

/*
 * The following defines how much buffer space the kernel should maintain
 * for a socket.
 */

#define SOCKET_BUFSIZE  4096

/*
 * Static routines for this file:
 */

static TcpState *       CreateSocket _ANSI_ARGS_((Tcl_Interp *interp,
                            int port, char *host, int server,
                            char *myaddr, int myport, int async));
static int              CreateSocketAddress _ANSI_ARGS_(
                            (struct sockaddr_in *sockaddrPtr,
                            char *host, int port));
static void             TcpAccept _ANSI_ARGS_((ClientData data, int mask));
static int              TcpBlockModeProc _ANSI_ARGS_((ClientData data,
                            int mode));
static int              TcpCloseProc _ANSI_ARGS_((ClientData instanceData,
                            Tcl_Interp *interp));
static int              TcpGetHandleProc _ANSI_ARGS_((ClientData instanceData,
                            int direction, ClientData *handlePtr));
static int              TcpGetOptionProc _ANSI_ARGS_((ClientData instanceData,
                            Tcl_Interp *interp, char *optionName,
                            Tcl_DString *dsPtr));
static int              TcpInputProc _ANSI_ARGS_((ClientData instanceData,
                            char *buf, int toRead,  int *errorCode));
static int              TcpOutputProc _ANSI_ARGS_((ClientData instanceData,
                            char *buf, int toWrite, int *errorCode));
static void             TcpWatchProc _ANSI_ARGS_((ClientData instanceData,
                            int mask));
static int              WaitForConnect _ANSI_ARGS_((TcpState *statePtr,
                            int *errorCodePtr));

/*
 * This structure describes the channel type structure for TCP socket
 * based IO:
 */

static Tcl_ChannelType tcpChannelType = {
    "tcp",                      /* Type name. */
    TCL_CHANNEL_VERSION_2,      /* v2 channel */
    TcpCloseProc,               /* Close proc. */
    TcpInputProc,               /* Input proc. */
    TcpOutputProc,              /* Output proc. */
    NULL,                       /* Seek proc. */
    NULL,                       /* Set option proc. */
    TcpGetOptionProc,           /* Get option proc. */
    TcpWatchProc,               /* Initialize notifier. */
    TcpGetHandleProc,           /* Get OS handles out of channel. */
    NULL,                       /* close2proc. */
    TcpBlockModeProc,           /* Set blocking or non-blocking mode.*/
    NULL,                       /* flush proc. */
    NULL,                       /* handler proc. */
};

/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetHostName --
 *
 *	Returns the name of the local host.
 *
 * Results:
 *      A string containing the network name for this machine, or
 *      an empty string if we can't figure out the name.  The caller
 *      must not modify or free this string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Tcl_GetHostName()
{
    struct utsname u;
    struct hostent *hp;

#ifdef VERBOSE
    printf("Tcl_GetHostName\n");
#endif

    if (hostnameInited) {
        return hostname;
    }

    (VOID *) memset((VOID *) &u, (int) 0, sizeof(struct utsname));
    if (uname(&u) > -1) {
        hp = gethostbyname(u.nodename);
        if (hp != NULL) {
            strcpy(hostname, hp->h_name);
        } else {
            strcpy(hostname, u.nodename);
        }
        hostnameInited = 1;
        return hostname;
    }

    hostname[0] = 0;
    return hostname;
}

/*
 *----------------------------------------------------------------------
 *
 * TcpBlockModeProc --
 *
 *      This procedure is invoked by the generic IO level to set blocking
 *      and nonblocking mode on a TCP socket based channel.
 *
 * Results:
 *      0 if successful, errno when failed.
 *
 * Side effects:
 *      Sets the device into blocking or nonblocking mode.
 *
 *----------------------------------------------------------------------
 */

        /* ARGSUSED */
static int
TcpBlockModeProc(instanceData, mode)
    ClientData instanceData;            /* Socket state. */
    int mode;                           /* The mode to set. Can be one of
                                         * TCL_MODE_BLOCKING or
                                         * TCL_MODE_NONBLOCKING. */
{
    TcpState *statePtr = (TcpState *) instanceData;
    int setting;

#ifdef VERBOSE
    printf("TcpBlockModeProc\n");
    fflush(stdout);
#endif
#ifndef USE_FIONBIO
    setting = fcntl(statePtr->fd, F_GETFL);
    if (mode == TCL_MODE_BLOCKING) {
        statePtr->flags &= (~(TCP_ASYNC_SOCKET));
        setting &= (~(O_NONBLOCK));
    } else {
        statePtr->flags |= TCP_ASYNC_SOCKET;
        setting |= O_NONBLOCK;
    }
    if (fcntl(statePtr->fd, F_SETFL, setting) < 0) {
        return errno;
    }
#endif

#ifdef  USE_FIONBIO
    if (mode == TCL_MODE_BLOCKING) {
        statePtr->flags &= (~(TCP_ASYNC_SOCKET));
        setting = 0;
        if (ioctl(statePtr->fd, (int) FIONBIO, &setting) == -1) {
            return errno;
        }
    } else {
        statePtr->flags |= TCP_ASYNC_SOCKET;
        setting = 1;
        if (ioctl(statePtr->fd, (int) FIONBIO, &setting) == -1) {
            return errno;
        }
    }
#endif

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * WaitForConnect --
 *
 *      Waits for a connection on an asynchronously opened socket to
 *      be completed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The socket is connected after this function returns.
 *
 *----------------------------------------------------------------------
 */

static int
WaitForConnect(statePtr, errorCodePtr)
    TcpState *statePtr;         /* State of the socket. */
    int *errorCodePtr;          /* Where to store errors? */
{
    int timeOut;                /* How long to wait. */
    int state;                  /* Of calling TclWaitForFile. */
    int flags;                  /* fcntl flags for the socket. */

#ifdef VERBOSE
    printf("WaitForConnect\n");
    fflush(stdout);
#endif
    /*
     * If an asynchronous connect is in progress, attempt to wait for it
     * to complete before reading.
     */

    if (statePtr->flags & TCP_ASYNC_CONNECT) {
        if (statePtr->flags & TCP_ASYNC_SOCKET) {
            timeOut = 0;
        } else {
            timeOut = -1;
        }
        errno = 0;
        state = TclOS2WaitForFile(statePtr->fd, TCL_WRITABLE | TCL_EXCEPTION,
                                  timeOut);
        if (!(statePtr->flags & TCP_ASYNC_SOCKET)) {
#ifndef USE_FIONBIO
            flags = fcntl(statePtr->fd, F_GETFL);
            flags &= (~(O_NONBLOCK));
            (void) fcntl(statePtr->fd, F_SETFL, flags);
#endif

#ifdef  USE_FIONBIO
            flags = 0;
            (void) ioctl(statePtr->fd, FIONBIO, &flags);
#endif
        }
        if (state & TCL_EXCEPTION) {
            return -1;
        }
        if (state & TCL_WRITABLE) {
            statePtr->flags &= (~(TCP_ASYNC_CONNECT));
        } else if (timeOut == 0) {
            *errorCodePtr = errno = EWOULDBLOCK;
            return -1;
        }
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TcpInputProc --
 *
 *      This procedure is invoked by the generic IO level to read input
 *      from a TCP socket based channel.
 *
 *      NOTE: We cannot share code with FilePipeInputProc because here
 *      we must use recv to obtain the input from the channel, not read.
 *
 * Results:
 *      The number of bytes read is returned or -1 on error. An output
 *      argument contains the POSIX error code on error, or zero if no
 *      error occurred.
 *
 * Side effects:
 *      Reads input from the input device of the channel.
 *
 *----------------------------------------------------------------------
 */

        /* ARGSUSED */
static int
TcpInputProc(instanceData, buf, bufSize, errorCodePtr)
    ClientData instanceData;            /* Socket state. */
    char *buf;                          /* Where to store data read. */
    int bufSize;                        /* How much space is available
                                         * in the buffer? */
    int *errorCodePtr;                  /* Where to store error code. */
{
    TcpState *statePtr = (TcpState *) instanceData;
    int bytesRead, state;

#ifdef VERBOSE
    printf("TcpInputProc\n");
    fflush(stdout);
#endif
    *errorCodePtr = 0;
    state = WaitForConnect(statePtr, errorCodePtr);
#ifdef VERBOSE
    printf("    state %d\n", state);
    fflush(stdout);
#endif
    if (state != 0) {
        return -1;
    }
    bytesRead = recv(statePtr->fd, buf, (size_t) bufSize, 0);
    if (bytesRead > -1) {
        return bytesRead;
    }
    if (errno == ECONNRESET) {

        /*
         * Turn ECONNRESET into a soft EOF condition.
         */

        return 0;
    }
    *errorCodePtr = errno;
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TcpOutputProc --
 *
 *      This procedure is invoked by the generic IO level to write output
 *      to a TCP socket based channel.
 *
 *      NOTE: We cannot share code with FilePipeOutputProc because here
 *      we must use send, not write, to get reliable error reporting.
 *
 * Results:
 *      The number of bytes written is returned. An output argument is
 *      set to a POSIX error code if an error occurred, or zero.
 *
 * Side effects:
 *      Writes output on the output device of the channel.
 *
 *----------------------------------------------------------------------
 */

static int
TcpOutputProc(instanceData, buf, toWrite, errorCodePtr)
    ClientData instanceData;            /* Socket state. */
    char *buf;                          /* The data buffer. */
    int toWrite;                        /* How many bytes to write? */
    int *errorCodePtr;                  /* Where to store error code. */
{
    TcpState *statePtr = (TcpState *) instanceData;
    int written;
    int state;                          /* Of waiting for connection. */

#ifdef VERBOSE
    printf("TcpOutputProc [%s]\n", buf);
    fflush(stdout);
#endif
    *errorCodePtr = 0;
    state = WaitForConnect(statePtr, errorCodePtr);
    if (state != 0) {
        return -1;
    }
    written = send(statePtr->fd, buf, (size_t) toWrite, 0);
    if (written > -1) {
        return written;
    }
    *errorCodePtr = errno;
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TcpCloseProc --
 *
 *      This procedure is invoked by the generic IO level to perform
 *      channel-type-specific cleanup when a TCP socket based channel
 *      is closed.
 *
 * Results:
 *      0 if successful, the value of errno if failed.
 *
 * Side effects:
 *      Closes the socket of the channel.
 *
 *----------------------------------------------------------------------
 */

        /* ARGSUSED */
static int
TcpCloseProc(instanceData, interp)
    ClientData instanceData;    /* The socket to close. */
    Tcl_Interp *interp;         /* For error reporting - unused. */
{
    TcpState *statePtr = (TcpState *) instanceData;
    int errorCode = 0;

#ifdef VERBOSE
    printf("TcpCloseProc\n");
    fflush(stdout);
#endif

    /*
     * Delete a file handler that may be active for this socket if this
     * is a server socket - the file handler was created automatically
     * by Tcl as part of the mechanism to accept new client connections.
     * Channel handlers are already deleted in the generic IO channel
     * closing code that called this function, so we do not have to
     * delete them here.
     */

    Tcl_DeleteFileHandler(statePtr->fd);

    if (close(statePtr->fd) < 0) {
        errorCode = errno;
    }
    ckfree((char *) statePtr);

    return errorCode;
}

/*
 *----------------------------------------------------------------------
 *
 * TcpGetOptionProc --
 *
 *      Computes an option value for a TCP socket based channel, or a
 *      list of all options and their values.
 *
 *      Note: This code is based on code contributed by John Haxby.
 *
 * Results:
 *      A standard Tcl result. The value of the specified option or a
 *      list of all options and their values is returned in the
 *      supplied DString.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
TcpGetOptionProc(instanceData, interp, optionName, dsPtr)
    ClientData instanceData;     /* Socket state. */
    Tcl_Interp *interp;          /* For error reporting - can be NULL. */
    char *optionName;            /* Name of the option to
                                  * retrieve the value for, or
                                  * NULL to get all options and
                                  * their values. */
    Tcl_DString *dsPtr;          /* Where to store the computed
                                  * value; initialized by caller. */
{
    TcpState *statePtr = (TcpState *) instanceData;
    struct sockaddr_in sockname;
    struct sockaddr_in peername;
    struct hostent *hostEntPtr;
    int size = sizeof(struct sockaddr_in);
    size_t len = 0;
    char buf[TCL_INTEGER_SPACE];

#ifdef VERBOSE
    printf("TcpGetOptionProc\n");
    fflush(stdout);
#endif
    if (optionName != (char *) NULL) {
        len = strlen(optionName);
    }

    if ((len > 1) && (optionName[1] == 'e') &&
            (strncmp(optionName, "-error", len) == 0)) {
        int optlen;
        int err, ret;

        optlen = sizeof(int);
        ret = getsockopt(statePtr->fd, SOL_SOCKET, SO_ERROR,
                (char *)&err, &optlen);
        if (ret < 0) {
            err = errno;
        }
        if (err != 0) {
            Tcl_DStringAppend(dsPtr, Tcl_ErrnoMsg(err), -1);
        }
       return TCL_OK;
    }

    if ((len == 0) ||
            ((len > 1) && (optionName[1] == 'p') &&
                    (strncmp(optionName, "-peername", len) == 0))) {
        if (getpeername(statePtr->fd, (struct sockaddr *) &peername,
                &size) >= 0) {
            if (len == 0) {
                Tcl_DStringAppendElement(dsPtr, "-peername");
                Tcl_DStringStartSublist(dsPtr);
            }
            Tcl_DStringAppendElement(dsPtr, inet_ntoa(peername.sin_addr));
            hostEntPtr = gethostbyaddr(                 /* INTL: Native. */
                    (char *) &peername.sin_addr,
                    sizeof(peername.sin_addr), AF_INET);
            if (hostEntPtr != NULL) {
                Tcl_DString ds;

                Tcl_ExternalToUtfDString(NULL, hostEntPtr->h_name, -1, &ds);
                Tcl_DStringAppendElement(dsPtr, Tcl_DStringValue(&ds));
            } else {
                Tcl_DStringAppendElement(dsPtr, inet_ntoa(peername.sin_addr));
            }
            TclFormatInt(buf, ntohs(peername.sin_port));
            Tcl_DStringAppendElement(dsPtr, buf);
            if (len == 0) {
                Tcl_DStringEndSublist(dsPtr);
            } else {
                return TCL_OK;
            }
        } else {
            /*
             * getpeername failed - but if we were asked for all the options
             * (len==0), don't flag an error at that point because it could
             * be an fconfigure request on a server socket. (which have
             * no peer). same must be done on win&mac.
             */

            if (len) {
                if (interp) {
                    Tcl_AppendResult(interp, "can't get peername: ",
                                     Tcl_PosixError(interp),
                                     (char *) NULL);
                }
                return TCL_ERROR;
            }
        }
    }

    if ((len == 0) ||
            ((len > 1) && (optionName[1] == 's') &&
                    (strncmp(optionName, "-sockname", len) == 0))) {
        if (getsockname(statePtr->fd, (struct sockaddr *) &sockname, &size)
                >= 0) {
            if (len == 0) {
                Tcl_DStringAppendElement(dsPtr, "-sockname");
                Tcl_DStringStartSublist(dsPtr);
            }
            Tcl_DStringAppendElement(dsPtr, inet_ntoa(sockname.sin_addr));
            hostEntPtr = gethostbyaddr(                 /* INTL: Native. */
                    (char *) &sockname.sin_addr,
                    sizeof(sockname.sin_addr), AF_INET);
            if (hostEntPtr != (struct hostent *) NULL) {
                Tcl_DString ds;

                Tcl_ExternalToUtfDString(NULL, hostEntPtr->h_name, -1, &ds);
                Tcl_DStringAppendElement(dsPtr, Tcl_DStringValue(&ds));
            } else {
                Tcl_DStringAppendElement(dsPtr, inet_ntoa(sockname.sin_addr));
            }
            TclFormatInt(buf, ntohs(sockname.sin_port));
            Tcl_DStringAppendElement(dsPtr, buf);
            if (len == 0) {
                Tcl_DStringEndSublist(dsPtr);
            } else {
                return TCL_OK;
            }
        } else {
            if (interp) {
                Tcl_AppendResult(interp, "can't get sockname: ",
                                 Tcl_PosixError(interp),
                                 (char *) NULL);
            }
            return TCL_ERROR;
        }
    }

    if (len > 0) {
        return Tcl_BadChannelOption(interp, optionName, "peername sockname");
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TcpWatchProc --
 *
 *      Initialize the notifier to watch Tcl_Files from this channel.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets up the notifier so that a future event on the channel will
 *      be seen by Tcl.
 *
 *----------------------------------------------------------------------
 */

static void
TcpWatchProc(instanceData, mask)
    ClientData instanceData;            /* The socket state. */
    int mask;                           /* Events of interest; an OR-ed
                                         * combination of TCL_READABLE,
                                         * TCL_WRITABLE and TCL_EXCEPTION. */
{
    TcpState *statePtr = (TcpState *) instanceData;

    /*
     * Make sure we don't mess with server sockets since they will never
     * be readable or writable at the Tcl level.  This keeps Tcl scripts
     * from interfering with the -accept behavior.
     */

    if (!statePtr->acceptProc) {
        if (mask) {
            Tcl_CreateFileHandler(statePtr->fd, mask,
                    (Tcl_FileProc *) Tcl_NotifyChannel,
                    (ClientData) statePtr->channel);
        } else {
            Tcl_DeleteFileHandler(statePtr->fd);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TcpGetHandleProc --
 *
 *      Called from Tcl_GetChannelHandle to retrieve OS handles from inside
 *      a TCP socket based channel.
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

        /* ARGSUSED */
static int
TcpGetHandleProc(instanceData, direction, handlePtr)
    ClientData instanceData;    /* The socket state. */
    int direction;              /* Not used. */
    ClientData *handlePtr;      /* Where to store the handle.  */
{
    TcpState *statePtr = (TcpState *) instanceData;

    *handlePtr = (ClientData)statePtr->fd;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateSocket --
 *
 *      This function opens a new socket in client or server mode
 *      and initializes the TcpState structure.
 *
 * Results:
 *      Returns a new TcpState, or NULL with an error in the interp's
 *      result, if interp is not NULL.
 *
 * Side effects:
 *      Opens a socket.
 *
 *----------------------------------------------------------------------
 */

static TcpState *
CreateSocket(interp, port, host, server, myaddr, myport, async)
    Tcl_Interp *interp;         /* For error reporting; can be NULL. */
    int port;                   /* Port number to open. */
    char *host;                 /* Name of host on which to open port.
                                 * NULL implies INADDR_ANY */
    int server;                 /* 1 if socket should be a server socket,
                                 * else 0 for a client socket. */
    char *myaddr;               /* Optional client-side address */
    int myport;                 /* Optional client-side port */
    int async;                  /* If nonzero and creating a client socket,
                                 * attempt to do an async connect. Otherwise
                                 * do a synchronous connect or bind. */
{
    int status, sock, asyncConnect, curState, origState;
    struct sockaddr_in sockaddr;        /* socket address */
    struct sockaddr_in mysockaddr;      /* Socket address for client */
    TcpState *statePtr;

#ifdef VERBOSE
    printf("CreateSocket %d %s s?%d\n", port, host, server);
    fflush(stdout);
#endif
    sock = -1;
    origState = 0;
    if (! CreateSocketAddress(&sockaddr, host, port)) {
        goto addressError;
    }
    if ((myaddr != NULL || myport != 0) &&
            ! CreateSocketAddress(&mysockaddr, myaddr, myport)) {
        goto addressError;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        goto addressError;
    }

    /*
     * Set the close-on-exec flag so that the socket will not get
     * inherited by child processes.
     */

    fcntl(sock, F_SETFD, FD_CLOEXEC);

    /*
     * Set kernel space buffering
     */

    TclSockMinimumBuffers(sock, SOCKET_BUFSIZE);

    asyncConnect = 0;
    status = 0;
    if (server) {

        /*
         * Set up to reuse server addresses automatically and bind to the
         * specified port.
         */

        status = 1;
        (void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &status,
                sizeof(status));
        status = bind(sock, (struct sockaddr *) &sockaddr,
                sizeof(struct sockaddr));
        if (status != -1) {
            status = listen(sock, SOMAXCONN);
        }
    } else {
        if (myaddr != NULL || myport != 0) {
            status = 1;
            (void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                    (char *) &curState, sizeof(status));
            status = bind(sock, (struct sockaddr *) &mysockaddr,
                    sizeof(struct sockaddr));
            if (status < 0) {
                goto bindError;
            }
        }

        /*
         * Attempt to connect. The connect may fail at present with an
         * EINPROGRESS but at a later time it will complete. The caller
         * will set up a file handler on the socket if she is interested in
         * being informed when the connect completes.
         */

        if (async) {
#ifndef USE_FIONBIO
            origState = fcntl(sock, F_GETFL);
            curState = origState | O_NONBLOCK;
            status = fcntl(sock, F_SETFL, curState);
#endif

#ifdef  USE_FIONBIO
            curState = 1;
            status = ioctl(sock, FIONBIO, &curState);
#endif
        } else {
            status = 0;
        }
        if (status > -1) {
            status = connect(sock, (struct sockaddr *) &sockaddr,
                    sizeof(sockaddr));
            if (status < 0) {
                if (errno == EINPROGRESS) {
                    asyncConnect = 1;
                    status = 0;
                }
                /*
                 * Here we are if the connect succeeds. In case of an
                 * asynchronous connect we have to reset the channel to
                 * blocking mode.  This appears to happen not very often,
                 * but e.g. on a HP 9000/800 under HP-UX B.11.00 we enter
                 * this stage. [Bug: 4388]
                 */
                if (async) {
#ifndef USE_FIONBIO
                    origState = fcntl(sock, F_GETFL);
                    curState = origState & ~(O_NONBLOCK);
                    status = fcntl(sock, F_SETFL, curState);
#endif

#ifdef  USE_FIONBIO
                    curState = 0;
                    status = ioctl(sock, FIONBIO, &curState);
#endif
                }
            }
        }
    }

bindError:
    if (status < 0) {
        if (interp != NULL) {
            Tcl_AppendResult(interp, "couldn't open socket: ",
                    Tcl_PosixError(interp), (char *) NULL);
        }
        if (sock != -1) {
            close(sock);
        }
        return NULL;
    }

    /*
     * Allocate a new TcpState for this socket.
     */

    statePtr = (TcpState *) ckalloc((unsigned) sizeof(TcpState));
    statePtr->flags = 0;
    if (asyncConnect) {
        statePtr->flags = TCP_ASYNC_CONNECT;
    }
    statePtr->fd = sock;

    return statePtr;

addressError:
    if (sock != -1) {
        close(sock);
    }
    if (interp != NULL) {
        Tcl_AppendResult(interp, "couldn't open socket: ",
                         Tcl_PosixError(interp), (char *) NULL);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateSocketAddress --
 *
 *      This function initializes a sockaddr structure for a host and port.
 *
 * Results:
 *      1 if the host was valid, 0 if the host could not be converted to
 *      an IP address.
 *
 * Side effects:
 *      Fills in the *sockaddrPtr structure.
 *
 *----------------------------------------------------------------------
 */

static int
CreateSocketAddress(sockaddrPtr, host, port)
    struct sockaddr_in *sockaddrPtr;    /* Socket address */
    char *host;                         /* Host.  NULL implies INADDR_ANY */
    int port;                           /* Port number */
{
    struct hostent *hostent;            /* Host database entry */
    struct in_addr addr;                /* For 64/32 bit madness */

#ifdef VERBOSE
    printf("CreateSocketAddress\n");
    fflush(stdout);
#endif
    (void) memset((VOID *) sockaddrPtr, '\0', sizeof(struct sockaddr_in));
    sockaddrPtr->sin_family = AF_INET;
    sockaddrPtr->sin_port = htons((unsigned short) (port & 0xFFFF));
    if (host == NULL) {
        addr.s_addr = INADDR_ANY;
    } else {
        Tcl_DString ds;
        CONST char *native;

        if (host == NULL) {
            native = NULL;
        } else {
            native = Tcl_UtfToExternalDString(NULL, host, -1, &ds);
        }
        addr.s_addr = inet_addr(native);                /* INTL: Native. */
        /*
         * This is 0xFFFFFFFF to ensure that it compares as a 32bit -1
         * on either 32 or 64 bits systems.
         */
        if (addr.s_addr == 0xFFFFFFFF) {
            hostent = gethostbyname(native);            /* INTL: Native. */
            if (hostent != NULL) {
                memcpy((VOID *) &addr,
                        (VOID *) hostent->h_addr_list[0],
                        (size_t) hostent->h_length);
            } else {
#ifdef  EHOSTUNREACH
                errno = EHOSTUNREACH;
#else
#ifdef ENXIO
                errno = ENXIO;
#endif
#endif
                if (native != NULL) {
                    Tcl_DStringFree(&ds);
                }
                return 0;       /* error */
            }
        }
        if (native != NULL) {
            Tcl_DStringFree(&ds);
        }
    }

    /*
     * NOTE: On 64 bit machines the assignment below is rumored to not
     * do the right thing. Please report errors related to this if you
     * observe incorrect behavior on 64 bit machines such as DEC Alphas.
     * Should we modify this code to do an explicit memcpy?
     */

    sockaddrPtr->sin_addr.s_addr = addr.s_addr;
    return 1;   /* Success. */
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_OpenTcpClient --
 *
 *      Opens a TCP client socket and creates a channel around it.
 *
 * Results:
 *      The channel or NULL if failed.  An error message is returned
 *      in the interpreter on failure.
 *
 * Side effects:
 *      Opens a client socket and creates a new channel.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
Tcl_OpenTcpClient(interp, port, host, myaddr, myport, async)
    Tcl_Interp *interp;                 /* For error reporting; can be NULL. */
    int port;                           /* Port number to open. */
    char *host;                         /* Host on which to open port. */
    char *myaddr;                       /* Client-side address */
    int myport;                         /* Client-side port */
    int async;                          /* If nonzero, attempt to do an
                                         * asynchronous connect. Otherwise
                                         * we do a blocking connect. */
{
    TcpState *statePtr;
    char channelName[16 + TCL_INTEGER_SPACE];

#ifdef VERBOSE
    printf("Tcl_OpenTcpClient\n");
    fflush(stdout);
#endif
    /*
     * Create a new client socket and wrap it in a channel.
     */

    statePtr = CreateSocket(interp, port, host, 0, myaddr, myport, async);
    if (statePtr == NULL) {
        return NULL;
    }

    statePtr->acceptProc = NULL;
    statePtr->acceptProcData = (ClientData) NULL;

    sprintf(channelName, "sock%d", statePtr->fd);

    statePtr->channel = Tcl_CreateChannel(&tcpChannelType, channelName,
                                          (ClientData) statePtr,
                                          (TCL_READABLE | TCL_WRITABLE));
    if (Tcl_SetChannelOption(interp, statePtr->channel, "-translation",
                             "auto crlf") == TCL_ERROR) {
        Tcl_Close((Tcl_Interp *) NULL, statePtr->channel);
        return NULL;
    }
    return statePtr->channel;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_MakeTcpClientChannel --
 *
 *      Creates a Tcl_Channel from an existing client TCP socket.
 *
 * Results:
 *      The Tcl_Channel wrapped around the preexisting TCP socket.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
Tcl_MakeTcpClientChannel(sock)
    ClientData sock;            /* The socket to wrap up into a channel. */
{
    TcpState *statePtr;
    char channelName[16 + TCL_INTEGER_SPACE];

#ifdef VERBOSE
    printf("Tcl_MakeTcpClientChannel\n");
    fflush(stdout);
#endif
    statePtr = (TcpState *) ckalloc((unsigned) sizeof(TcpState));
    statePtr->fd = (int) sock;
    statePtr->acceptProc = NULL;
    statePtr->acceptProcData = (ClientData) NULL;

    sprintf(channelName, "sock%d", statePtr->fd);

    statePtr->channel = Tcl_CreateChannel(&tcpChannelType, channelName,
                                          (ClientData) statePtr,
                                          (TCL_READABLE | TCL_WRITABLE));
    if (Tcl_SetChannelOption((Tcl_Interp *) NULL, statePtr->channel,
                             "-translation", "auto crlf") == TCL_ERROR) {
        Tcl_Close((Tcl_Interp *) NULL, statePtr->channel);
        return NULL;
    }
    return statePtr->channel;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_OpenTcpServer --
 *
 *      Opens a TCP server socket and creates a channel around it.
 *
 * Results:
 *      The channel or NULL if failed. If an error occurred, an
 *      error message is left in the interp's result if interp is
 *      not NULL.
 *
 * Side effects:
 *      Opens a server socket and creates a new channel.
 *
 *----------------------------------------------------------------------
 */

Tcl_Channel
Tcl_OpenTcpServer(interp, port, myHost, acceptProc, acceptProcData)
    Tcl_Interp *interp;                 /* For error reporting - may be
                                         * NULL. */
    int port;                           /* Port number to open. */
    char *myHost;                       /* Name of local host. */
    Tcl_TcpAcceptProc *acceptProc;      /* Callback for accepting connections
                                         * from new clients. */
    ClientData acceptProcData;          /* Data for the callback. */
{
    TcpState *statePtr;
    char channelName[16 + TCL_INTEGER_SPACE];

#ifdef VERBOSE
    printf("Tcl_OpenTcpServer port %d myHost [%s]\n", port, myHost);
    fflush(stdout);
#endif

    if (TclpHasSockets(interp) != TCL_OK) {
        return NULL;
    }

    /*
     * Create a new client socket and wrap it in a channel.
     */

    statePtr = CreateSocket(interp, port, myHost, 1, NULL, 0, 0);
    if (statePtr == NULL) {
        return NULL;
    }

    statePtr->acceptProc = acceptProc;
    statePtr->acceptProcData = acceptProcData;

    /*
     * Set up the callback mechanism for accepting connections
     * from new clients.
     */

    Tcl_CreateFileHandler(statePtr->fd, TCL_READABLE, TcpAccept,
            (ClientData) statePtr);
    sprintf(channelName, "sock%d", statePtr->fd);
    statePtr->channel = Tcl_CreateChannel(&tcpChannelType, channelName,
            (ClientData) statePtr, 0);
    return statePtr->channel;
}

/*
 *----------------------------------------------------------------------
 *
 * TcpAccept --
 *      Accept a TCP socket connection.  This is called by the event loop.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates a new connection socket. Calls the registered callback
 *      for the connection acceptance mechanism.
 *
 *----------------------------------------------------------------------
 */

        /* ARGSUSED */
static void
TcpAccept(data, mask)
    ClientData data;                    /* Callback token. */
    int mask;                           /* Not used. */
{
    TcpState *sockState;                /* Client data of server socket. */
    int newsock;                        /* The new client socket */
    TcpState *newSockState;             /* State for new socket. */
    struct sockaddr_in addr;            /* The remote address */
    int len;                            /* For accept interface */
    char channelName[16 + TCL_INTEGER_SPACE];

#ifdef VERBOSE
    printf("TcpAccept\n");
    fflush(stdout);
#endif
    sockState = (TcpState *) data;

    len = sizeof(struct sockaddr_in);
    newsock = accept(sockState->fd, (struct sockaddr *) &addr, &len);
    if (newsock < 0) {
        return;
    }

    /*
     * Set close-on-exec flag to prevent the newly accepted socket from
     * being inherited by child processes.
     */

    (void) fcntl(newsock, F_SETFD, FD_CLOEXEC);

    newSockState = (TcpState *) ckalloc((unsigned) sizeof(TcpState));

    newSockState->flags = 0;
    newSockState->fd = newsock;
    newSockState->acceptProc = NULL;
    newSockState->acceptProcData = NULL;

    sprintf(channelName, "sock%d", newsock);
    newSockState->channel = Tcl_CreateChannel(&tcpChannelType, channelName,
            (ClientData) newSockState, (TCL_READABLE | TCL_WRITABLE));

    Tcl_SetChannelOption(NULL, newSockState->channel, "-translation",
                         "auto crlf");

    if (sockState->acceptProc != NULL) {
        (sockState->acceptProc) (sockState->acceptProcData,
                newSockState->channel, inet_ntoa(addr.sin_addr),
                ntohs(addr.sin_port));
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpHasSockets --
 *
 *      Detect if sockets are available on this platform.
 *
 * Results:
 *      Returns TCL_OK.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
TclpHasSockets(interp)
    Tcl_Interp *interp;
{
    return TCL_OK;
}

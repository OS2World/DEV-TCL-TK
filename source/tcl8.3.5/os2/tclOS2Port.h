/*
 * tclOS2Port.h --
 *
 *	This header file handles porting issues that occur because of
 *	differences between OS/2 and Unix. It should be the only file
 *	that contains #ifdefs to handle different flavors of OS.
 *
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 1996-2001 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _TCLOS2PORT
#define _TCLOS2PORT

#ifndef _TCLINT
#   include "tclInt.h"
#endif

#define INCL_BASE
#define INCL_PM
#include <os2.h>
#undef INCL_PM
#undef INCL_BASE

#ifndef OS2
#define OS2
#endif

/* Global variables */
extern HAB hab;	/* Anchor block */
extern HMQ hmq;	/* Message queue */

/*
 *---------------------------------------------------------------------------
 * The following sets of #includes and #ifdefs are required to get Tcl to
 * compile under the OS/2 compilers.
 *---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <io.h>
#include <malloc.h>
#include <process.h>
#include <signal.h>
#include <string.h>

#include <sys/types.h>	/* required by sys/stat.h */
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/utime.h>

#include <sys/time.h>

/* BSD sockets from EMX are int */
#define SOCKET int
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/select.h>


#ifdef BUILD_tcl
# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS EXPENTRY
#endif

/*
 * Maximum number of drives
 */

#ifndef MAXDRIVES
#define MAXDRIVES 26
#endif

/*
 * If ENOTSUP is not defined, define it to a value that will never occur.
 */

#ifndef ENOTSUP
#define ENOTSUP         -1030507
#endif

#if 0
/*
 * Declare directory manipulation routines.
 */

#ifdef HAS_DIRENT
#	include <dirent.h>
#else /* HAS_DIRENT */

#include <direct.h>
#define MAXNAMLEN 255

struct dirent {
    long d_ino;			/* Inode number of entry */
    short d_reclen;		/* Length of this record */
    short d_namlen;		/* Length of string in d_name */
    char d_name[MAXNAMLEN + 1];
				/* Name must be no longer than this */
};

typedef struct _dirdesc DIR;

EXTERN void			closedir _ANSI_ARGS_((DIR *dirp));
EXTERN DIR *			opendir _ANSI_ARGS_((char *name));
EXTERN struct dirent *		readdir _ANSI_ARGS_((DIR *dirp));
#endif /* HAS_DIRENT */
#endif /* if 0 */

/*
 * Supply definitions for macros to query wait status, if not already
 * defined in header files above.
 */

#if TCL_UNION_WAIT
#   define WAIT_STATUS_TYPE union wait
#else
#   define WAIT_STATUS_TYPE int
#endif

#ifndef WIFEXITED
#   define WIFEXITED(stat)  (((*((int *) &(stat))) & 0xff) == 0)
#endif

#ifndef WEXITSTATUS
#   define WEXITSTATUS(stat) (((*((int *) &(stat))) >> 8) & 0xff)
#endif

#ifndef WIFSIGNALED
#   define WIFSIGNALED(stat) (((*((int *) &(stat)))) && ((*((int *) &(stat))) == ((*((int *) &(stat))) & 0x00ff)))
#endif

#ifndef WTERMSIG
#   define WTERMSIG(stat)    ((*((int *) &(stat))) & 0x7f)
#endif

#ifndef WIFSTOPPED
#   define WIFSTOPPED(stat)  (((*((int *) &(stat))) & 0xff) == 0177)
#endif

#ifndef WSTOPSIG
#   define WSTOPSIG(stat)    (((*((int *) &(stat))) >> 8) & 0xff)
#endif

/*
 * Define constants for waitpid() system call if they aren't defined
 * by a system header file.
 */

#ifndef WNOHANG
#   define WNOHANG 1
#endif
#ifndef WUNTRACED
#   define WUNTRACED 2
#endif

/*
 * Define access mode constants if they aren't already defined.
 */

#ifndef F_OK
#    define F_OK 00
#endif
#ifndef X_OK
#    define X_OK 01
#endif
#ifndef W_OK
#    define W_OK 02
#endif
#ifndef R_OK
#    define R_OK 04
#endif

/*
 * Define macros to query file type bits, if they're not already
 * defined.
 */

#ifndef S_ISREG
#   ifdef S_IFREG
#       define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#   else
#       define S_ISREG(m) 0
#   endif
# endif
#ifndef S_ISDIR
#   ifdef S_IFDIR
#       define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#   else
#       define S_ISDIR(m) 0
#   endif
# endif
#ifndef S_ISCHR
#   ifdef S_IFCHR
#       define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#   else
#       define S_ISCHR(m) 0
#   endif
# endif
#ifndef S_ISBLK
#   ifdef S_IFBLK
#       define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#   else
#       define S_ISBLK(m) 0
#   endif
# endif
#ifndef S_ISFIFO
#   ifdef S_IFIFO
#       define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#   else
#       define S_ISFIFO(m) 0
#   endif
# endif

/*
 * Define MAXPATHLEN in terms of MAXPATH if available
 */

#ifndef MAX_PATH
#define MAX_PATH CCHMAXPATH
#endif

#ifndef MAXPATH
#define MAXPATH MAX_PATH
#endif /* MAXPATH */

#ifndef MAXPATHLEN
#define MAXPATHLEN MAXPATH
#endif /* MAXPATHLEN */

/*
 * Define pid_t and uid_t if they're not already defined.
 */

#if ! TCL_PID_T
#   define pid_t int
#endif
#if ! TCL_UID_T
#   define uid_t int
#endif

/*
 *---------------------------------------------------------------------------
 * The following macros and declarations represent the interface between
 * generic and OS/2-specific parts of Tcl.  Some of the macros may
 * override functions declared in tclInt.h.
 *---------------------------------------------------------------------------
 */

/*
 * The default platform eol translation on OS/2 is TCL_TRANSLATE_CRLF:
 */

#define TCL_PLATFORM_TRANSLATION        TCL_TRANSLATE_CRLF

/*
 * Declare dynamic loading extension macro.
 */

#define TCL_SHLIB_EXT ".dll"

/*
 * The following define ensures that we use the native putenv
 * implementation to modify the environment array.  This keeps
 * the C level environment in synch with the system level environment.
 */

#define USE_PUTENV      1

/* Wrappers for system memory allocation: see tclOS2Alloc.c */

/*
 * The following macros have trivial definitions, allowing generic code to
 * address platform-specific issues.
 */

#define TclpReleaseFile(file)   ckfree((char *) file)

/*
 * The following macros and declarations wrap the C runtime library
 * functions.
 */

#define TclpExit                exit
#define TclpLstat               TclpStat

/*
 * Platform specific mutex definition used by memory allocators.
 * These mutexes are statically allocated and explicitly initialized.
 * Most modules do not use this, but instead use Tcl_Mutex types and
 * Tcl_MutexLock and Tcl_MutexUnlock that are self-initializing.
 */

#ifdef TCL_THREADS
typedef HMTX TclpMutex;
EXTERN void     TclpMutexInit _ANSI_ARGS_((Tcl_Mutex *mPtr));
EXTERN void     TclpMutexLock _ANSI_ARGS_((Tcl_Mutex *mPtr));
EXTERN void     TclpMutexUnlock _ANSI_ARGS_((Tcl_Mutex *mPtr));
#else
typedef int TclpMutex;
#define TclpMutexInit(a)
#define TclpMutexLock(a)
#define TclpMutexUnlock(a)
#endif /* TCL_THREADS */

#include "tclPlatDecls.h"
#include "tclIntPlatDecls.h"

# undef TCL_STORAGE_CLASS
# define TCL_STORAGE_CLASS DLLIMPORT

#endif /* _TCLOS2PORT */

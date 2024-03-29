/* 
 * tclOS2Error.c --
 *
 *	This file contains code for converting from OS/2 errors to
 *	errno errors.
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tclOS2Int.h"

/*
 * The following table contains the mapping from OS/2 errors to
 * errno errors.
 */

static char errorTable[] = {
    0,		/* NO_ERROR			0 */
    EINVAL,	/* ERROR_INVALID_FUNCTION	1 */
    ENOENT,	/* ERROR_FILE_NOT_FOUND		2 */
    ENOENT,	/* ERROR_PATH_NOT_FOUND		3 */
    EMFILE,	/* ERROR_TOO_MANY_OPEN_FILES	4 */
    EACCES,	/* ERROR_ACCESS_DENIED		5 */
    EBADF,	/* ERROR_INVALID_HANDLE		6 */
    ENOMEM,	/* ERROR_ARENA_TRASHED		7 */
    ENOMEM,	/* ERROR_NOT_ENOUGH_MEMORY	8 */
    ENOMEM,	/* ERROR_INVALID_BLOCK		9 */
    E2BIG,	/* ERROR_BAD_ENVIRONMENT	10 */
    ENOEXEC,	/* ERROR_BAD_FORMAT		11 */
    EACCES,	/* ERROR_INVALID_ACCESS		12 */
    EINVAL,	/* ERROR_INVALID_DATA		13 */
    ENOMEM,	/* ERROR_OUT_OF_MEMORY		14 */
    ENOENT,	/* ERROR_INVALID_DRIVE		15 */
    EACCES,	/* ERROR_CURRENT_DIRECTORY	16 */
    EXDEV,	/* ERROR_NOT_SAME_DEVICE	17 */
    ENOENT,	/* ERROR_NO_MORE_FILES		18 */
    EROFS,	/* ERROR_WRITE_PROTECT		19 */
    EIO,	/* ERROR_BAD_UNIT		20 */
    EAGAIN,	/* ERROR_NOT_READY		21 */
    EIO,	/* ERROR_BAD_COMMAND		22 */
    EIO,	/* ERROR_CRC			23 */
    EIO,	/* ERROR_BAD_LENGTH		24 */
    EIO,	/* ERROR_SEEK			25 */
    EIO,	/* ERROR_NOT_DOS_DISK		26 */
    EIO,	/* ERROR_SECTOR_NOT_FOUND	27 */
    EAGAIN,	/* ERROR_OUT_OF_PAPER		28 */
    EIO,	/* ERROR_WRITE_FAULT		29 */
    EIO,	/* ERROR_READ_FAULT		30 */
    EIO,	/* ERROR_GEN_FAILURE		31 */
    EACCES,	/* ERROR_SHARING_VIOLATION	32 */
    EACCES,	/* ERROR_LOCK_VIOLATION		33 */
    EIO,	/* ERROR_WRONG_DISK		34 */
    ENOENT,	/* ERROR_FCB_UNAVAILABLE	35 */
    ENOENT,	/* ERROR_SHARING_BUFFER_EXCEEDED	36 */
    EINVAL,	/* ERROR_CODE_PAGE_MISMATCHED	37 */
    EIO,	/* ERROR_HANDLE_EOF		38 */
    ENOSPC,	/* ERROR_HANDLE_DISK_FULL	39 */
    EINVAL,	/* 40 */
    EINVAL,	/* 41 */
    EINVAL,	/* 42 */
    EINVAL,	/* 43 */
    EINVAL,	/* 44 */
    EINVAL,	/* 45 */
    EINVAL,	/* 46 */
    EINVAL,	/* 47 */
    EINVAL,	/* 48 */
    EINVAL,	/* 49 */
    ENOENT,	/* ERROR_NOT_SUPPORTED		50 */
    EAGAIN,	/* ERROR_REM_NOT_LIST		51 */
    EEXIST,	/* ERROR_DUP_NAME		52 */
    ENOENT,	/* ERROR_BAD_NETPATH		53 */
    EAGAIN,	/* ERROR_NETWORK_BUSY		54 */
    ENOENT,	/* ERROR_DEV_NOT_EXIST		55 */
    EAGAIN,	/* ERROR_TOO_MANY_CMDS		56 */
    EIO,	/* ERROR_ADAP_HDW_ERR		57 */
    EIO,	/* ERROR_BAD_NET_RESP		58 */
    EIO,	/* ERROR_UNEXP_NET_ERR		59 */
    EINVAL,	/* ERROR_BAD_REM_ADAP		60 */
    ENOSPC,	/* ERROR_PRINTQ_FULL		61 */
    ENOSPC,	/* ERROR_NO_SPOOL_SPACE		62 */
    ENOENT,	/* ERROR_PRINT_CANCELLED	63 */
    ENOENT,	/* ERROR_NETNAME_DELETED	64 */
    EACCES,	/* ERROR_NETWORK_ACCESS_DENIED	65 */
    ENOENT,	/* ERROR_BAD_DEV_TYPE		66 */
    ENOENT,	/* ERROR_BAD_NET_NAME		67 */
    ENOENT,	/* ERROR_TOO_MANY_NAMES		68 */
    EIO,	/* ERROR_TOO_MANY_SESS		69 */
    EAGAIN,	/* ERROR_SHARING_PAUSED		70 */
    EINVAL,	/* ERROR_REQ_NOT_ACCEP		71 */
    EAGAIN,	/* ERROR_REDIR_PAUSED		72 */
    EACCES,	/* ERROR_SBCS_ATT_WRITE_PROT	73 */
    EAGAIN,	/* ERROR_SBCS_GENERAL_FAILURE	74 */
    ENOMEM,	/* ERROR_XGA_OUT_MEMORY		75 */
    EINVAL,	/* 76 */
    EINVAL,	/* 77 */
    EINVAL,	/* 78 */
    EINVAL,	/* 79 */
    EEXIST,	/* ERROR_FILE_EXISTS		80 */
    EINVAL,	/* ERROR_DUP_FCB		81 */
    ENOSPC,	/* ERROR_CANNOT_MAKE		82 */
    EIO,	/* ERROR_FAIL_I24		83 */
    ENOENT,	/* ERROR_OUT_OF_STRUCTURES	84 */
    EEXIST,	/* ERROR_ALREADY_ASSIGNED	85 */
    EPERM,	/* ERROR_INVALID_PASSWORD	86 */
    EINVAL,	/* ERROR_INVALID_PARAMETER	87 */
    EIO,	/* ERROR_NET_WRITE_FAULT	88 */
    EAGAIN,	/* ERROR_NO_PROC_SLOTS		89 */
    EINVAL,	/* ERROR_NOT_FROZEN		90 */
    EINVAL,	/* ERR_TSTOVFL			91 */
    EINVAL,	/* ERR_TSTDUP			92 */
    EINVAL,	/* ERROR_NO_ITEMS		93 */
    EINVAL,	/* 94 */
    EINVAL,	/* ERROR_INTERRUPT		95 */
    EINVAL,	/* 96 */
    EINVAL,	/* 97 */
    EINVAL,	/* 98 */
    EAGAIN,	/* ERROR_DEVICE_IN_USE		99 */
    EINVAL,	/* ERROR_TOO_MANY_SEMAPHORES	100 */
    EEXIST,	/* ERROR_EXCL_SEM_ALREADY_OWNED	101 */
    EACCES,	/* ERROR_SEM_IS_SET		102 */
    EINVAL,	/* ERROR_TOO_MANY_SEM_REQUESTS	103 */
    EACCES,	/* ERROR_INVALID_AT_INTERRUPT_TIME	104 */
    EINVAL,	/* ERROR_SEM_OWNER_DIED		105 */
    EINVAL,	/* ERROR_SEM_USER_LIMIT		106 */
    EXDEV,	/* ERROR_DISK_CHANGE		107 */
    EACCES,	/* ERROR_DRIVE_LOCKED		108 */
    EPIPE,	/* ERROR_BROKEN_PIPE		109 */
    ENOENT,	/* ERROR_OPEN_FAILED		110 */
    ERANGE,	/* ERROR_BUFFER_OVERFLOW	111 */
    ENOSPC,	/* ERROR_DISK_FULL		112 */
    EMFILE,	/* ERROR_NO_MORE_SEARCH_HANDLES	113 */
    EBADF,	/* ERROR_INVALID_TARGET_HANDLE	114 */
    EACCES,	/* ERROR_PROTECTION_VIOLATION	115 */
    EINVAL,	/* ERROR_VIOKBD_REQUEST		116 */
    EINVAL,	/* ERROR_INVALID_CATEGORY	117 */
    EINVAL,	/* ERROR_INVALID_VERIFY_SWITCH	118 */
    ENOENT,	/* ERROR_BAD_DRIVER_LEVEL	119 */
    ENOENT,	/* ERROR_CALL_NOT_IMPLEMENTED	120 */
    EINVAL,	/* ERROR_SEM_TIMEOUT		121 */
    EINVAL,	/* ERROR_INSUFFICIENT_BUFFER	122 */
    EINVAL,	/* ERROR_INVALID_NAME		123 */
    EINVAL,	/* ERROR_INVALID_LEVEL		124 */
    ENOENT,	/* ERROR_NO_VOLUME_LABEL	125 */
    ENOENT,	/* ERROR_MOD_NOT_FOUND		126 */
    ESRCH,	/* ERROR_PROC_NOT_FOUND		127 */
    ECHILD,	/* ERROR_WAIT_NO_CHILDREN	128 */
    ECHILD,	/* ERROR_CHILD_NOT_COMPLETE	129 */
    EINVAL,	/* ERROR_DIRECT_ACCESS_HANDLE	130 */
    EACCES,	/* ERROR_NEGATIVE_SEEK		131 */
    ESPIPE,	/* ERROR_SEEK_ON_DEVICE		132 */
    EACCES,	/* ERROR_IS_JOIN_TARGET		133 */
    EACCES,	/* ERROR_IS_JOINED		134 */
    EACCES,	/* ERROR_IS_SUBSTED		135 */
    EACCES,	/* ERROR_NOT_JOINED		136 */
    EACCES,	/* ERROR_NOT_SUBSTED		137 */
    EACCES,	/* ERROR_JOIN_TO_JOIN		138 */
    EACCES,	/* ERROR_SUBST_TO_SUBST		139 */
    EACCES,	/* ERROR_JOIN_TO_SUBST		140 */
    EACCES,	/* ERROR_SUBST_TO_JOIN		141 */
    EAGAIN,	/* ERROR_BUSY_DRIVE		142 */
    EACCES,	/* ERROR_SAME_DRIVE		143 */
    EACCES,	/* ERROR_DIR_NOT_ROOT		144 */
    EACCES,	/* ERROR_DIR_NOT_EMPTY		145 */
    EACCES,	/* ERROR_IS_SUBST_PATH		146 */
    EACCES,	/* ERROR_IS_JOIN_PATH		147 */
    EAGAIN,	/* ERROR_PATH_BUSY		148 */
    EACCES,	/* ERROR_IS_SUBST_TARGET	149 */
    EINVAL,	/* ERROR_SYSTEM_TRACE		150 */
    EINVAL,	/* ERROR_INVALID_EVENT_COUNT	151 */
    EINVAL,	/* ERROR_TOO_MANY_MUXWAITERS	152 */
    EINVAL,	/* ERROR_INVALID_LIST_FORMAT	153 */
    EINVAL,	/* ERROR_LABEL_TOO_LONG		154 */
    EINVAL,	/* ERROR_TOO_MANY_TCBS		155 */
    EACCES,	/* ERROR_SIGNAL_REFUSED		156 */
    EINVAL,	/* ERROR_DISCARDED		157 */
    EACCES,	/* ERROR_NOT_LOCKED		158 */
    ENOENT,	/* ERROR_BAD_THREADID_ADDR	159 */
    ENOENT,	/* ERROR_BAD_ARGUMENTS		160 */
    ENOENT,	/* ERROR_BAD_PATHNAME		161 */
    EINVAL,	/* ERROR_SIGNAL_PENDING		162 */
    EINVAL,	/* ERROR_UNCERTAIN_MEDIA	163 */
    EINVAL,	/* ERROR_MAX_THRDS_REACHED	164 */
    ENOENT,	/* ERROR_MONITORS_NOT_SUPPORTED	165 */
    EINVAL,	/* ERROR_UNC_DRIVER_NOT_INSTALLED	166 */
    EACCES,	/* ERROR_LOCK_FAILED		167 */
    EINVAL,	/* ERROR_SWAPIO_FAILED		168 */
    EINVAL,	/* ERROR_SWAPIN_FAILED		169 */
    EAGAIN,	/* ERROR_BUSY			170 */
    EINVAL,	/* 171 */
    EINVAL,	/* 172 */
    EINVAL,	/* ERROR_CANCEL_VIOLATION	173 */
    ENOENT,	/* ERROR_ATOMIC_LOCK_NOT_SUPPORTED	174 */
    ENOENT,	/* ERROR_READ_LOCKS_NOT_SUPPORTED	175 */
    EINVAL,	/* 176 */
    EINVAL,	/* 177 */
    EINVAL,	/* 178 */
    EINVAL,	/* 179 */
    EINVAL,	/* ERROR_INVALID_SEGMENT_NUMBER	180 */
    EINVAL,	/* ERROR_INVALID_CALLGATE	181 */
    EINVAL,	/* ERROR_INVALID_ORDINAL	182 */
    EEXIST,	/* ERROR_ALREADY_EXISTS		183 */
    ECHILD,	/* ERROR_NO_CHILD_PROCESS	184 */
    EINVAL,	/* ERROR_CHILD_ALIVE_NOWAIT	185 */
    EINVAL,	/* ERROR_INVALID_FLAG_NUMBER	186 */
    EINVAL,	/* ERROR_SEM_NOT_FOUND		187 */
    EINVAL,	/* ERROR_INVALID_STARTING_CODESEG	188 */
    EINVAL,	/* ERROR_INVALID_STACKSEG	189 */
    EINVAL,	/* ERROR_INVALID_MODULETYPE	190 */
    EINVAL,	/* ERROR_INVALID_EXE_SIGNATURE	191 */
    EACCES,	/* ERROR_EXE_MARKED_INVALID	192 */
    EINVAL,	/* ERROR_BAD_EXE_FORMAT		193 */
    ERANGE,	/* ERROR_ITERATED_DATA_EXCEEDS_64K	194 */
    EINVAL,	/* ERROR_INVALID_MINALLOCSIZE	195 */
    EACCES,	/* ERROR_DYNLINK_FROM_INVALID_RING	196 */
    EACCES,	/* ERROR_IOPL_NOT_ENABLED	197 */
    EINVAL,	/* ERROR_INVALID_SEGDPL		198 */
    ERANGE,	/* ERROR_AUTODATASEG_EXCEEDS_64K	199 */
    EINVAL,	/* ERROR_RING2SEG_MUST_BE_MOVABLE	200 */
    EINVAL,	/* ERROR_RELOCSRC_CHAIN_EXCEEDS_SEGLIMIT	201 */
    EINVAL,	/* ERROR_INFLOOP_IN_RELOC_CHAIN	202 */
    EINVAL,	/* ERROR_ENVVAR_NOT_FOUND	203 */
    EINVAL,	/* ERROR_NOT_CURRENT_CTRY	204 */
    EINVAL,	/* ERROR_NO_SIGNAL_SENT		205 */
    ENAMETOOLONG,/* ERROR_FILENAME_EXCED_RANGE	206 */
    EINVAL,	/* ERROR_RING2_STACK_IN_USE	207 */
    ENAMETOOLONG,/* ERROR_META_EXPANSION_TOO_LONG	208 */
    EINVAL,	/* ERROR_INVALID_SIGNAL_NUMBER	209 */
    EINVAL,	/* ERROR_THREAD_1_INACTIVE	210 */
    EINVAL,	/* ERROR_INFO_NOT_AVAIL		211 */
    EACCES,	/* ERROR_LOCKED			212 */
    EINVAL,	/* ERROR_BAD_DYNALINK		213 */
    EINVAL,	/* ERROR_TOO_MANY_MODULES	214 */
    EINVAL,	/* ERROR_NESTING_NOT_ALLOWED	215 */
    EINVAL,	/* ERROR_CANNOT_SHRINK		216 */
    EINVAL,	/* ERROR_ZOMBIE_PROCESS		217 */
    EINVAL,	/* ERROR_STACK_IN_HIGH_MEMORY	218 */
    EINVAL,	/* ERROR_INVALID_EXITROUTINE_RING	219 */
    EINVAL,	/* ERROR_GETBUF_FAILED		220 */
    EINVAL,	/* ERROR_FLUSHBUF_FAILED	221 */
    EINVAL,	/* ERROR_TRANSFER_TOO_LONG	222 */
    EINVAL,	/* ERROR_FORCENOSWAP_FAILED	223 */
    EINVAL,	/* ERROR_SMG_NO_TARGET_WINDOW	224 */
    EINVAL,	/* 225 */
    EINVAL,	/* 226 */
    EINVAL,	/* 227 */
    ECHILD,	/* ERROR_NO_CHILDREN		228 */
    EINVAL,	/* ERROR_INVALID_SCREEN_GROUP	229 */
    EPIPE,	/* ERROR_BAD_PIPE		230 */
    EWOULDBLOCK,	/* ERROR_PIPE_BUSY		231 */
    EINVAL,	/* ERROR_NO_DATA		232 */
    EPIPE,	/* ERROR_PIPE_NOT_CONNECTED	233 */
    EINVAL,	/* ERROR_MORE_DATA		234 */
    EINVAL,	/*                              235 */
    EINVAL,	/*                              236 */
    EINVAL,	/*                              237 */
    EINVAL,	/*                              238 */
    EINVAL,	/*                              239 */
    EINVAL,	/*                              240 */
    EINVAL,	/*                              241 */
    EINVAL,	/*                              242 */
    EINVAL,	/*                              243 */
    EINVAL,	/*                              244 */
    EINVAL,	/*                              245 */
    EINVAL,	/*                              246 */
    EINVAL,	/*                              247 */
    EINVAL,	/*                              248 */
    EINVAL,	/*                              249 */
    EINVAL,	/* ERROR_CIRCULARITY_REQUESTED  250 */
};

static const unsigned int tableLen = sizeof(errorTable);


/*
 *----------------------------------------------------------------------
 *
 * TclOS2ConvertError --
 *
 *	This routine converts an OS/2 error into an errno value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the errno global variable.
 *
 *----------------------------------------------------------------------
 */

void
TclOS2ConvertError(errCode)
    ULONG errCode;		/* OS/2 error code. */
{
    if (errCode >= tableLen) {
	errno = EINVAL;
    } else {
	errno = errorTable[errCode];
    }
#ifdef VERBOSE
    printf("TclOS2ConvertError %d => %d\n", errCode, errno);
    printf("opened files not closed yet: %d\n", openedFiles);
    fflush(stdout);
#endif
}

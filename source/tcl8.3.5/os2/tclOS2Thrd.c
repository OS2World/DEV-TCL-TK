/* 
 * tclOS2Thread.c --
 *
 *	This file implements the OS/2-specific thread operations.
 *
 * Copyright (c) 1998 by Sun Microsystems, Inc.
 * Copyright (c) 1999 by Scriptics Corporation
 * Copyright (c) 2002 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tclOS2Int.h"

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

/*
 * Relevant notes from SMART regarding porting the Windows code to OS/2:
 *
 * EnterCriticalSection  - Cat:020 Type:010 Area:999
 * Replace EnterCriticalSection() with DosRequestMutexSem().
 * EnterCriticalSection() functions like an mutual exclusion
 * semaphore, providing serialized access to a resource.
 * The declaration of the structure CRITICAL_SECTION should
 * also be removed from the code.
 * DosCreateEventSem() must have first been called
 * to create an event semaphore.  Do not replace
 * EnterCriticalSection() with DosEnterCriticalSection().
 * DosEnterCriticalSection() will cause all threads in the
 * process to give up their timeslices, potentially degrading
 * performance.  EnterCriticalSection() does not return a
 * value; DosReleaseMutexSem() returns NO_ERROR.
 *
 * Same holds for LeaveCriticalSection() -> DosReleaseMutexSem().
 */

#ifdef TCL_THREADS

/*
 * This is the master lock used to serialize access to other
 * serialization data structures.
 */

static HMTX masterLock;
static int init = 0;
#define MASTER_LOCK  DosRequestMutexSem(masterLock, SEM_INDEFINITE_WAIT)
#define MASTER_UNLOCK  DosReleaseMutexSem(masterLock)

/*
 * This is the master lock used to serialize initialization and finalization
 * of Tcl as a whole.
 */

static HMTX initLock;

/*
 * allocLock is used by Tcl's version of malloc for synchronization.
 * For obvious reasons, cannot use any dyamically allocated storage.
 */

static HMTX allocLock;
static Tcl_Mutex allocLockPtr = (Tcl_Mutex) &allocLock;

/*
 * Condition variables are implemented with a combination of a 
 * per-thread OS/2 Event and a per-condition waiting queue.
 * The idea is that each thread has its own Event that it waits
 * on when it is doing a ConditionWait; it uses the same event for
 * all condition variables because it only waits on one at a time.
 * Each condition variable has a queue of waiting threads, and a 
 * mutex used to serialize access to this queue.
 *
 * Special thanks to David Nichols and
 * Jim Davidson for advice on the Condition Variable implementation.
 */

/*
 * The per-thread event and queue pointers.
 */

typedef struct ThreadSpecificData {
    HEV condEvent;			/* Per-thread condition event sem */
    struct ThreadSpecificData *nextPtr;	/* Queue pointers */
    struct ThreadSpecificData *prevPtr;
    HAB hab;                            /* From Warp 4, OS/2 enforces the
                                         * requirement of getting a hab and
                                         * message queue for each thread. */
    HMQ hmq;                            /* Queue for the abovementioned. */
    int flags;				/* See flags below */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * State bits for the thread.
 * OS2_THREAD_UNINIT		Uninitialized.  Must be zero because
 *				of the way ThreadSpecificData is created.
 * OS2_THREAD_RUNNING		Running, not waiting.
 * OS2_THREAD_BLOCKED		Waiting, or trying to wait.
 * OS2_THREAD_DEAD		Dying - no per-thread event anymore.
 */ 

#define OS2_THREAD_UNINIT	0x0
#define OS2_THREAD_RUNNING	0x1
#define OS2_THREAD_BLOCKED	0x2
#define OS2_THREAD_DEAD		0x4

/*
 * The per condition queue pointers and the
 * Mutex used to serialize access to the queue.
 */

typedef struct OS2Condition {
    HMTX condLock;	/* Lock to serialize queuing on the condition */
    struct ThreadSpecificData *firstPtr;	/* Queue pointers */
    struct ThreadSpecificData *lastPtr;
} OS2Condition;

static void FinalizeConditionEvent(ClientData data);

/*
 * Originally, there was one DosAllocThreadLocalMemory() for each key
 * (comparable to the TlsAlloc() in the Windows port. We'd run into the
 * maximum number of bytes (128) available for that very soon, though.
 * In the Unix port, the pthreads key API is used, so we'll follow that line.
 * The following is based on the pthreads port to OS/2 (version 0.04) by
 * Antony T Curtis <antony.curtis@olcs.net>, originally written by
 * John Birrell <jb@cimlogic.com.au>. Though it's only a small part of
 * that (and changed), I'll insert the license statement in full:
 *
 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
 * All rights reserved.
 *
 * Modified and extended by Antony T Curtis <antony.curtis@olcs.net>
 * for use with OS/2.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define PTHREAD_KEYS_MAX  256
#define _thread_kern_sig_block(a)       {DosEnterMustComplete((ULONG *)(a)); \
                                         _signals_blocked++;}
#define _thread_kern_sig_unblock(a)     {DosExitMustComplete((ULONG *)&(a)); \
                                         _signals_blocked--;}
#define _thread_run     ((pthread_t)(*_threadstore()))
#define PTHREAD_INIT if ((!_thread_initial) && (_gettid()==1)) { \
        _thread_init();                                                 \
        _thread_initial = _thread_run;                                  \
}

struct pthread_key {
        long  count;
};

struct pthread {
        const void      **specific_data;
        int             specific_data_count;
};
typedef struct  pthread                 *pthread_t;

typedef int   pthread_key_t;
/* Ptr to the first thread: */
struct pthread   * volatile _thread_initial = NULL;
static ULONG status = 0;
static int _signals_blocked;

/* Static variables: */
static struct pthread_key key_table[PTHREAD_KEYS_MAX];
/*
 * Forward declarations for functions defined later in this file.
 */
static int    pthread_key_create _ANSI_ARGS_((pthread_key_t * key,
                                              void (*destructor) (void *)));
static int    pthread_key_delete _ANSI_ARGS_((pthread_key_t key));
static inline const void ** pthread_key_allocate_data _ANSI_ARGS_((void));
static int    pthread_setspecific _ANSI_ARGS_((pthread_key_t key,
                                               const void *value));
static void * pthread_getspecific _ANSI_ARGS_((pthread_key_t key));
struct pthread * _thread_init _ANSI_ARGS_((void));

/*
 *----------------------------------------------------------------------
 *
 * Tcl_CreateThread --
 *
 *	This procedure creates a new thread.
 *
 * Results:
 *	TCL_OK if the thread could be created.  The thread ID is
 *	returned in a parameter.
 *
 * Side effects:
 *	A new thread is created.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_CreateThread(idPtr, proc, clientData, stackSize, flags)
    Tcl_ThreadId *idPtr;                /* Return, the ID of the thread */
    Tcl_ThreadCreateProc proc;          /* Main() function of the thread */
    ClientData clientData;              /* The one argument to Main() */
    int stackSize;                      /* Size of stack for the new thread */
    int flags;                          /* Flags controlling behaviour of
                                         * the new thread */
{
    if (stackSize == 0) stackSize = 0x30000;
    *idPtr = (Tcl_ThreadId) _beginthread((void *)proc, NULL, stackSize,
                                         (void *)clientData);
#ifdef VERBOSE
    printf("_beginthread proc 0x%x data 0x%x stack %d flags 0x%x returned %d\n",
           proc, clientData, stackSize, flags, *idPtr);
    fflush(stdout);
#endif
    if (*idPtr == (Tcl_ThreadId) -1) {
	return TCL_ERROR;
    } else {
	return TCL_OK;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpThreadExit --
 *
 *	This procedure terminates the current thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	This procedure terminates the current thread.
 *
 *----------------------------------------------------------------------
 */

void
TclpThreadExit(status)
    int status;
{
    _endthread();
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetCurrentThread --
 *
 *	This procedure returns the ID of the currently running thread.
 *
 * Results:
 *	A thread ID.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tcl_ThreadId
Tcl_GetCurrentThread()
{
    PPIB processInfoBlockPtr;
    PTIB threadInfoBlockPtr;
    APIRET rc;

    rc = DosGetInfoBlocks(&threadInfoBlockPtr, &processInfoBlockPtr);
    return (Tcl_ThreadId)threadInfoBlockPtr->tib_ptib2->tib2_ultid;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpInitLock
 *
 *	This procedure is used to grab a lock that serializes initialization
 *	and finalization of Tcl.  On some platforms this may also initialize
 *	the mutex used to serialize creation of more mutexes and thread
 *	local storage keys.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquire the initialization mutex.
 *
 *----------------------------------------------------------------------
 */

void
TclpInitLock()
{
    APIRET rc;

    if (!init) {
	/*
	 * There is a fundamental race here that is solved by creating
	 * the first Tcl interpreter in a single threaded environment.
	 * Once the interpreter has been created, it is safe to create
	 * more threads that create interpreters in parallel.
         * Unnamed semaphore => arg 1 NULL.
	 */
	init = 1;
        rc = DosCreateMutexSem(NULL, &initLock, DC_SEM_SHARED, FALSE);
        if (rc != NO_ERROR) {
            panic("Can't create the init Lock in TclpInitLock");
        }
        rc = DosCreateMutexSem(NULL, &masterLock, DC_SEM_SHARED, FALSE);
        if (rc != NO_ERROR) {
            panic("Can't create the master Lock in TclpInitLock");
        }
    }
    rc = DosRequestMutexSem(initLock, SEM_INDEFINITE_WAIT);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpInitUnlock
 *
 *	This procedure is used to release a lock that serializes initialization
 *	and finalization of Tcl.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Release the initialization mutex.
 *
 *----------------------------------------------------------------------
 */

void
TclpInitUnlock()
{
    APIRET rc;

    rc = DosReleaseMutexSem(initLock);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpMasterLock
 *
 *	This procedure is used to grab a lock that serializes creation
 *	of mutexes, condition variables, and thread local storage keys.
 *
 *	This lock must be different than the initLock because the
 *	initLock is held during creation of syncronization objects.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquire the master mutex.
 *
 *----------------------------------------------------------------------
 */

void
TclpMasterLock()
{
    APIRET rc;

    if (!init) {
	/*
	 * There is a fundamental race here that is solved by creating
	 * the first Tcl interpreter in a single threaded environment.
	 * Once the interpreter has been created, it is safe to create
	 * more threads that create interpreters in parallel.
         * Unnamed semaphore => arg 1 NULL.
	 */
	init = 1;
        rc = DosCreateMutexSem(NULL, &initLock, DC_SEM_SHARED, FALSE);
        rc = DosCreateMutexSem(NULL, &masterLock, DC_SEM_SHARED, FALSE);
    }
    rc = DosRequestMutexSem(masterLock, SEM_INDEFINITE_WAIT);
}
#endif /* ifdef TCL_THREADS */


/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetAllocMutex
 *
 *      This procedure returns a pointer to a statically initialized
 *      mutex for use by the memory allocator.  The allocator must
 *      use this lock, because all other locks are allocated...
 *
 * Results:
 *      A pointer to a mutex that is suitable for passing to
 *      Tcl_MutexLock and Tcl_MutexUnlock.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Mutex *
Tcl_GetAllocMutex()
{
#ifdef TCL_THREADS
    APIRET rc;

    rc = DosCreateMutexSem(NULL, &allocLock, DC_SEM_SHARED, FALSE);
    return &allocLockPtr;
#else
    return NULL;
#endif
}

#ifdef TCL_THREADS
/*
 *----------------------------------------------------------------------
 *
 * TclpMasterUnlock
 *
 *	This procedure is used to release a lock that serializes creation
 *	and deletion of synchronization objects.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Release the master mutex.
 *
 *----------------------------------------------------------------------
 */

void
TclpMasterUnlock()
{
    APIRET rc;

    rc = DosReleaseMutexSem(masterLock);
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_MutexLock --
 *
 *      This procedure is invoked to lock a mutex.  This is a self
 *      initializing mutex that is automatically finalized during
 *      Tcl_Finalize.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May block the current thread.  The mutex is aquired when
 *      this returns.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_MutexLock(mutexPtr)
    Tcl_Mutex *mutexPtr;        /* The lock */
{
    APIRET rc;
    HMTX *hmtxPtr;
    if (*mutexPtr == NULL) {
        MASTER_LOCK;

        /*
         * Double inside master lock check to avoid a race.
         */

        if (*mutexPtr == NULL) {
            hmtxPtr = (HMTX *)ckalloc(sizeof(HMTX));
            rc = DosCreateMutexSem(NULL, hmtxPtr, DC_SEM_SHARED, FALSE);
#ifdef VERBOSE
            printf("Tcl_MutexLock DosCreateMutexSem returns %d *hmtxPtr 0x%x\n",
                   rc, *hmtxPtr);
            fflush(stdout);
#endif
            *mutexPtr = (Tcl_Mutex)hmtxPtr;
            TclRememberMutex(mutexPtr);
        }
        MASTER_UNLOCK;
    }
    hmtxPtr = (HMTX *) *mutexPtr;
    rc = DosRequestMutexSem(*hmtxPtr, SEM_INDEFINITE_WAIT);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_MutexUnlock --
 *
 *      This procedure is invoked to unlock a mutex.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The mutex is released when this returns.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_MutexUnlock(mutexPtr)
    Tcl_Mutex *mutexPtr;        /* The lock */
{
    APIRET rc;

    rc = DosReleaseMutexSem(**(HMTX**)mutexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeMutex --
 *
 *	This procedure is invoked to clean up one mutex.  This is only
 *	safe to call at the end of time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The mutex list is deallocated.
 *
 *----------------------------------------------------------------------
 */

void
TclpFinalizeMutex(mutexPtr)
    Tcl_Mutex *mutexPtr;
{
    APIRET rc;

    HMTX *hmtxPtr = *(HMTX **)mutexPtr;
    if (hmtxPtr != NULL) {
        rc = DosCloseMutexSem(*hmtxPtr);
        ckfree((char *)hmtxPtr);
	*mutexPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TclpThreadDataKeyInit --
 *
 *	This procedure initializes a thread specific data block key.
 *	Each thread has table of pointers to thread specific data.
 *	all threads agree on which table entry is used by each module.
 *	this is remembered in a "data key", that is just an index into
 *	this table.  To allow self initialization, the interface
 *	passes a pointer to this key and the first thread to use
 *	the key fills in the pointer to the key.  The key should be
 *	a process-wide static.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will allocate memory the first time this process calls for
 *	this key.  In this case it modifies its argument
 *	to hold the pointer to information about the key.
 *
 *----------------------------------------------------------------------
 */

void
TclpThreadDataKeyInit(keyPtr)
    Tcl_ThreadDataKey *keyPtr;	/* Identifier for the data chunk,
				 * really (ULONG **) */
{
    pthread_key_t *pkeyPtr;

#ifdef VERBOSE
    printf("TclpThreadDataKeyInit, keyPtr %x, *keyPtr %x\n", keyPtr, *keyPtr);
    fflush(stdout);
#endif
    MASTER_LOCK;
    if (*keyPtr == NULL) {
        pkeyPtr = (pthread_key_t *)ckalloc(sizeof(pthread_key_t));
        pthread_key_create(pkeyPtr, NULL);
        *keyPtr = (Tcl_ThreadDataKey)pkeyPtr;
        TclRememberDataKey(keyPtr);
    }
    MASTER_UNLOCK;
}


/*
 *----------------------------------------------------------------------
 *
 * TclpThreadDataKeyGet --
 *
 *	This procedure returns a pointer to a block of thread local storage.
 *
 * Results:
 *	A thread-specific pointer to the data structure, or NULL
 *	if the memory has not been assigned to this key for this thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

VOID *
TclpThreadDataKeyGet(keyPtr)
    Tcl_ThreadDataKey *keyPtr;	/* Identifier for the data chunk,
				 * really (ULONG **) */
{
    pthread_key_t *pkeyPtr = *(pthread_key_t **)keyPtr;
#ifdef VERBOSE
    printf("TclpThreadDataKeyGet, keyPtr %x, *keyPtr %x\n", keyPtr, *keyPtr);
    fflush(stdout);
#endif
    if (pkeyPtr == NULL) {
        return NULL;
    } else {
        return (VOID *)pthread_getspecific(*pkeyPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TclpThreadDataKeySet --
 *
 *	This procedure sets the pointer to a block of thread local storage.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up the thread so future calls to TclpThreadDataKeyGet with
 *	this key will return the data pointer.
 *
 *----------------------------------------------------------------------
 */

void
TclpThreadDataKeySet(keyPtr, data)
    Tcl_ThreadDataKey *keyPtr;	/* Identifier for the data chunk,
				 * really (pthread_key_t **) */
    VOID *data;			/* Thread local storage */
{
    pthread_key_t *pkeyPtr = *(pthread_key_t **)keyPtr;
#ifdef VERBOSE
    printf("TclpThreadDataKeySet, keyPtr %x, *keyPtr %x, data %x\n", keyPtr,
           *keyPtr, data);
    fflush(stdout);
#endif
    pthread_setspecific(*pkeyPtr, data);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeThreadData --
 *
 *	This procedure cleans up the thread-local storage.  This is
 *	called once for each thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees up the memory.
 *
 *----------------------------------------------------------------------
 */

void
TclpFinalizeThreadData(keyPtr)
    Tcl_ThreadDataKey *keyPtr;
{
    VOID *result;
    pthread_key_t *pkeyPtr;

#ifdef VERBOSE
    printf("TclpFinalizeThreadData, keyPtr %x, *keyPtr %x\n", keyPtr, *keyPtr);
    fflush(stdout);
#endif
    if (*keyPtr != NULL) {
        pkeyPtr = *(pthread_key_t **)keyPtr;
        result = (VOID *)pthread_getspecific(*pkeyPtr);
        if (result != NULL) {
            ckfree((char *)result);
            pthread_setspecific(*pkeyPtr, (void *)NULL);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeThreadDataKey --
 *
 *	This procedure is invoked to clean up one key.  This is a
 *	process-wide storage identifier.  The thread finalization code
 *	cleans up the thread local storage itself.
 *
 *	This assumes the master lock is held.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The key is deallocated.
 *
 *----------------------------------------------------------------------
 */

void
TclpFinalizeThreadDataKey(keyPtr)
    Tcl_ThreadDataKey *keyPtr;
{
    ULONG *indexPtr;
    if (*keyPtr != NULL) {
	indexPtr = *(ULONG **)keyPtr;
/*
        rc = DosFreeThreadLocalMemory((PULONG)keyPtr);
*/
	ckfree((char *)indexPtr);
	*keyPtr = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_ConditionWait --
 *
 *	This procedure is invoked to wait on a condition variable.
 *	The mutex is automically released as part of the wait, and
 *	automatically grabbed when the condition is signaled.
 *
 *	The mutex must be held when this procedure is called.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May block the current thread.  The mutex is aquired when
 *	this returns.  Will allocate memory for a HEV
 *	and initialize this the first time this Tcl_Condition is used.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_ConditionWait(condPtr, mutexPtr, timePtr)
    Tcl_Condition *condPtr;	/* Really (OS2Condition **) */
    Tcl_Mutex *mutexPtr;	/* Really (HMTX **) */
    Tcl_Time *timePtr;		/* Timeout on waiting period */
{
    OS2Condition *os2CondPtr;	/* Per-condition queue head */
    ULONG wtime;		/* OS/2 time value */
    int timeout;		/* True if we got a timeout */
    int doExit = 0;		/* True if we need to do exit setup */
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
    APIRET rc;

    if (tsdPtr->flags & OS2_THREAD_DEAD) {
	/*
	 * No more per-thread event on which to wait.
	 */

	return;
    }

    /*
     * Self initialize the two parts of the condition.
     * The per-condition and per-thread parts need to be
     * handled independently.
     */

    if (tsdPtr->flags == OS2_THREAD_UNINIT) {
	MASTER_LOCK;

	/* 
	 * Create the per-thread event and queue pointers and HAB if running
         * under PM.
	 */

	if (tsdPtr->flags == OS2_THREAD_UNINIT) {
            /* manual reset, initially nonsignalled */
            rc = DosCreateEventSem(NULL, &tsdPtr->condEvent, 0L, FALSE);
	    tsdPtr->nextPtr = NULL;
	    tsdPtr->prevPtr = NULL;
	    tsdPtr->flags = OS2_THREAD_RUNNING;
	    doExit = 1;
	}
	MASTER_UNLOCK;

	if (doExit) {
	    /*
	     * Create a per-thread exit handler to clean up the condEvent.
	     * We must be careful do do this outside the Master Lock
	     * because Tcl_CreateThreadExitHandler uses its own
	     * ThreadSpecificData, and initializing that may drop
	     * back into the Master Lock.
	     */
	    
	    Tcl_CreateThreadExitHandler(FinalizeConditionEvent,
		    (ClientData) tsdPtr);
	}
    }

    if (*condPtr == NULL) {
	MASTER_LOCK;

	/*
	 * Initialize the per-condition queue pointers and Mutex.
	 */

	if (*condPtr == NULL) {
	    os2CondPtr = (OS2Condition *)ckalloc(sizeof(OS2Condition));
#ifdef VERBOSE
            printf("Tcl_ConditionWait ckalloc os2CondPtr %p in thread %d\n",
                   os2CondPtr, (int)Tcl_GetCurrentThread());
            fflush(stdout);
#endif
            rc = DosCreateMutexSem(NULL, &os2CondPtr->condLock, DC_SEM_SHARED,
                                   FALSE);
	    os2CondPtr->firstPtr = NULL;
	    os2CondPtr->lastPtr = NULL;
	    *condPtr = (Tcl_Condition)os2CondPtr;
	    TclRememberCondition(condPtr);
	}
	MASTER_UNLOCK;
    }
    os2CondPtr = *((OS2Condition **)condPtr);
    if (timePtr == NULL) {
	wtime = SEM_INDEFINITE_WAIT;
    } else {
	wtime = timePtr->sec * 1000 + timePtr->usec / 1000;
    }

    /*
     * Queue the thread on the condition, using
     * the per-condition lock for serialization.
     */

    tsdPtr->flags = OS2_THREAD_BLOCKED;
    tsdPtr->nextPtr = NULL;
    rc = DosRequestMutexSem(os2CondPtr->condLock, SEM_INDEFINITE_WAIT);
    tsdPtr->prevPtr = os2CondPtr->lastPtr;		/* A: */
    os2CondPtr->lastPtr = tsdPtr;
    if (tsdPtr->prevPtr != NULL) {
        tsdPtr->prevPtr->nextPtr = tsdPtr;
    }
    if (os2CondPtr->firstPtr == NULL) {
        os2CondPtr->firstPtr = tsdPtr;
    }

    /*
     * Unlock the caller's mutex and wait for the condition, or a timeout.
     * There is a minor issue here in that we don't count down the
     * timeout if we get notified, but another thread grabs the condition
     * before we do.  In that race condition we'll wait again for the
     * full timeout.  Timed waits are dubious anyway.  Either you have
     * the locking protocol wrong and are masking a deadlock,
     * or you are using conditions to pause your thread.
     */
    
    rc = DosReleaseMutexSem(**(HMTX**)mutexPtr);
    timeout = 0;
    while (!timeout && (tsdPtr->flags & OS2_THREAD_BLOCKED)) {
        ULONG postCount;
        rc = DosResetEventSem(tsdPtr->condEvent, &postCount);
        rc = DosReleaseMutexSem(os2CondPtr->condLock);
	if (DosWaitEventSem(tsdPtr->condEvent, wtime) == ERROR_TIMEOUT) {
	    timeout = 1;
	}
        rc = DosRequestMutexSem(os2CondPtr->condLock, SEM_INDEFINITE_WAIT);
    }

    /*
     * Be careful on timeouts because the signal might arrive right around
     * the time limit and someone else could have taken us off the queue.
     */
    
    if (timeout) {
	if (tsdPtr->flags & OS2_THREAD_RUNNING) {
	    timeout = 0;
	} else {
	    /*
	     * When dequeuing, we can leave the tsdPtr->nextPtr
	     * and tsdPtr->prevPtr with dangling pointers because
	     * they are reinitialilzed w/out reading them when the
	     * thread is enqueued later.
	     */

            if (os2CondPtr->firstPtr == tsdPtr) {
                os2CondPtr->firstPtr = tsdPtr->nextPtr;
            } else {
                tsdPtr->prevPtr->nextPtr = tsdPtr->nextPtr;
            }
            if (os2CondPtr->lastPtr == tsdPtr) {
                os2CondPtr->lastPtr = tsdPtr->prevPtr;
            } else {
                tsdPtr->nextPtr->prevPtr = tsdPtr->prevPtr;
            }
            tsdPtr->flags = OS2_THREAD_RUNNING;
	}
    }

    rc = DosReleaseMutexSem(os2CondPtr->condLock);
    rc = DosRequestMutexSem(**(HMTX**)mutexPtr, SEM_INDEFINITE_WAIT);
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_ConditionNotify --
 *
 *	This procedure is invoked to signal a condition variable.
 *
 *	The mutex must be held during this call to avoid races,
 *	but this interface does not enforce that.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May unblock another thread.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_ConditionNotify(condPtr)
    Tcl_Condition *condPtr;
{
    OS2Condition *os2CondPtr;
    ThreadSpecificData *tsdPtr;
    APIRET rc;
#ifdef VERBOSE
    printf("Tcl_ConditionNotify in thread %d\n", (int)Tcl_GetCurrentThread());
    fflush(stdout);
#endif
    if (condPtr != NULL) {
	os2CondPtr = *((OS2Condition **)condPtr);

        if (os2CondPtr == NULL) {
#ifdef VERBOSE
            printf("os2CondPtr NULL in thread %d\n",
                   (int)Tcl_GetCurrentThread());
            fflush(stdout);
#endif
            return;
        }

	/*
	 * Loop through all the threads waiting on the condition
	 * and notify them (i.e., broadcast semantics).  The queue
	 * manipulation is guarded by the per-condition coordinating mutex.
	 */

        rc = DosRequestMutexSem(os2CondPtr->condLock, SEM_INDEFINITE_WAIT);
#ifdef VERBOSE
        printf("Tcl_ConditionNotify: os2CondPtr %p, condLock 0x%x, rc %d\n",
               os2CondPtr, os2CondPtr->condLock, rc);
        fflush(stdout);
#endif
	while (os2CondPtr->firstPtr != NULL) {
	    tsdPtr = os2CondPtr->firstPtr;
	    os2CondPtr->firstPtr = tsdPtr->nextPtr;
	    if (os2CondPtr->lastPtr == tsdPtr) {
		os2CondPtr->lastPtr = NULL;
	    }
	    tsdPtr->flags = OS2_THREAD_RUNNING;
	    tsdPtr->nextPtr = NULL;
	    tsdPtr->prevPtr = NULL;	/* Not strictly necessary, see A: */
            rc = DosPostEventSem(tsdPtr->condEvent);
#ifdef VERBOSE
            printf("DosPostEventSem tdsPtr %p condEvent 0x%x returns %d\n",
                   tsdPtr, tsdPtr->condEvent, rc);
            fflush(stdout);
#endif
	}
#ifdef VERBOSE
        printf("before DosReleaseMutexSem os2CondPtr %p condLock 0x%x\n",
               os2CondPtr, (os2CondPtr != NULL) ? os2CondPtr->condLock : 0);
        fflush(stdout);
#endif
        rc = DosReleaseMutexSem(os2CondPtr->condLock);
    } else {
	/*
	 * Noone has used the condition variable, so there are no waiters.
	 */
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FinalizeConditionEvent --
 *
 *	This procedure is invoked to clean up the per-thread
 *	event used to implement condition waiting.
 *	This is only safe to call at the end of time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The per-thread event is closed.
 *
 *----------------------------------------------------------------------
 */

static void
FinalizeConditionEvent(data)
    ClientData data;
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)data;
    APIRET rc;

    tsdPtr->flags = OS2_THREAD_DEAD;
    rc = DosCloseMutexSem(tsdPtr->condEvent);
    WinDestroyMsgQueue(tsdPtr->hmq);
    tsdPtr->hmq= (HMQ)0;
    WinTerminate(tsdPtr->hab);
    tsdPtr->hab= (HAB)0;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeCondition --
 *
 *	This procedure is invoked to clean up a condition variable.
 *	This is only safe to call at the end of time.
 *
 *	This assumes the Master Lock is held.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The condition variable is deallocated.
 *
 *----------------------------------------------------------------------
 */

void
TclpFinalizeCondition(condPtr)
    Tcl_Condition *condPtr;
{
    OS2Condition *os2CondPtr = *((OS2Condition **)condPtr);
    APIRET rc;

    /*
     * Note - this is called long after the thread-local storage is
     * reclaimed.  The per-thread condition waiting event is
     * reclaimed earlier in a per-thread exit handler, which is
     * called before thread local storage is reclaimed.
     */

    if (os2CondPtr != NULL) {
        rc = DosCloseEventSem(os2CondPtr->condLock);
	ckfree((char *)os2CondPtr);
	*condPtr = NULL;
    }
}

int
pthread_key_create(pthread_key_t * key, void (*destructor) (void *))
{
    PTHREAD_INIT;

    for ((*key) = 0; (*key) < PTHREAD_KEYS_MAX; (*key)++) {
            if (key_table[(*key)].count == 0) {
                    key_table[(*key)].count++;
                    return (0);
            }
    }
    return (EAGAIN);
}

int
pthread_key_delete(pthread_key_t key)
{
    int             ret;
    ULONG           status = 0;

    PTHREAD_INIT;

    /* Block signals: */
    _thread_kern_sig_block(&status);

    if (key < PTHREAD_KEYS_MAX) {
            switch (key_table[key].count) {
            case 1:
                    key_table[key].count = 0;
            case 0:
                    ret = 0;
                    break;
            default:
                    ret = EBUSY;
            }
    } else {
            ret = EINVAL;
    }

    /* Unblock signals: */
    _thread_kern_sig_unblock(status);
    return (ret);
}


static inline const void **
pthread_key_allocate_data(void)
{
    const void    **new_data;

    PTHREAD_INIT;

    if ((new_data = (const void **) malloc(sizeof(void *) * PTHREAD_KEYS_MAX)) != NULL) {
            memset((void *) new_data, 0, sizeof(void *) * PTHREAD_KEYS_MAX);
    }
    return (new_data);
}

int
pthread_setspecific(pthread_key_t key, const void *value)
{
    pthread_t       pthread;
    int             ret = 0;
    int             status;

    PTHREAD_INIT;
#ifdef VERBOSE
    printf("pthread_setspecific, _thread_run %x\n", _thread_run);
    fflush(stdout);
#endif

    /* Block signals: */
    _thread_kern_sig_block(&status);

    /* Point to the running thread: */
    pthread = _thread_run;

    if ((pthread->specific_data)
        || (pthread->specific_data = pthread_key_allocate_data())) {
            if ((key < PTHREAD_KEYS_MAX) && (key_table)) {
                    if (key_table[key].count) {
                            if (pthread->specific_data[key] == NULL) {
                                    if (value != NULL) {
                                            pthread->specific_data_count++;
                                            key_table[key].count++;
                                    }
                            } else {
                                    if (value == NULL) {
                                            pthread->specific_data_count--;
                                            key_table[key].count--;
                                    }
                            }
                            pthread->specific_data[key] = value;
                            ret = 0;
                    } else {
                            ret = EINVAL;
                    }
            } else {
                    ret = EINVAL;
            }
    } else {
            ret = ENOMEM;
    }

    /* Unblock signals: */
    _thread_kern_sig_unblock(status);
    return (ret);
}

void *
pthread_getspecific(pthread_key_t key)
{
    pthread_t       pthread;
    int             status;
    void            *data;

    PTHREAD_INIT;

    /* Block signals: */
    _thread_kern_sig_block(&status);

    /* Point to the running thread: */
    pthread = _thread_run;

    /* Check for errors: */
    if (pthread == NULL) {
            /* Return an invalid argument error: */
            errno = EINVAL;
            data = NULL;
    }
    /* Check if there is specific data: */
    else if (pthread->specific_data != NULL
             && (key < PTHREAD_KEYS_MAX) && (key_table)) {
            /* Check if this key has been used before: */
            if (key_table[key].count) {
                    /* Return the value: */
                    data = (void *) pthread->specific_data[key];
            } else {
                    /*
                     * This key has not been used before, so return NULL
                     * instead:
                     */
                    data = NULL;
            }
    } else {
            /* No specific data has been created, so just return NULL: */
            data = NULL;
    }

    /* Unblock signals: */
    _thread_kern_sig_unblock(status);
    return (data);
}

struct pthread * _thread_init(void)
{
    struct pthread  *thread;
#ifdef VERBOSE
    printf("_thread_init, _thread_run %x\n", _thread_run);
    fflush(stdout);
#endif

    if (thread = _thread_run) return (thread);

    /* first time ! */
    _thread_run = thread = (pthread_t) malloc(sizeof(struct pthread));
#ifdef VERBOSE
    printf("malloced thread %x, _thread_run %x\n", thread, _thread_run);
    fflush(stdout);
#endif
    thread->specific_data = NULL;
    return (thread);
}
#endif /* TCL_THREADS */

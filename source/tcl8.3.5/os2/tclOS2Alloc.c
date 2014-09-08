/* 
 * tclOS2Alloc.c --
 *
 *	This file contains the definitions for the system memory allocation
 *	routines TclSys* used in tclAlloc.c, which cannot be handled via
 *	#define because DosAllocMem doesn't give the pointer as the return
 *	value and there's no ReAlloc either.
 *
 * Copyright (c) 1999-2002 Illya Vaes
 *
 */

#include "tclOS2Int.h"


/*
 *----------------------------------------------------------------------
 *
 * TclpSysAlloc --
 *
 *	Allocate a new block of memory from the system.
 *      There will always be a multiple of 4K actually allocated.
 *
 * Results:
 *	Returns a pointer to a new block of memory.
 *
 * Side effects:
 *	Obtains memory from system.
 *
 *----------------------------------------------------------------------
 */

void *
TclpSysAlloc(
    long size,	/* Size of block to allocate. */
    int isBin)		/* Is this a bin allocation? */
{
    PVOID memPtr;

#ifdef VERBOSE
    printf("TclpSysAlloc %d (0x%x), isBin %d\n", size, size, isBin);
        fflush(stdout);
#endif
    rc = DosAllocMem(&memPtr, (ULONG)size,
                     PAG_COMMIT | PAG_READ | PAG_WRITE | PAG_EXECUTE);
    if (rc != NO_ERROR) {
#ifdef VERBOSE
        printf("TclpSysAlloc: DosAllocMem %d ERROR %x\n", size, rc);
        fflush(stdout);
#endif
        return NULL;
    }
#ifdef VERBOSE
    printf("TclpSysAlloc: DosAllocMem %d OK: %x\n", size, memPtr);
    fflush(stdout);
#endif

    return memPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpSysFree --
 *
 *	Free memory that we allocated back to the system.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed.
 *
 *----------------------------------------------------------------------
 */

void
TclpSysFree(
    void *ptr)		/* Pointer to system memory to free. */
{   
    /* Technically, this could be done with
     * #define TclpSysFree(ptr) ((void)DosFreeMem((PVOID)ptr))
     */
    rc = DosFreeMem((PVOID)ptr);
#ifdef VERBOSE
    if (rc != NO_ERROR) {
        printf("TclpSysFree: DosFreeMem %x ERROR %x\n", ptr, rc);
    } else {
        printf("TclpSysFree: DosFreeMem %x OK\n", ptr);
    }
    fflush(stdout);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TclpSysRealloc --
 *
 *	This function reallocates a chunk of system memory.
 *
 * Results:
 *	Returns a pointer to the newly allocated block.
 *
 * Side effects:
 *	May copy the contents of the original block to the new block
 *	and deallocate the original block.
 *
 *----------------------------------------------------------------------
 */

void *
TclpSysRealloc(
    void *oldPtr,		/* Original block. */
    unsigned int size)	/* New size of block. */
{   
#ifdef VERBOSE
    printf("TclpSysRealloc\n");
    fflush(stdout);
#endif
    /* We don't know the size of the block, so free and re-allocate */
    TclpSysFree(oldPtr);
    return TclpSysAlloc(size, 1);
}

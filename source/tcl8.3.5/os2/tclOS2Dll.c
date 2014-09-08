/* 
 * tclOS2Dll.c --
 *
 *	This file contains the DLL entry point.
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 * Copyright (c) 1998-2000 Scriptics Corporation.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tclOS2Int.h"

static void           MemoryCleanup _ANSI_ARGS_((ClientData clientData));

/*
 * The following data structure is used to keep track of all of the memory
 * alloced for environ by Tcl so that it can be freed when the dll is unloaded.
 */

typedef struct AllocedMemItem {
    unsigned long handle;
    char **environ;
    int nrEntries;
} AllocedMemItem;

static HMODULE tclInstance;   /* Global library instance handle. */

#ifndef STATIC_BUILD


/*
 *----------------------------------------------------------------------
 *
 * _DLL_InitTerm --
 *
 *	DLL entry point.
 *
 * Results:
 *	TRUE on sucess, FALSE on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
unsigned long
_DLL_InitTerm(
    unsigned long hInst,	/* Library instance handle. */
    unsigned long reason	/* Reason this function is being called. */
)
{
    switch (reason) {
    case 0: {	/* INIT */
        PPIB pibPtr;
        PTIB tibPtr;
        AllocedMemItem *ptr;

        /* Add to the list for alloced memory */
        ptr = (AllocedMemItem*) ckalloc(sizeof(AllocedMemItem));
        if (ptr == NULL) return FALSE;
        ptr->handle = hInst;
        ptr->nrEntries = 0;
        ptr->environ = NULL;
        /* Queue undeleted files for removal on exiting Tcl */
        Tcl_CreateExitHandler(MemoryCleanup, (ClientData)ptr);

        /* Let Tcl know our handle */
	tclInstance = (HMODULE)hInst;

        /* Fill environ */
        rc = DosGetInfoBlocks(&tibPtr, &pibPtr);
        if (environ == NULL) {
            /* Determine length of environment */
            BOOL lastString = FALSE;
            PCHAR nextString = pibPtr->pib_pchenv;
            int length = 0;
#ifdef VERBOSE
            int envSize = 0;
            printf("Copying environ...\n");
            fflush(stdout);
#endif
            if (nextString == NULL || *nextString == 0) {
                lastString = TRUE;
            }
            while (!lastString) {
                length = strlen(nextString);
#ifdef VERBOSE
                envSize += length+1;
#endif
                nextString += length+1;
                ptr->nrEntries++;
                if (*nextString == 0) lastString = TRUE;
            }
#ifdef VERBOSE
            printf("envSize %d\n", envSize);
            fflush(stdout);
#endif
            if (ptr->nrEntries > 0) {
                PCHAR copyString;
                int count = 0;
                environ = (char **) ckalloc(ptr->nrEntries * sizeof(char *));
                if (environ == NULL) return FALSE;
#ifdef VERBOSE
                printf("ckalloced environ %d\n", ptr->nrEntries*sizeof(char *));
                fflush(stdout);
#endif
                ptr->environ = environ;
                lastString = FALSE;
                nextString = pibPtr->pib_pchenv;
                while (!lastString) {
                    length = strlen(nextString);
                    copyString = ckalloc(length * sizeof(char) + 1);
#ifdef VERBOSE
                    printf("ckalloced copyString %d\n", length* sizeof(char)+1);
                    fflush(stdout);
#endif
                    if (copyString == NULL) return FALSE;
                    strncpy(copyString, nextString, length);
                    environ[count] = copyString;
                    count++;
                    nextString += length+1;
                    if (*nextString == 0) lastString = TRUE;
                }
            }
#ifdef VERBOSE
        } else {
            printf("Not copying environ\n");
            fflush(stdout);
#endif
        }

        TclOS2Init(hInst);

        return TRUE; 
    }

    case 1: {	/* TERM */

        if (hInst == tclInstance) {
            Tcl_Finalize();
        }

        return TRUE; 
    }

    }

    return FALSE; 
}

#endif /* !STATIC_BUILD */

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetTclInstance --
 *
 *      Retrieves the global library instance handle.
 *
 * Results:
 *      Returns the global library instance handle.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

HMODULE
TclOS2GetTclInstance()
{
    return tclInstance;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2Init --
 *
 *      This function initializes the internal state of the tcl library.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Initializes the tclPlatformId variable.
 *
 *----------------------------------------------------------------------
 */

void
TclOS2Init(hInst)
    HMODULE hInst;            /* Library instance handle. */
{
    tclInstance = hInst;
}

/*
 *-------------------------------------------------------------------------
 *
 * TclOS2NoBackslash --
 *
 *      Change the backslashes to slashes for use in Tcl.
 *
 * Results:
 *      All backslashes in given string are changed to slashes.
 *
 * Side effects:
 *      None.
 *
 *-------------------------------------------------------------------------
 */

char *
TclOS2NoBackslash(
    char *path)                 /* String to change. */
{
    char *p;

    for (p = path; *p != '\0'; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return path;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpCheckStackSpace --
 *
 *      Detect if we are about to blow the stack.  Called before an
 *      evaluation can happen when nesting depth is checked.
 *
 * Results:
 *      1 if there is enough stack space to continue; 0 if not.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TclpCheckStackSpace()
{
    /*
     * This function is unimplemented on OS/2.
     */

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetPlatform --
 *
 *      This is a kludge that allows the test library to get access
 *      the internal tclPlatform variable.
 *
 * Results:
 *      Returns a pointer to the tclPlatform variable.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

TclPlatformType *
TclOS2GetPlatform()
{
    return &tclPlatform;
}

/*
 *----------------------------------------------------------------------
 *
 * MemoryCleanup
 *
 *      This procedure is a Tcl_ExitProc used to clean up the left-over
 *      memory for the environ array.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Deallocates storage used by environ.
 *
 *----------------------------------------------------------------------
 */

static void
MemoryCleanup(clientData)
    ClientData clientData;      /* Address of AllocedMemItem structure */
{
    APIRET rc;
    AllocedMemItem *ptr = (AllocedMemItem *)  clientData;

    /* Free memory */
    while (ptr->nrEntries > 0) {
#ifdef VERBOSE
        printf("ckfree entry %d [%s]\n", ptr->nrEntries - 1,
               ptr->environ[ptr->nrEntries - 1]);
        fflush(stdout);
#endif
        ckfree(ptr->environ[ptr->nrEntries - 1]);
        ptr->nrEntries--;
    }
    ckfree((char *)ptr->environ);
#ifdef VERBOSE
    printf("ckfree-ed environ\n");
    fflush(stdout);
#endif
}

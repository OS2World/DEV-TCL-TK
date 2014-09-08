/* 
 * tkOS2Dll.c --
 *
 *	This file contains a stub dll entry point.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tkOS2Int.h"

/* Save the Tk DLL handle for TkPerl */
unsigned long dllHandle = (unsigned long) NULLHANDLE;


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

unsigned long _DLL_InitTerm(unsigned long modHandle, unsigned long flag)
{
    /*
     * If we are attaching to the DLL from a new process, tell Tk about
     * the handle to use. If we are detaching then clean up any
     * data structures related to this DLL.
     */

    switch (flag) {
    case 0: {    /* INIT */

#ifdef __cplusplus
        __ctordtorInit();
#endif

#ifndef USE_TCL_STUBS
        TkOS2XInit(modHandle);
#endif /* USE_TCL_STUBS */
        /* Save handle */
        dllHandle = modHandle;
        return TRUE;
    }

    case 1:     /* TERM */
        TkOS2XCleanup(dllHandle);
        TkOS2ExitPM();
        /* Invalidate handle */
        dllHandle = (unsigned long)NULLHANDLE;

#ifdef __cplusplus
        __ctordtorTerm();
#endif
        return TRUE;
    }

    return FALSE;
}

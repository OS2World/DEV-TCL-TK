/* 
 * tclOS2Load.c --
 *
 *	This procedure provides a version of the TclpLoadFile that
 *	works with the OS/2 "DosLoadModule" and "DosQueryProcAddr"
 *	APIs for dynamic loading.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tclOS2Int.h"


/*
 *----------------------------------------------------------------------
 *
 * TclpLoadFile --
 *
 *	Dynamically loads a binary code file into memory and returns
 *	the addresses of two procedures within that file, if they
 *	are defined.
 *
 * Results:
 *	A standard Tcl completion code.  If an error occurs, an error
 *	message is left in the interp's result.
 *
 * Side effects:
 *	New code suddenly appears in memory.
 *
 *----------------------------------------------------------------------
 */

int
TclpLoadFile(interp, fileName, sym1, sym2, proc1Ptr, proc2Ptr, clientDataPtr)
    Tcl_Interp *interp;		/* Used for error reporting. */
    char *fileName;		/* Name of the file containing the desired
				 * code. */
    char *sym1, *sym2;		/* Names of two procedures to look up in
				 * the file's symbol table. */
    Tcl_PackageInitProc **proc1Ptr, **proc2Ptr;
				/* Where to return the addresses corresponding
				 * to sym1 and sym2. */
    ClientData *clientDataPtr;  /* Filled with token for dynamically loaded
                                 * file which will be passed back to
                                 * TclpUnloadFile() to unload the file. */
{
    HMODULE handle;
    UCHAR LoadError[256];       /* Area for name of DLL that we failed on */
    char *nativeName;
    Tcl_DString ds;

    nativeName = Tcl_UtfToExternalDString(NULL, fileName, -1, &ds);
#ifdef VERBOSE
    printf("TclpLoadFile %s %s %s\n", nativeName, sym1, sym2);
    fflush(stdout);
#endif
    rc = DosLoadModule((PSZ)LoadError, sizeof(LoadError), (PSZ)nativeName,
                       &handle);

    *clientDataPtr = (ClientData) handle;

    if (rc != NO_ERROR) {
	Tcl_AppendResult(interp, "couldn't load library \"", nativeName,
		"\": ", (char *) NULL);
        switch (rc) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " could not be found in library path",
                        (char *) NULL);
                break;
            case ERROR_INVALID_NAME:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid name", (char *) NULL);
                break;
            case ERROR_PROC_NOT_FOUND:
                Tcl_AppendResult(interp, "could not find specified procedure",
                        (char *) NULL);
                break;
            case ERROR_INVALID_SEGMENT_NUMBER:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid segment number", (char *) NULL);
                break;
            case ERROR_INVALID_ORDINAL:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid ordinal", (char *) NULL);
                break;
            case ERROR_INVALID_MODULETYPE:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid module type", (char *) NULL);
                break;
            case ERROR_INVALID_EXE_SIGNATURE:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid executable signature", (char *) NULL);
                break;
            case ERROR_EXE_MARKED_INVALID:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " is marked invalid (link-errors)", (char *) NULL);
                break;
            case ERROR_ITERATED_DATA_EXCEEDS_64K:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has iterated data exceeding 64K", (char *) NULL);
                break;
            case ERROR_INVALID_MINALLOCSIZE:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid minimum allocation size",
                        (char *) NULL);
                break;
            case ERROR_DYNLINK_FROM_INVALID_RING:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " cannot be dynamically linked to from privilege",
                        " level 2", (char *) NULL);
                break;
            case ERROR_INVALID_SEGDPL:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid segment descriptor privilege level",
                        " - can only have levels 2 and 3", (char *) NULL);
                break;
            case ERROR_AUTODATASEG_EXCEEDS_64K:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has automatic data segment exceeding 64K",
                        (char *) NULL);
                break;
            case ERROR_FILENAME_EXCED_RANGE:
                Tcl_AppendResult(interp, "this library or a dependent library",
                        " has an invalid name or extension greater than",
                        " 8.3 characters", (char *) NULL);
                break;
            case ERROR_INIT_ROUTINE_FAILED:
                Tcl_AppendResult(interp, "the library initialization",
                        " routine failed", (char *) NULL);
                break;
            default:
#ifdef VERBOSE
                printf("DosLoadModule %s ERROR %d on %s\n", nativeName, rc,
                       LoadError);
                fflush(stdout);
#endif
                Tcl_AppendResult(interp, Tcl_PosixError(interp),
                        (char *) NULL);
                TclOS2ConvertError(rc);
        }
	return TCL_ERROR;
    }

    /*
     * For each symbol, check for both Symbol and _Symbol, since some
     * compilers generate C symbols with a leading '_' by default.
     */

    rc = DosQueryProcAddr(handle, 0L, sym1, (PFN *)proc1Ptr);
    if (rc != NO_ERROR) {
#ifdef VERBOSE
        printf("DosQueryProcAddr %s ERROR %d\n", sym1, rc);
#endif
        Tcl_DStringAppend(&ds, "_", 1);
        sym1 = Tcl_DStringAppend(&ds, sym1, -1);
        rc = DosQueryProcAddr(handle, 0L, sym1, (PFN *)proc1Ptr);
        if (rc != NO_ERROR) {
#ifdef VERBOSE
            printf("DosQueryProcAddr %s ERROR %d\n", sym1, rc);
#endif
            *proc1Ptr = NULL;
        }
#ifdef VERBOSE
          else {
            printf("DosQueryProcAddr %s OK\n", sym1, rc);
        }
#endif
        Tcl_DStringFree(&ds);
    }
#ifdef VERBOSE
      else {
        printf("DosQueryProcAddr %s OK\n", sym1, rc);
    }
#endif

    rc = DosQueryProcAddr(handle, 0L, sym2, (PFN *)proc2Ptr);
    if (rc != NO_ERROR) {
#ifdef VERBOSE
        printf("DosQueryProcAddr %s ERROR %d\n", sym2, rc);
#endif
        Tcl_DStringAppend(&ds, "_", 1);
        sym2 = Tcl_DStringAppend(&ds, sym2, -1);
        rc = DosQueryProcAddr(handle, 0L, sym2, (PFN *)proc2Ptr);
        if (rc != NO_ERROR) {
#ifdef VERBOSE
            printf("DosQueryProcAddr %s ERROR %d\n", sym2, rc);
#endif
            *proc2Ptr = NULL;
        }
#ifdef VERBOSE
          else {
            printf("DosQueryProcAddr %s OK\n", sym2, rc);
        }
#endif
        Tcl_DStringFree(&ds);
    }
#ifdef VERBOSE
      else {
        printf("DosQueryProcAddr %s OK\n", sym2, rc);
    }
#endif
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpUnloadFile --
 *
 *      Unloads a dynamically loaded binary code file from memory.
 *      Code pointers in the formerly loaded file are no longer valid
 *      after calling this function.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Code removed from memory.
 *
 *----------------------------------------------------------------------
 */

void
TclpUnloadFile(clientData)
    ClientData clientData;      /* ClientData returned by a previous call
                                 * to TclpLoadFile().  The clientData is
                                 * a token that represents the loaded
                                 * file. */
{
    DosFreeModule((HMODULE) clientData);
}

/*
 *----------------------------------------------------------------------
 *
 * TclGuessPackageName --
 *
 *      If the "load" command is invoked without providing a package
 *      name, this procedure is invoked to try to figure it out.
 *
 * Results:
 *      Always returns 0 to indicate that we couldn't figure out a
 *      package name;  generic code will then try to guess the package
 *      from the file name.  A return value of 1 would have meant that
 *      we figured out the package name and put it in bufPtr.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TclGuessPackageName(fileName, bufPtr)
    char *fileName;             /* Name of file containing package (already
                                 * translated to local form if needed). */
    Tcl_DString *bufPtr;        /* Initialized empty dstring.  Append
                                 * package name to this if possible. */
{
    return 0;
}

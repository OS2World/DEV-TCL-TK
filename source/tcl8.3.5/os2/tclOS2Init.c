/* 
 * tclOS2Init.c --
 *
 *	Contains the OS/2-specific interpreter initialization functions.
 *
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 by Scriptics Corporation.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tclOS2Int.h"

/*
 * The following macro can be defined at compile time to specify
 * the Tcl profile key.
 */

#ifndef TCL_REGISTRY_KEY
/*
#define TCL_REGISTRY_KEY "Software\\Scriptics\\Tcl\\" TCL_VERSION
*/
#define TCL_REGISTRY_KEY "Tcl" TCL_PATCH_LEVEL
#endif

/* How many file handles do we want? OS/2 default is 20 */
#define MAX_FH ((ULONG) 25)

/* Global PM variables, necessary because of event loop and thus console */
static HMTX globalsLock;
#define GLOBALS_LOCK  DosRequestMutexSem(globalsLock, SEM_INDEFINITE_WAIT)
#define GLOBALS_UNLOCK  DosReleaseMutexSem(globalsLock)
static BOOL usePm = TRUE;
static HAB tclHab= (HAB)0;
static HMQ tclHmq= (HMQ)0;
/* Other global variables */
/* THREAD SECURITY? */
ULONG maxPath;
LONG rc;
ULONG sysInfo[QSV_MAX];   /* System Information Data Buffer */
#ifdef VERBOSE
int openedFiles = 0;	/* Files opened by us with DosOpen/DosDupHandle */
#endif
typedef struct ThreadSpecificData {
    HAB tclHab;
    HMQ tclHmq;
    BOOL PmInitialized;
    BOOL usePm;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

/*
 * The following variable remembers if we've already initialized PM.
 */

static BOOL PmInitialized = FALSE;


/*
 * The following macros define the possible platforms (quite a bit too
 * optimistic for OS/2...).
 */

#ifndef PROCESSOR_ARCHITECTURE_INTEL
#define PROCESSOR_ARCHITECTURE_INTEL 0
#endif
#ifndef PROCESSOR_ARCHITECTURE_MIPS
#define PROCESSOR_ARCHITECTURE_MIPS  1
#endif
#ifndef PROCESSOR_ARCHITECTURE_ALPHA
#define PROCESSOR_ARCHITECTURE_ALPHA 2
#endif
#ifndef PROCESSOR_ARCHITECTURE_PPC
#define PROCESSOR_ARCHITECTURE_PPC   3
#endif
#ifndef PROCESSOR_ARCHITECTURE_SHX
#define PROCESSOR_ARCHITECTURE_SHX   4
#endif
#ifndef PROCESSOR_ARCHITECTURE_ARM
#define PROCESSOR_ARCHITECTURE_ARM   5
#endif
#ifndef PROCESSOR_ARCHITECTURE_IA64
#define PROCESSOR_ARCHITECTURE_IA64  6
#endif
#ifndef PROCESSOR_ARCHITECTURE_ALPHA64
#define PROCESSOR_ARCHITECTURE_ALPHA64 7
#endif
#ifndef PROCESSOR_ARCHITECTURE_MSIL
#define PROCESSOR_ARCHITECTURE_MSIL  8
#endif
#ifndef PROCESSOR_ARCHITECTURE_UNKNOWN
#define PROCESSOR_ARCHITECTURE_UNKNOWN 0xFFFF
#endif

/*
 * The following arrays contain the human readable strings for the OS/2
 * platform and processor values.
 */


#define NUMPLATFORMS 1
static char* platforms[NUMPLATFORMS] = {
    "OS/2"
};

#define NUMPROCESSORS 9
static char* processors[NUMPROCESSORS] = {
    "intel", "mips", "alpha", "ppc", "shx", "arm", "ia64", "alpha64", "msil"
};

/* Used to store the encoding used for binary files */
static Tcl_Encoding binaryEncoding = NULL;
/* Has the basic library path encoding issue been fixed */
static int libraryPathEncodingFixed = 0;

/*
 * The Init script (common to Windows, OS/2 and Unix platforms) is
 * defined in generic/tclInitScript.h
 */

#include "tclInitScript.h"


/*
 *----------------------------------------------------------------------
 *
 * TclpInitPlatform --
 *
 *      Initialize all the platform-dependant things like signals and
 *      floating-point error handling.
 *
 *      Called at process initialization time.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TclpInitPlatform()
{
    tclPlatform = TCL_PLATFORM_OS2;

    /*
     * The following code stops OS/2 from automatically putting up Hard
     * Error pop-ups, e.g, when someone tries to access a file that is
     * locked or a drive with no disk in it.  Tcl already returns the
     * appropriate error to the caller, and they can decide to put up
     * their own dialog in response to that failure.
     */

    DosError(FERR_DISABLEHARDERR);

    /* Create lock to protect global variables */
    rc = DosCreateMutexSem(NULL, &globalsLock, DC_SEM_SHARED, FALSE);
    if (rc != NO_ERROR) {
        panic("Can't create the globals Lock in TclpInitPlatform");
    }

#ifdef STATIC_BUILD
    /*
     * If we are in a statically linked executable, then we need to
     * explicitly initialize here since _DLL_InitTerm() will not be invoked.
     */

    TclOS2Init((HMODULE)pibPtr->pib_hmte);
#endif
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpInitLibraryPath --
 *
 *      Initialize the library path at startup.
 *
 *      This call sets the library path to strings in UTF-8. Any
 *      pre-existing library path information is assumed to have been
 *      in the native multibyte encoding.
 *
 *      Called at process initialization time.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

void
TclpInitLibraryPath(path)
    CONST char *path;           /* Potentially dirty UTF string that is */
                                /* the path to the executable name.     */
{
#define LIBRARY_SIZE        32
    Tcl_Obj *pathPtr, *objPtr;
    char *str;
    Tcl_DString buffer, ds;
    int pathc;
    char **pathv;
    char installLib[LIBRARY_SIZE], developLib[LIBRARY_SIZE];
    char dllName[(MAX_PATH + LIBRARY_SIZE) * TCL_UTF_MAX + 1];

    Tcl_DStringInit(&ds);
    pathPtr = Tcl_NewObj();

    /*
     * Initialize the substrings used when locating an executable.  The
     * installLib variable computes the path as though the executable
     * is installed.  The developLib computes the path as though the
     * executable is run from a development directory.
     */

    sprintf(installLib, "lib/tcl%s", TCL_VERSION);
    sprintf(developLib, "../tcl%s/library",
            ((TCL_RELEASE_LEVEL < 2) ? TCL_PATCH_LEVEL : TCL_VERSION));

    /*
     * Look for the library relative to default encoding dir.
     */

    str = Tcl_GetDefaultEncodingDir();
    if ((str != NULL) && (str[0] != '\0')) {
        objPtr = Tcl_NewStringObj(str, -1);
        Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
    }

    /*
     * Look for the library relative to the TCL_LIBRARY env variable.
     * If the last dirname in the TCL_LIBRARY path does not match the
     * last dirname in the installLib variable, use the last dir name
     * of installLib in addition to the orginal TCL_LIBRARY path.
     */

    str = getenv("TCL_LIBRARY");                        /* INTL: Native. */
    Tcl_ExternalToUtfDString(NULL, str, -1, &buffer);
    str = Tcl_DStringValue(&buffer);

    if ((str != NULL) && (str[0] != '\0')) {
        /*
         * If TCL_LIBRARY is set, search there.
         */

        objPtr = Tcl_NewStringObj(str, -1);
        Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);

        Tcl_SplitPath(str, &pathc, &pathv);
        if ((pathc > 0) && (stricmp(installLib + 4, pathv[pathc-1]) != 0)) {
            /*
             * If TCL_LIBRARY is set but refers to a different tcl
             * installation than the current version, try fiddling with the
             * specified directory to make it refer to this installation by
             * removing the old "tclX.Y" and substituting the current
             * version string.
             */

            pathv[pathc - 1] = installLib + 4;
            str = Tcl_JoinPath(pathc, pathv, &ds);
            objPtr = Tcl_NewStringObj(str, Tcl_DStringLength(&ds));
            Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
            Tcl_DStringFree(&ds);
        }
        ckfree((char *) pathv);
    }
    Tcl_DStringFree(&buffer);

    /*
     * Look for the library relative to the DLL.  Only use the installLib
     * because in practice, the DLL is always installed.
     */

    rc = DosQueryModuleName(TclOS2GetTclInstance(), MAX_PATH, dllName);
    if (installLib != NULL) {
        char *end, *p;

        end = strrchr(dllName, '\\');
        *end = '\0';
        p = strrchr(dllName, '\\');
        if (p != NULL) {
            end = p;
        }
        *end = '\\';
        strcpy(end + 1, installLib);
    }
    TclOS2NoBackslash(dllName);
    Tcl_ListObjAppendElement(NULL, pathPtr, Tcl_NewStringObj(dllName, -1));

    /*
     * Look for the library relative to the executable.  This algorithm
     * should be the same as the one in the tcl_findLibrary procedure.
     *
     * This code looks in the following directories:
     *
     *  <bindir>/../<installLib>
     *         (e.g. /usr/local/bin/../lib/tcl8.2)
     *  <bindir>/../../<installLib>
     *         (e.g. /usr/local/TclPro/solaris-sparc/bin/../../lib/tcl8.2)
     *  <bindir>/../library
     *         (e.g. /usr/src/tcl8.2/unix/../library)
     *  <bindir>/../../library
     *         (e.g. /usr/src/tcl8.2/unix/solaris-sparc/../../library)
     *  <bindir>/../../<developLib>
     *         (e.g. /usr/src/tcl8.2/unix/../../tcl8.2/library)
     *  <bindir>/../../../<devlopLib>
     *         (e.g. /usr/src/tcl8.2/unix/solaris-sparc/../../../tcl8.2/library)
     */

    /*
     * The variable path holds an absolute path.  Take care not to
     * overwrite pathv[0] since that might produce a relative path.
     */

    if (path != NULL) {
        Tcl_SplitPath(path, &pathc, &pathv);
        if (pathc > 2) {
            str = pathv[pathc - 2];
            pathv[pathc - 2] = installLib;
            path = Tcl_JoinPath(pathc - 1, pathv, &ds);
            pathv[pathc - 2] = str;
            objPtr = Tcl_NewStringObj(path, Tcl_DStringLength(&ds));
            Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
            Tcl_DStringFree(&ds);
        }
        if (pathc > 3) {
            str = pathv[pathc - 3];
            pathv[pathc - 3] = installLib;
            path = Tcl_JoinPath(pathc - 2, pathv, &ds);
            pathv[pathc - 3] = str;
            objPtr = Tcl_NewStringObj(path, Tcl_DStringLength(&ds));
            Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
            Tcl_DStringFree(&ds);
        }
        if (pathc > 2) {
            str = pathv[pathc - 2];
            pathv[pathc - 2] = "library";
            path = Tcl_JoinPath(pathc - 1, pathv, &ds);
            pathv[pathc - 2] = str;
            objPtr = Tcl_NewStringObj(path, Tcl_DStringLength(&ds));
            Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
            Tcl_DStringFree(&ds);
        }
        if (pathc > 3) {
            str = pathv[pathc - 3];
            pathv[pathc - 3] = "library";
            path = Tcl_JoinPath(pathc - 2, pathv, &ds);
            pathv[pathc - 3] = str;
            objPtr = Tcl_NewStringObj(path, Tcl_DStringLength(&ds));
            Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
            Tcl_DStringFree(&ds);
        }
        if (pathc > 3) {
            str = pathv[pathc - 3];
            pathv[pathc - 3] = developLib;
            path = Tcl_JoinPath(pathc - 2, pathv, &ds);
            pathv[pathc - 3] = str;
            objPtr = Tcl_NewStringObj(path, Tcl_DStringLength(&ds));
            Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
            Tcl_DStringFree(&ds);
        }
        if (pathc > 4) {
            str = pathv[pathc - 4];
            pathv[pathc - 4] = developLib;
            path = Tcl_JoinPath(pathc - 3, pathv, &ds);
            pathv[pathc - 4] = str;
            objPtr = Tcl_NewStringObj(path, Tcl_DStringLength(&ds));
            Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
            Tcl_DStringFree(&ds);
        }
        ckfree((char *) pathv);
    }

    TclSetLibraryPath(pathPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpSetInitialEncodings --
 *
 *      Based on the locale, determine the encoding of the operating
 *      system and the default encoding for newly opened files.
 *
 *      Called at process initialization time, and part way through
 *      startup, we verify that the initial encodings were correctly
 *      setup.  Depending on Tcl's environment, there may not have been
 *      enough information first time through (above).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The Tcl library path is converted from native encoding to UTF-8,
 *      on the first call, and the encodings may be changed on first or
 *      second call.
 *
 *---------------------------------------------------------------------------
 */

void
TclpSetInitialEncodings()
{
    CONST char *encoding;
    char buf[4 + TCL_INTEGER_SPACE];
    ULONG currentCodePages[1], lenCodePages;

    rc = DosQueryCp(sizeof(currentCodePages), currentCodePages, &lenCodePages);
    if (rc != NO_ERROR) {
        Tcl_SetSystemEncoding(NULL, NULL);
    } else {
        sprintf(buf, "cp%ld", currentCodePages[0]);
        Tcl_SetSystemEncoding(NULL, buf);
    }

    if (libraryPathEncodingFixed == 0) {
        Tcl_Obj *pathPtr = TclGetLibraryPath();
        if (pathPtr != NULL) {
            int i, objc;
            Tcl_Obj **objv;

            objc = 0;
            Tcl_ListObjGetElements(NULL, pathPtr, &objc, &objv);
            for (i = 0; i < objc; i++) {
                int length;
                char *string;
                Tcl_DString ds;

                string = Tcl_GetStringFromObj(objv[i], &length);
                Tcl_ExternalToUtfDString(NULL, string, length, &ds);
                Tcl_SetStringObj(objv[i], Tcl_DStringValue(&ds),
                                 Tcl_DStringLength(&ds));
                Tcl_DStringFree(&ds);
            }
        }
        libraryPathEncodingFixed = 1;
    }

    /* This is only ever called from the startup thread */
    if (binaryEncoding == NULL) {
        /*
         * Keep this encoding preloaded.  The IO package uses it for
         * gets on a binary channel.
         */
        encoding = "iso8859-1";
        binaryEncoding = Tcl_GetEncoding(NULL, encoding);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpSetVariables --
 *
 *      Performs platform-specific interpreter initialization related to
 *      the tcl_platform and env variables, and other platform-specific
 *      things.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets "tcl_platform", and "env(HOME)" Tcl variables.
 *
 *----------------------------------------------------------------------
 */

void
TclpSetVariables(interp)
    Tcl_Interp *interp;         /* Interp to initialize. */
{
    char *ptr;
    char buffer[TCL_INTEGER_SPACE * 2];
    Tcl_DString ds;

    /* Request all available system information */
    rc= DosQuerySysInfo (1L, QSV_MAX, (PVOID)sysInfo, sizeof(ULONG)*QSV_MAX);
    maxPath = sysInfo[QSV_MAX_PATH_LENGTH - 1];
#ifdef VERBOSE
    printf("major version [%d], minor version [%d], rev. [%d], maxPath [%d]\n",
           sysInfo[QSV_VERSION_MAJOR - 1], sysInfo[QSV_VERSION_MINOR - 1],
           sysInfo[QSV_VERSION_REVISION - 1], sysInfo[QSV_MAX_PATH_LENGTH - 1]);
#endif

    /*
     * Define the tcl_platform array.
     */

    Tcl_SetVar2(interp, "tcl_platform", "platform", "os2", TCL_GLOBAL_ONLY);
    Tcl_SetVar2(interp, "tcl_platform", "os", "OS/2", TCL_GLOBAL_ONLY);
    /*
     * IBM hack for LX-versions above 2.11
     *  OS/2 version    MAJOR MINOR
     *  2.0             20    0
     *  2.1             20    10
     *  2.11            20    11
     *  3.0             20    30
     *  4.0             20    40
     */
    if (sysInfo[QSV_VERSION_MAJOR-1]==20 && sysInfo[QSV_VERSION_MINOR-1] > 11) {
        int major = (int) (sysInfo[QSV_VERSION_MINOR - 1] / 10);
        sprintf(buffer, "%d.%d", major,
                (int) sysInfo[QSV_VERSION_MINOR - 1] - major * 10);
    } else {
        sprintf(buffer, "%d.%d", (int) (sysInfo[QSV_VERSION_MAJOR - 1] / 10),
                (int)sysInfo[QSV_VERSION_MINOR - 1]);
    }
    Tcl_SetVar2(interp, "tcl_platform", "osVersion", buffer, TCL_GLOBAL_ONLY);
    /* No API for determining processor (yet) */
    Tcl_SetVar2(interp, "tcl_platform", "machine",
                processors[PROCESSOR_ARCHITECTURE_INTEL], TCL_GLOBAL_ONLY);

#ifdef _DEBUG
    /*
     * The existence of the "debug" element of the tcl_platform array indicates
     * that this particular Tcl shell has been compiled with debug information.
     * Using "info exists tcl_platform(debug)" a Tcl script can direct the
     * interpreter to load debug versions of DLLs with the load command.
     */

    Tcl_SetVar2(interp, "tcl_platform", "debug", "1",
            TCL_GLOBAL_ONLY);
#endif

    /*
     * Set up the HOME environment variable from the HOMEDRIVE & HOMEPATH
     * environment variables, if necessary.
     */

    Tcl_DStringInit(&ds);
    ptr = Tcl_GetVar2(interp, "env", "HOME", TCL_GLOBAL_ONLY);
    if (ptr == NULL) {
        ptr = Tcl_GetVar2(interp, "env", "HOMEDRIVE", TCL_GLOBAL_ONLY);
        if (ptr != NULL) {
            Tcl_DStringAppend(&ds, ptr, -1);
        }
        ptr = Tcl_GetVar2(interp, "env", "HOMEPATH", TCL_GLOBAL_ONLY);
        if (ptr != NULL) {
            Tcl_DStringAppend(&ds, ptr, -1);
        }
        if (Tcl_DStringLength(&ds) > 0) {
            Tcl_SetVar2(interp, "env", "HOME", Tcl_DStringValue(&ds),
                    TCL_GLOBAL_ONLY);
        } else {
            Tcl_SetVar2(interp, "env", "HOME", "c:/", TCL_GLOBAL_ONLY);
        }
    }

    /*
     * Initialize the user name from the environment first, since this is much
     * faster than asking the system.
     */

    Tcl_DStringSetLength(&ds, 100);
    if (TclGetEnv("USERNAME", &ds) == NULL) {
        Tcl_DStringSetLength(&ds, 0);
    }
    Tcl_SetVar2(interp, "tcl_platform", "user", Tcl_DStringValue(&ds),
            TCL_GLOBAL_ONLY);
    Tcl_DStringFree(&ds);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFindVariable --
 *
 *      Locate the entry in environ for a given name.  On Unix this
 *      routine is case sensetive, on Windows / OS/2 this matches mixed case.
 *
 * Results:
 *      The return value is the index in environ of an entry with the
 *      name "name", or -1 if there is no such entry.   The integer at
 *      *lengthPtr is filled in with the length of name (if a matching
 *      entry is found) or the length of the environ array (if no matching
 *      entry is found).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TclpFindVariable(name, lengthPtr)
    CONST char *name;           /* Name of desired environment variable
                                 * (UTF-8). */
    int *lengthPtr;             /* Used to return length of name (for
                                 * successful searches) or number of non-NULL
                                 * entries in environ (for unsuccessful
                                 * searches). */
{
    int i, length, result = -1;
    register CONST char *env, *p1, *p2;
    char *envUpper, *nameUpper;
    Tcl_DString envString;

    /*
     * Convert the name to all upper case for the case insensitive
     * comparison.
     */

    length = strlen(name);
    nameUpper = (char *) ckalloc((unsigned) length+1);
    memcpy((VOID *) nameUpper, (VOID *) name, (size_t) length+1);
    Tcl_UtfToUpper(nameUpper);

    Tcl_DStringInit(&envString);
    for (i = 0, env = environ[i]; env != NULL; i++, env = environ[i]) {
        /*
         * Chop the env string off after the equal sign, then Convert
         * the name to all upper case, so we do not have to convert
         * all the characters after the equal sign.
         */

        envUpper = Tcl_ExternalToUtfDString(NULL, env, -1, &envString);
        p1 = strchr(envUpper, '=');
        if (p1 == NULL) {
            continue;
        }
        length = (int) (p1 - envUpper);
        Tcl_DStringSetLength(&envString, length+1);
        Tcl_UtfToUpper(envUpper);

        p1 = envUpper;
        p2 = nameUpper;
        for (; *p2 == *p1; p1++, p2++) {
            /* NULL loop body. */
        }
        if ((*p1 == '=') && (*p2 == '\0')) {
            *lengthPtr = length;
            result = i;
            goto done;
        }

        Tcl_DStringFree(&envString);
    }

    *lengthPtr = i;

    done:
    Tcl_DStringFree(&envString);
    ckfree(nameUpper);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_Init --
 *
 *	This procedure is typically invoked by Tcl_AppInit procedures
 *	to perform additional initialization for a Tcl interpreter,
 *	such as sourcing the "init.tcl" script.
 *
 * Results:
 *	Returns a standard Tcl completion code and sets interp->result
 *	if there is an error.
 *
 * Side effects:
 *	Depends on what's in the init.tcl script.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_Init(interp)
    Tcl_Interp *interp;		/* Interpreter to initialize. */
{
    Tcl_Obj *pathPtr;

    if (tclPreInitScript != NULL) {
        if (Tcl_Eval(interp, tclPreInitScript) == TCL_ERROR) {
            return (TCL_ERROR);
        };
    }

    pathPtr = TclGetLibraryPath();
    if (pathPtr == NULL) {
        pathPtr = Tcl_NewObj();
    }
    Tcl_SetVar2Ex(interp, "tcl_libPath", NULL, pathPtr, TCL_GLOBAL_ONLY);
    return(Tcl_Eval(interp, initScript));
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_SourceRCFile --
 *
 *      This procedure is typically invoked by Tcl_Main of Tk_Main
 *      procedure to source an application specific rc file into the
 *      interpreter at startup time.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on what's in the rc script.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_SourceRCFile(interp)
    Tcl_Interp *interp;         /* Interpreter to source rc file into. */
{
    Tcl_DString temp;
    char *fileName;
#ifdef CLI_VERSION
    Tcl_Channel errChannel;
#endif

    fileName = Tcl_GetVar(interp, "tcl_rcFileName", TCL_GLOBAL_ONLY);

    if (fileName != NULL) {
        Tcl_Channel c;
        char *fullName;

        Tcl_DStringInit(&temp);
        fullName = Tcl_TranslateFileName(interp, fileName, &temp);
        if (fullName == NULL) {
            /*
             * Couldn't translate the file name (e.g. it referred to a
             * bogus user or there was no HOME environment variable).
             * Just do nothing.
             */
        } else {

            /*
             * Test for the existence of the rc file before trying to read it.
             */
            c = Tcl_OpenFileChannel(NULL, fullName, "r", 0);
            if (c != (Tcl_Channel) NULL) {
                Tcl_Close(NULL, c);
                if (Tcl_EvalFile(interp, fullName) != TCL_OK) {
#ifndef CLI_VERSION
                    char cbuf[1000];
                    sprintf(cbuf, "%s\n", Tcl_GetStringResult(interp));
                    WinMessageBox(HWND_DESKTOP, NULLHANDLE, cbuf, "Tclsh", 0,
                                  MB_OK | MB_ICONEXCLAMATION | MB_APPLMODAL);
#else
                    errChannel = Tcl_GetStdChannel(TCL_STDERR);
                    if (errChannel) {
                        Tcl_WriteObj(errChannel, Tcl_GetObjResult(interp));
                        Tcl_WriteChars(errChannel, "\n", 1);
                    }
#endif
                }
            }
        }
        Tcl_DStringFree(&temp);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2PMInitialize --
 *
 *	Performs OS/2-specific initialization. When we are not going to
 *	use PM perse (command line version), we only determine the anchor
 *	block handle, which is necessary if/when the registry package is
 *	loaded.
 *
 * Results:
 *	True or false depending on intialization.
 *
 * Side effects:
 *	Opens the "PM connection"
 *
 *----------------------------------------------------------------------
 */

BOOL
TclOS2PMInitialize(void)
{
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (tsdPtr->PmInitialized) return TRUE;

    tsdPtr->PmInitialized = TRUE;

    if (TclOS2GetUsePm()) {
        /* Initialize PM */
        tsdPtr->tclHab = WinInitialize (0);
#ifdef VERBOSE
        printf("HAB: %x\n", tsdPtr->tclHab);
#endif
        if (tsdPtr->tclHab == NULLHANDLE) return FALSE;
        /* Create message queue, increased size from 10 */
        tsdPtr->tclHmq= WinCreateMsgQueue (tclHab, 64);
#ifdef VERBOSE
        printf("HMQ: %x\n", tsdPtr->tclHmq);
#endif
        if (tsdPtr->tclHmq == NULLHANDLE) {
#ifdef VERBOSE
            printf("Last error %x\n", WinGetLastError(tsdPtr->tclHab));
#endif
            WinTerminate(tsdPtr->tclHab);
            tsdPtr->tclHab= (HAB)0;
            return FALSE;
        }
    }
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2PMShutdown --
 *
 *	Performs OS/2-specific cleanup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Closes the "PM connection"
 *
 *----------------------------------------------------------------------
 */

void
TclOS2PMShutdown(void)
{
    BOOL rc;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    if (TclOS2GetUsePm()) {
        /* Reset pointer to arrow */
        rc = WinSetPointer(HWND_DESKTOP,
                           WinQuerySysPointer(HWND_DESKTOP, SPTR_ARROW, FALSE));
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("WinSetPointer TclOS2PMShutdown ERROR: %x\n",
                   WinGetLastError(tclHab));
        } else {
            printf("WinSetPointer TclOS2PMShutdown OK\n");
        }
#endif
        if (tsdPtr->tclHmq != NULLHANDLE) {
            WinDestroyMsgQueue(tsdPtr->tclHmq);
        }
        tsdPtr->tclHmq= (HMQ)0;
        if (tsdPtr->tclHab != NULLHANDLE) {
            WinTerminate(tsdPtr->tclHab);
        }
        tsdPtr->tclHab= (HAB)0;
    }
    tsdPtr->PmInitialized = FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetHAB --
 *
 *	Get the handle to the anchor block.
 *
 * Results:
 *	HAB or NULLHANDLE.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

HAB
TclOS2GetHAB(void)
{
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
#ifdef VERBOSE
    printf("TclOS2GetHAB returning %x\n", tsdPtr->tclHab);
#endif
    return tsdPtr->tclHab;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetHMQ --
 *
 *	Get the handle to the message queue.
 *
 * Results:
 *	HMQ or NULLHANDLE.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

HMQ
TclOS2GetHMQ(HAB hab)
{
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
#ifdef VERBOSE
    printf("TclOS2GetHMQ returning %x\n", tsdPtr->tclHmq);
#endif
    return tsdPtr->tclHmq;
}

/*
 *----------------------------------------------------------------------
 *
 * TclPlatformExit --
 *
 *	Cleanup and exit on OS/2.
 *
 * Results:
 *	None. This procedure never returns (it exits the process when
 *	it's done).
 *
 * Side effects:
 *	This procedure terminates all relations with PM.
 *
 *----------------------------------------------------------------------
 */

void
TclPlatformExit(status)
    int status;				/* Status to exit with */
{
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
#ifdef VERBOSE
    printf("opened files not closed yet: %d\n", openedFiles);
#endif
    if (TclOS2GetUsePm()) {
        /*
         * The MLE of the Terminal edit window doesn't restore the pointer
         * when the 'exit' command is typed. Force it to be shown.
         */
#ifdef VERBOSE
        printf("Showing pointer...\n");
#endif
        WinShowPointer(HWND_DESKTOP, TRUE);
        GLOBALS_LOCK;
        if (tclHmq != NULLHANDLE) {
            WinDestroyMsgQueue(tclHmq);
        }
        tclHmq= (HMQ)0;
        if (tclHab != NULLHANDLE) {
            WinTerminate(tclHab);
        }
        tclHab= (HAB)0;
        GLOBALS_UNLOCK;
        TclOS2SetUsePm(0);
    }
    DosCloseMutexSem(globalsLock);
    exit(status);
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetUsePm --
 *
 *	Get the value of the DLL's usePm value
 *
 * Results:
 *	Value of usePm (Bool).
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

BOOL
TclOS2GetUsePm(void)
{
    BOOL value;

    GLOBALS_LOCK;
    value = usePm;
    GLOBALS_UNLOCK;

#ifdef VERBOSE
    printf("TclOS2GetUsePm: %d\n", value);
#endif
    return value;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2SetUsePm --
 *
 *	Set the value of the DLL's usePm value
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the DLL's usePm variable.
 *
 *----------------------------------------------------------------------
 */

void
TclOS2SetUsePm(value)
    BOOL value;
{
#ifdef VERBOSE
    BOOL oldValue, newValue;
#endif
    GLOBALS_LOCK;
#ifdef VERBOSE
    oldValue = usePm;
#endif
    usePm = value;
#ifdef VERBOSE
    newValue = usePm;
#endif
    GLOBALS_UNLOCK;
#ifdef VERBOSE
    printf("TclOS2SetUsePm: %d and %d => %d\n", oldValue, value, newValue);
#endif
    return;
}

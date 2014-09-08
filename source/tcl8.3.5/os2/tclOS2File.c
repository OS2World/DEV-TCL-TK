/* 
 * tclOS2File.c --
 *
 *      This file contains temporary wrappers around UNIX file handling
 *      functions. These wrappers map the UNIX functions to OS/2 HFILE-style
 *      files, which can be manipulated through the OS/2 console redirection
 *      interfaces.
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tclOS2Int.h"

/*
 * Mapping of drive numbers to drive letters
 */
static char drives[] = {'0', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
                        'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U',
                        'V', 'W', 'X', 'Y', 'Z'};


/*
 *----------------------------------------------------------------------
 *
 * TclpFindExecutable --
 *
 *	This procedure computes the absolute path name of the current
 *	application, given its argv[0] value.
 *
 * Results:
 *      A dirty UTF string that is the path to the executable.  At this
 *      point we may not know the system encoding.  Convert the native
 *      string value to UTF using the default encoding.  The assumption
 *      is that we will still be able to parse the path given the path
 *      name contains ASCII string and '/' chars do not conflict with
 *      other UTF chars.
 *
 * Side effects:
 *	The variable tclNativeExecutableName gets filled in with the file
 *	name for the application, if we figured it out.  If we couldn't
 *	figure it out, Tcl_FindExecutable is set to NULL.
 *
 *----------------------------------------------------------------------
 */

char *
TclpFindExecutable(argv0)
    CONST char *argv0;	/* The value of the application's argv[0] (native). */
{
    PPIB pibPtr;
    PTIB tibPtr;
    char moduleName[MAX_PATH+1];
    Tcl_DString nameString;
#ifdef VERBOSE
    printf("TclpFindExecutable [%s], tclNativeExecutableName [%s]\n", argv0,
            tclNativeExecutableName);
    fflush(stdout);
#endif

    if (tclNativeExecutableName != NULL) {
        return tclNativeExecutableName;
    }

    /*
     * Under OS/2 we ignore argv0, and return the Module Name.
     */

    rc = DosGetInfoBlocks(&tibPtr, &pibPtr);
#ifdef VERBOSE
    printf("pibPtr->pib_pchcmd [%s]\n", pibPtr->pib_pchcmd);
    fflush(stdout);
#endif

    rc = DosQueryModuleName((HMODULE)pibPtr->pib_hmte, 256, moduleName);
#ifdef VERBOSE
    printf("module name [%s]\n", moduleName);
    fflush(stdout);
#endif

    Tcl_ExternalToUtfDString(NULL, moduleName, -1, &nameString);
    tclNativeExecutableName = (char *)
            ckalloc((unsigned) (Tcl_DStringLength(&nameString) + 1));
    strcpy(tclNativeExecutableName, Tcl_DStringValue(&nameString));
    Tcl_DStringFree(&nameString);

    TclOS2NoBackslash(tclNativeExecutableName);
    return tclNativeExecutableName;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpMatchFileTypes --
 *
 *      This routine is used by the globbing code to search a
 *      directory for all files which match a given pattern.
 *
 * Results:
 *      If the tail argument is NULL, then the matching files are
 *      added to the interp->result.  Otherwise, TclDoGlob is called
 *      recursively for each matching subdirectory.  The return value
 *      is a standard Tcl result indicating whether an error occurred
 *      in globbing.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------- */

int
TclpMatchFilesTypes(
    Tcl_Interp *interp,         /* Interpreter to receive results. */
    char *separators,           /* Directory separators to pass to TclDoGlob. */
    Tcl_DString *dirPtr,        /* Contains path to directory to search. */
    char *pattern,              /* Pattern to match against. */
    char *tail,                 /* Pointer to end of pattern.  Tail must
                                 * point to a location in pattern and must
                                 * not be static. */
    GlobTypeData *types)        /* Object containing list of acceptable types.
                                 * May be NULL. */
{
    APIRET rc;
    char drivePat[] = "?:\\";
    const char *message;
    char *dir, *newPattern, *root;
    int matchDotFiles;
    int dirLength, result = TCL_OK;
    Tcl_DString dirString, patternString;
    HDIR handle;
    FILESTATUS3 infoBuf;
    FILEFINDBUF3 data;
    Tcl_DString ds;
    char *p = NULL, c;
    char *nativeName;
    Tcl_Obj *resultPtr;
    ULONG filesAtATime = 1;
    ULONG diskNum = 3;		/* Assume C: for errors */
#ifdef CASE_SENSITIVE_GLOBBING
    ULONG volFlags;
    BYTE fsBuf[1024];		/* Info about file system */
    ULONG bufSize;
#endif

#ifdef VERBOSE
    printf("TclpMatchFileTypes path [%s], pat [%s]\n", Tcl_DStringValue(dirPtr),
           pattern);
#endif

    /*
     * Convert the path to normalized form since some interfaces only
     * accept backslashes. This goes for Windows, probably not for OS/2.
     */

    dirLength = Tcl_DStringLength(dirPtr);
    Tcl_DStringInit(&dirString);
    if (dirLength == 0) {
        Tcl_DStringAppend(&dirString, ".", 1);
        p = Tcl_DStringValue(&dirString);
    } else {

        Tcl_DStringAppend(&dirString, Tcl_DStringValue(dirPtr),
                Tcl_DStringLength(dirPtr));
        for (p = Tcl_DStringValue(&dirString); *p != '\0'; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
        p--;
/*
        if ((*p != '\\') && (*p != ':')) {
            Tcl_DStringAppend(&dirString, "\\", 1);
        }
*/

        /*
         * DosQueryPathInfo can only handle a trailing (back)slash for the root
         * of a drive, so cut it off in other case.
         */
        if ((*p == '\\') && (*(p-1) != ':') && (*p != '.')) {
            Tcl_DStringSetLength(&dirString, Tcl_DStringLength(&dirString)-1);
            p--;
        }

        /*
         * In cases of eg. "c:filespec", we need to put the current dir for that
         * disk after the drive specification.
         */
        if (*p == ':') {
            char wd[256];
            ULONG len = 256;
            ULONG drive;

            if (*(p-1) > 'Z') drive = *(p-1) - 'a' + 1;
            else drive = *(p-1) - 'A' + 1;
            rc = DosQueryCurrentDir(drive, (PBYTE)wd, &len);
#ifdef VERBOSE
            printf("DosQueryCurrentDir drive %c (%d) returns %d [%s] len %d\n",
                   *(p-1), drive, rc, wd, len);
#endif
            if (rc == NO_ERROR) {
                Tcl_DStringAppend(&dirString, "\\", 1);
                len = strlen(wd);
                Tcl_DStringAppend(&dirString, wd, len);
                p += len+1;
            }
#ifdef VERBOSE
            printf("    *p now %c\n", *p);
#endif
        }
    }

    dir = Tcl_DStringValue(&dirString);

    /*
     * First verify that the specified path is actually a directory.
     */

    nativeName = Tcl_UtfToExternalDString(NULL, dir,
                                          Tcl_DStringLength(&dirString), &ds);
    rc = DosQueryPathInfo(nativeName, FIL_STANDARD, &infoBuf, sizeof(infoBuf));
#ifdef VERBOSE
    printf("DosQueryPathInfo [%s] returned [%d]\n", nativeName, rc);
    fflush(stdout);
#endif
    Tcl_DStringFree(&ds);
    if ( (rc != NO_ERROR) || ((infoBuf.attrFile & FILE_DIRECTORY) == 0)) {
        Tcl_DStringFree(&dirString);
        return TCL_OK;
    }

    if (*p != '\\') {
        Tcl_DStringAppend(&dirString, "\\", 1);
    }
    dir = Tcl_DStringValue(&dirString);

    /*
     * Next check the volume information for the directory to see whether
     * comparisons should be case sensitive or not.  If the root is null, then
     * we use the root of the current directory.  If the root is just a drive
     * specifier, we use the root directory of the given drive.
     * There's no API for determining case sensitivity and preservation (that
     * I've found) perse. We can determine the File System Driver though, and
     * assume correct values for some file systems we know, eg. FAT, HPFS,
     * NTFS, ext2fs.
     */

    switch (Tcl_GetPathType(dir)) {
        case TCL_PATH_RELATIVE: {
            ULONG logical;
            /* Determine current drive */
            rc = DosQueryCurrentDisk(&diskNum, &logical);
            if (rc != NO_ERROR) {
                message = "couldn't read volume information for \"";
                goto error;
            }
#ifdef VERBOSE
            printf("TCL_PATH_RELATIVE, disk %d\n", diskNum);
#endif

            break;
        }
        case TCL_PATH_VOLUME_RELATIVE: {
            ULONG logical;
            /* Determine current drive */
            rc = DosQueryCurrentDisk(&diskNum, &logical);
            if (rc != NO_ERROR) {
                message = "couldn't read volume information for \"";
                goto error;
            }
#ifdef VERBOSE
            printf("TCL_PATH_VOLUME_RELATIVE, disk %d\n", diskNum);
#endif

            if (*dir == '\\') {
                root = NULL;
            } else {
                root = drivePat;
                *root = dir[0];
            }
            break;
        }
        case TCL_PATH_ABSOLUTE:
            /* Use given drive */
            diskNum = (ULONG) dir[0] - 'A' + 1;
            if (dir[0] >= 'a') {
                diskNum -= ('a' - 'A');
            }
#ifdef VERBOSE
            printf("TCL_PATH_ABSOLUTE, disk %d\n", diskNum);
#endif

            if (dir[1] == ':') {
                root = drivePat;
                *root = dir[0];
            } else if (dir[1] == '\\') {
                p = strchr(dir+2, '\\');
                p = strchr(p+1, '\\');
                p++;
                c = *p;
                *p = 0;
                *p = c;
            }
            break;
    }

    /*
     * In OS/2, although some volumes may support case sensitivity, according
     * to the Control Program Guide and Reference file searching is case
     * insensitive.  So in globbing we need to ignore the case of file names.
     * Just in case, make switchable via compile-time option.
     */

#undef CASE_SENSITIVE_GLOBBING
#ifndef CASE_SENSITIVE_GLOBBING
    Tcl_DStringInit(&patternString);
    newPattern = Tcl_DStringAppend(&patternString, pattern, tail - pattern);
    Tcl_UtfToLower(newPattern);

#else /* CASE_SENSITIVE_GLOBBING */
    /* Now determine file system driver name and hack the case stuff */
    bufSize = sizeof(fsBuf);
    rc = DosQueryFSAttach(NULL, diskNum, FSAIL_DRVNUMBER, ((PFSQBUFFER2)fsBuf),
                          &bufSize);
    if (rc != NO_ERROR) {
        /* Error, assume FAT */
#ifdef VERBOSE
        printf("DosQueryFSAttach %d ERROR %d (bufsize %d)\n", diskNum, rc,
               bufSize);
#endif
        volFlags = 0;
    } else {
        USHORT cbName = ((PFSQBUFFER2) fsBuf)->cbName;
#ifdef VERBOSE
        printf("DosQueryFSAttach %d OK, szN [%s], szFSDN [%s] (bufsize %d)\n",
               diskNum, ((PFSQBUFFER2)fsBuf)->szName,
               ((PFSQBUFFER2)(fsBuf+cbName))->szFSDName, bufSize);
#endif
        if (strcmp(((PFSQBUFFER2)(fsBuf+cbName))->szFSDName, "FAT") == 0) {
            volFlags = 0;
        } else
        if (strcmp(((PFSQBUFFER2)(fsBuf+cbName))->szFSDName, "HPFS") == 0) {
            volFlags = FS_CASE_IS_PRESERVED;
        } else
        if (strcmp(((PFSQBUFFER2)(fsBuf+cbName))->szFSDName, "NFS") == 0) {
            volFlags = FS_CASE_SENSITIVE | FS_CASE_IS_PRESERVED;
        } else
        if (strcmp(((PFSQBUFFER2)(fsBuf+cbName))->szFSDName, "EXT2FS") == 0) {
            volFlags = FS_CASE_SENSITIVE | FS_CASE_IS_PRESERVED;
        } else
        if (strcmp(((PFSQBUFFER2)(fsBuf+cbName))->szFSDName, "VINES") == 0) {
            volFlags = 0;
        } else
        if (strcmp(((PFSQBUFFER2)(fsBuf+cbName))->szFSDName, "NTFS") == 0) {
            volFlags = FS_CASE_IS_PRESERVED;
        } else {
            volFlags = 0;
        }
    }

    /*
     * If the volume is not case sensitive, then we need to convert the pattern
     * to lower case.
     */

    Tcl_DStringInit(&patternString);
    newPattern = Tcl_DStringAppend(&patternString, pattern, tail - pattern);
    if (!(volFlags & FS_CASE_SENSITIVE)) {
        Tcl_UtfToLower(newPattern);
    }
#endif /* CASE_SENSITIVE_GLOBBING */

    /*
     * We need to check all files in the directory, so append a *
     * to the path. Not "*.*".
     */


    dir = Tcl_DStringAppend(&dirString, "*", 3);
    nativeName = Tcl_UtfToExternalDString(NULL, dir, -1, &ds);

    /*
     * Now open the directory for reading and iterate over the contents.
     */

    handle = HDIR_SYSTEM;
    rc = DosFindFirst(nativeName, &handle, FILE_NORMAL | FILE_DIRECTORY, &data,
                      sizeof(data), &filesAtATime, FIL_STANDARD);
#ifdef VERBOSE
    printf("DosFindFirst %s returns %x (%s)\n", nativeName, rc, data.achName);
#endif
    Tcl_DStringFree(&dirString);

    if (rc != NO_ERROR) {
        message = "couldn't read directory \"";
        goto error;
    }

    /*
     * Clean up the tail pointer.  Leave the tail pointing to the
     * first character after the path separator or NULL.
     */

    if (*tail == '\\') {
        tail++;
    }
    if (*tail == '\0') {
        tail = NULL;
    } else {
        tail++;
    }

    /*
     * Check to see if the pattern needs to compare with dot files.
     */

    if ((newPattern[0] == '.')
            || ((pattern[0] == '\\') && (pattern[1] == '.'))) {
        matchDotFiles = 1;
    } else {
        matchDotFiles = 0;
    }

    /*
     * Now iterate over all of the files in the directory.
     */

    resultPtr = Tcl_GetObjResult(interp);
#ifdef VERBOSE
    for ( rc = NO_ERROR;
          rc == NO_ERROR;
          printf("DosFindNext returns %x (%s)\n",
                 rc = DosFindNext(handle, &data, sizeof(data), &filesAtATime),
                 data.achName)) {
#else
    for (   rc = NO_ERROR;
            rc == NO_ERROR;
            rc = DosFindNext(handle, &data, sizeof(data), &filesAtATime)) {
#endif
        char *nativeMatchResult;
        char *name, *fname;

        nativeName = data.achName;
        name = Tcl_ExternalToUtfDString(NULL, nativeName, -1, &ds);

        /*
         * Check to see if the file matches the pattern.  We need to convert
         * the file name to lower case for comparison purposes.  Note that we
         * are ignoring the case sensitivity flag because Windows doesn't honor
         * case even if the volume is case sensitive.  If the volume also
         * doesn't preserve case, then we previously returned the lower case
         * form of the name.  This didn't seem quite right since there are
         * non-case-preserving volumes that actually return mixed case.  So now
         * we are returning exactly what we get from the system.
         */

        Tcl_UtfToLower(name);
        nativeMatchResult = NULL;

        if ((matchDotFiles == 0) && (name[0] == '.')) {
            /*
             * Ignore hidden files.
             */
        } else if (Tcl_StringMatch(name, newPattern) != 0) {
            nativeMatchResult = nativeName;
        }
        Tcl_DStringFree(&ds);

        if (nativeMatchResult == NULL) {
            continue;
        }

        /*
         * If the file matches, then we need to process the remainder of the
         * path.  If there are more characters to process, then ensure matching
         * files are directories and call TclDoGlob. Otherwise, just add the
         * file to the result.
         */

        name = Tcl_ExternalToUtfDString(NULL, nativeMatchResult, -1, &ds);
        Tcl_DStringAppend(dirPtr, name, -1);
        Tcl_DStringFree(&ds);

        fname = Tcl_DStringValue(dirPtr);
        nativeName = Tcl_ExternalToUtfDString(NULL, fname,
                                              Tcl_DStringLength(dirPtr), &ds);

        /*
         * We only retrieve the attributes of the file if it is
         * absolutely necessary.
         */

        if (tail == NULL) {
            int typeOk = 1;
            if (types != NULL) {
                if (types->perm != 0) {
                    if ((DosQueryPathInfo(dirPtr->string, FIL_STANDARD,
                                          &infoBuf, sizeof(infoBuf))
                         == NO_ERROR) &&
                        (((types->perm & TCL_GLOB_PERM_RONLY) &&
                                !(infoBuf.attrFile & FILE_READONLY)) ||
                         ((types->perm & TCL_GLOB_PERM_HIDDEN) &&
                                !(infoBuf.attrFile & FILE_HIDDEN)) ||
                         ((types->perm & TCL_GLOB_PERM_R) &&
                                (TclpAccess(fname, R_OK) != 0)) ||
                         ((types->perm & TCL_GLOB_PERM_W) &&
                                (TclpAccess(fname, W_OK) != 0)) ||
                         ((types->perm & TCL_GLOB_PERM_X) &&
                                (TclpAccess(fname, X_OK) != 0)))
                       ) {
                        typeOk = 0;
                    }
                }
                if (typeOk && types->type != 0) {
                    struct stat buf;
                    /*
                     * We must match at least one flag to be listed
                     */
                    typeOk = 0;
                    if (TclpLstat(fname, &buf) >= 0) {
                        /*
                         * In order bcdpfls as in 'find -t'
                         */
                        if (
                            ((types->type & TCL_GLOB_TYPE_BLOCK) &&
                                    S_ISBLK(buf.st_mode)) ||
                            ((types->type & TCL_GLOB_TYPE_CHAR) &&
                                    S_ISCHR(buf.st_mode)) ||
                            ((types->type & TCL_GLOB_TYPE_DIR) &&
                                    S_ISDIR(buf.st_mode)) ||
                            ((types->type & TCL_GLOB_TYPE_PIPE) &&
                                    S_ISFIFO(buf.st_mode)) ||
                            ((types->type & TCL_GLOB_TYPE_FILE) &&
                                    S_ISREG(buf.st_mode))
#ifdef S_ISLNK
                            || ((types->type & TCL_GLOB_TYPE_LINK) &&
                                    S_ISLNK(buf.st_mode))
#endif
#ifdef S_ISSOCK
                            || ((types->type & TCL_GLOB_TYPE_SOCK) &&
                                    S_ISSOCK(buf.st_mode))
#endif
                            ) {
                            typeOk = 1;
                        }
                    } else {
                        /* Posix error occurred */
                    }
                }
            }
            if (typeOk) {
                Tcl_ListObjAppendElement(interp, resultPtr,
                        Tcl_NewStringObj(fname, Tcl_DStringLength(dirPtr)));
            }
        } else {
            if ( (DosQueryPathInfo(dirPtr->string, FIL_STANDARD, &infoBuf,
                                  sizeof(infoBuf)) == NO_ERROR) &&
                 (infoBuf.attrFile & FILE_DIRECTORY)) {
                Tcl_DStringAppend(dirPtr, "/", 1);
                result = TclDoGlob(interp, separators, dirPtr, tail, types);
                if (result != TCL_OK) {
                    break;
                }
            }
        }
        /*
         * Free ds here to ensure that nativeName is valid above.
         */

        Tcl_DStringFree(&ds);

        Tcl_DStringSetLength(dirPtr, dirLength);
    }

    DosFindClose(handle);
    Tcl_DStringFree(&dirString);
    Tcl_DStringFree(&patternString);

    return result;

    error:
    Tcl_DStringFree(&dirString);
    TclOS2ConvertError(rc);
    Tcl_ResetResult(interp);
    Tcl_AppendResult(interp, message, Tcl_DStringValue(dirPtr), "\": ",
            Tcl_PosixError(interp), (char *) NULL);
    return TCL_ERROR;
}

/*
 * TclpMatchFiles --
 *
 * This function is now obsolete.  Call the above function
 * 'TclpMatchFilesTypes' instead.
 */
int
TclpMatchFiles(
    Tcl_Interp *interp,         /* Interpreter to receive results. */
    char *separators,           /* Directory separators to pass to TclDoGlob. */
    Tcl_DString *dirPtr,        /* Contains path to directory to search. */
    char *pattern,              /* Pattern to match against. */
    char *tail)                 /* Pointer to end of pattern.  Tail must
                                 * point to a location in pattern and must
                                 * not be static.*/
{
    return TclpMatchFilesTypes(interp,separators,dirPtr,pattern,tail,NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpGetUserHome --
 *
 *      This function takes the passed in user name and finds the
 *      corresponding home directory specified in the password file.
 *
 * Results:
 *      The result is a pointer to a string specifying the user's home
 *      directory, or NULL if the user's home directory could not be
 *      determined.  Storage for the result string is allocated in
 *      bufferPtr; the caller must call Tcl_DStringFree() when the result
 *      is no longer needed.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
TclpGetUserHome(name, bufferPtr)
    CONST char *name;           /* User name for desired home directory. */
    Tcl_DString *bufferPtr;     /* Uninitialized or free DString filled
                                 * with name of user's home directory. */
{
    /* Future implementation with SES and/or LANServer etc.? */
    /* Then also remove #define from tclOS2Int.h */
    return NULL;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpAccess --
 *
 *      This function replaces the library version of access(), fixing the
 *      following bugs:
 *
 *      1. access() returns that all files have execute permission.
 *
 * Results:
 *      See access documentation.
 *
 * Side effects:
 *      See access documentation.
 *
 *---------------------------------------------------------------------------
 */

int
TclpAccess(
    CONST char *path,           /* Path of file to access (UTF-8). */
    int mode)                   /* Permission setting. */
{
    Tcl_DString ds;
    FILESTATUS3 infoBuf;
    char *nativePath;

    nativePath = Tcl_UtfToExternalDString(NULL, path, -1, &ds);
    rc = DosQueryPathInfo(nativePath, FIL_STANDARD, &infoBuf, sizeof(infoBuf));
    Tcl_DStringFree(&ds);

    if (rc != NO_ERROR) {
        TclOS2ConvertError(rc);
        return -1;
    }

    if ((mode & W_OK) && (infoBuf.attrFile & FILE_READONLY)) {
        /*
         * File is not writable.
         */

        Tcl_SetErrno(EACCES);
        return -1;
    }

    if (mode & X_OK) {
        CONST char *p;

        if (infoBuf.attrFile & FILE_DIRECTORY) {
            /*
             * Directories are always executable.
             */

            return 0;
        }
        p = strrchr(path, '.');
        if (p != NULL) {
            p++;
            if ((stricmp(p, "exe") == 0)
                    || (stricmp(p, "com") == 0)
                    || (stricmp(p, "cmd") == 0)
                    || (stricmp(p, "bat") == 0)) {
                /*
                 * File that ends with .exe, .com, or .bat is executable.
                 */

                return 0;
            }
        }
        Tcl_SetErrno(EACCES);
        return -1;
    }

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpChdir --
 *
 *      This function replaces the library version of chdir().
 *
 * Results:
 *      See chdir() documentation.
 *
 * Side effects:
 *      See chdir() documentation.
 *
 *----------------------------------------------------------------------
 */

int
TclpChdir(path)
    CONST char *path;       /* Path to new working directory (UTF-8). */
{
    Tcl_DString ds;
    char *nativePath;

    nativePath = Tcl_UtfToExternalDString(NULL, path, -1, &ds);
#ifdef VERBOSE
    printf("TclChDir %s\n", nativePath);
#endif
    /* Set drive, if present */
    if (nativePath[1] == ':') {
        ULONG ulDriveNum;

        /* Determine disk number */
        for (ulDriveNum=1;
             ulDriveNum<27 && strnicmp(&drives[ulDriveNum], nativePath, 1) != 0;
             ulDriveNum++)
            /* do nothing */;
        if (ulDriveNum == 27) {
            TclOS2ConvertError(ERROR_INVALID_DRIVE);
            Tcl_DStringFree(&ds);
            return -1;
        }
        rc = DosSetDefaultDisk(ulDriveNum);
#ifdef VERBOSE
        printf("DosSetDefaultDisk %c (%d) returned [%d]\n", nativePath[0],
               ulDriveNum, rc);
#endif
        nativePath += 2;
    }
    /* Set directory if specified (not just a drive spec) */
    if (strcmp(nativePath, "") != 0) {
        rc = DosSetCurrentDir(nativePath);
#ifdef VERBOSE
        printf("DosSetCurrentDir [%s] returned [%d]\n", nativePath, rc);
#endif
        if (rc != NO_ERROR) {
            TclOS2ConvertError(rc);
            Tcl_DStringFree(&ds);
            return -1;
        }
    }
    Tcl_DStringFree(&ds);
    return 0;
}

#ifdef __CYGWIN__
/*
 *---------------------------------------------------------------------------
 *
 * TclpReadlink --
 *
 *     This function replaces the library version of readlink().
 *
 * Results:
 *     The result is a pointer to a string specifying the contents
 *     of the symbolic link given by 'path', or NULL if the symbolic
 *     link could not be read.  Storage for the result string is
 *     allocated in bufferPtr; the caller must call Tcl_DStringFree()
 *     when the result is no longer needed.
 *
 * Side effects:
 *     See readlink() documentation.
 *
 *---------------------------------------------------------------------------
 */

char *
TclpReadlink(path, linkPtr)
    CONST char *path;          /* Path of file to readlink (UTF-8). */
    Tcl_DString *linkPtr;      /* Uninitialized or free DString filled
                                * with contents of link (UTF-8). */
{
    char link[MAXPATHLEN];
    int length;
    char *native;
    Tcl_DString ds;

    native = Tcl_UtfToExternalDString(NULL, path, -1, &ds);
    length = readlink(native, link, sizeof(link));     /* INTL: Native. */
    Tcl_DStringFree(&ds);

    if (length < 0) {
        return NULL;
    }

    Tcl_ExternalToUtfDString(NULL, link, length, linkPtr);
    return Tcl_DStringValue(linkPtr);
}
#endif /* __CYGWIN__ */

/*
 *----------------------------------------------------------------------
 *
 * TclpGetCwd --
 *
 *      This function replaces the library version of getcwd().
 *
 * Results:
 *      The result is a pointer to a string specifying the current
 *      directory, or NULL if the current directory could not be
 *      determined.  If NULL is returned, an error message is left in the
 *      interp's result.  Storage for the result string is allocated in
 *      bufferPtr; the caller must call Tcl_DStringFree() when the result
 *      is no longer needed.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
TclpGetCwd(interp, bufferPtr)
    Tcl_Interp *interp;         /* If non NULL, used for error reporting. */
    Tcl_DString *bufferPtr;     /* Uninitialized or free DString filled
                                 * with name of current directory. */
{
#define DRIVEPART	3	/* Drive letter, ':' and '/' */
    static char buffer[MAXPATHLEN+1+DRIVEPART];
    char *p;
    ULONG length = MAXPATHLEN+1;
    ULONG ulDriveNum = 0;	/* A=1, B=2, ... */
    ULONG ulDriveMap = 0;	/* Bitmap of valid drives */

#ifdef VERBOSE
    printf("TclGetCwd\n");
#endif
    rc = DosQueryCurrentDisk(&ulDriveNum, &ulDriveMap);
#ifdef VERBOSE
    printf("DosQueryCurrentDisk returned [%d], drive %d (%c)\n", rc,
           ulDriveNum, drives[ulDriveNum]);
#endif
    if (rc != NO_ERROR) {
        TclOS2ConvertError(rc);
        if (interp != NULL) {
            Tcl_AppendResult(interp, "error getting default drive: ",
                             Tcl_PosixError(interp), (char *) NULL);
        }
        return NULL;
    }

    /* OS/2 returns pwd *without* leading slash!, so add it */
    buffer[0] = drives[ulDriveNum];
    buffer[1] = ':';
    buffer[2] = '/';
    rc = DosQueryCurrentDir(0, buffer+3, &length);
#ifdef VERBOSE
    printf("DosQueryCurrentDir returned [%d], dir %s\n", rc, buffer);
#endif
    if (rc != NO_ERROR) {
        TclOS2ConvertError(rc);
        if (interp != NULL) {
            Tcl_AppendResult(interp, "error getting working directory name: ",
                             Tcl_PosixError(interp), (char *) NULL);
        }
        return NULL;
    }
    Tcl_ExternalToUtfDString(NULL, buffer, -1, bufferPtr);

    /*
     * Convert to forward slashes for easier use in scripts.
     */

    for (p =  Tcl_DStringValue(bufferPtr); *p != '\0'; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return Tcl_DStringValue(bufferPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpStat, TclpLstat --
 *
 *      This function replaces the library version of stat().
 *
 * Results:
 *      See stat documentation.
 *
 * Side effects:
 *      See stat documentation.
 *
 *----------------------------------------------------------------------
 */

int
TclpStat(path, statPtr)
    CONST char *path;           /* Path of file to stat (UTF-8). */
    struct stat *statPtr;       /* Filled with results of stat call. */
{
    Tcl_DString ds;
    char *nativePath, *p;
    int result;

    /*
     * Ensure correct file sizes by forcing the OS to write any
     * pending data to disk. This is done only for channels which are
     * dirty, i.e. have been written to since the last flush here.
     */

    TclOS2FlushDirtyChannels();

    nativePath = Tcl_UtfToExternalDString(NULL, path, -1, &ds);

    /* Convert "D:" to current dir on drive D, ie. "D:." */
    if ((strlen(nativePath) == 2) && (nativePath[1] == ':')) {
        char name[4];

        strcpy(name, nativePath);
        name[2] = '.';
        name[3] = '\0';
        nativePath = name;
    }

#undef stat

    result = stat(nativePath, statPtr);
    p = strrchr(nativePath, '.');
    if (p != NULL) {
        if ((stricmp(p, ".exe") == 0)
                || (stricmp(p, ".com") == 0)
                || (stricmp(p, ".cmd") == 0)
                || (stricmp(p, ".bat") == 0)) {
            statPtr->st_mode |= S_IEXEC;
        }
    }

    return result;
}

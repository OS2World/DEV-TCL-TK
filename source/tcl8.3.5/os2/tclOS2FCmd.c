/*
 * tclOS2FCmd.c
 *
 *      This file implements the OS/2 specific portion of file manipulation 
 *      subcommands of the "file" command. 
 *
 * Copyright (c) 1996-1997 Sun Microsystems, Inc.
 * Copyright (c) 1996-2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tclOS2Int.h"

/*
 * The following constants specify the type of callback when
 * TraverseOS2Tree() calls the traverseProc()
 */

#define DOTREE_PRED   1     /* pre-order directory  */
#define DOTREE_POSTD  2     /* post-order directory */
#define DOTREE_F      3     /* regular file */

/*
 * Callbacks for file attributes code.
 */

static int              CopyFileAtts _ANSI_ARGS_((char *src, char *dst,
                            FILESTATUS3 *fsSource));
static int              GetOS2FileAttributes _ANSI_ARGS_((Tcl_Interp *interp,
                            int objIndex, CONST char *fileName,
                            Tcl_Obj **attributePtrPtr));
static int              SetOS2FileAttributes _ANSI_ARGS_((Tcl_Interp *interp,
                            int objIndex, CONST char *fileName,
                            Tcl_Obj *attributePtr));
#if 0
static int              CannotGetAttribute _ANSI_ARGS_((Tcl_Interp *interp,
                            int objIndex, CONST char *fileName,
                            Tcl_Obj **attributePtr));
static int              CannotSetAttribute _ANSI_ARGS_((Tcl_Interp *interp,
                            int objIndex, CONST char *fileName,
                            Tcl_Obj *attributePtr));
#endif

/*
 * Constants and variables necessary for file attributes subcommand.
 */

/*
enum {
    OS2_ARCHIVE_ATTRIBUTE,
    OS2_HIDDEN_ATTRIBUTE,
    OS2_LONGNAME_ATTRIBUTE,
    OS2_READONLY_ATTRIBUTE,
    OS2_SHORTNAME_ATTRIBUTE,
    OS2_SYSTEM_ATTRIBUTE
};

static int attributeArray[] = {
	FILE_ARCHIVED, FILE_HIDDEN, 0, FILE_READONLY, 0, FILE_SYSTEM
};

char *tclpFileAttrStrings[] = {
	"-archive", "-hidden", "-longname", "-readonly",
        "-shortname", "-system", (char *) NULL
};

const TclFileAttrProcs tclpFileAttrProcs[] = {
        {GetOS2FileAttributes, SetOS2FileAttributes},
        {GetOS2FileAttributes, SetOS2FileAttributes},
        {GetOS2FileLongName, CannotSetAttribute},
        {CannotGetAttribute, CannotSetAttribute},
        {GetOS2FileAttributes, SetOS2FileAttributes},
        {GetOS2FileShortName, CannotSetAttribute},
        {CannotGetAttribute, CannotSetAttribute},
        {GetOS2FileAttributes, SetOS2FileAttributes}
};
*/
enum {
    OS2_ARCHIVE_ATTRIBUTE,
    OS2_HIDDEN_ATTRIBUTE,
    OS2_READONLY_ATTRIBUTE,
    OS2_SYSTEM_ATTRIBUTE
};

static int attributeArray[] = {
	FILE_ARCHIVED, FILE_HIDDEN, FILE_READONLY, FILE_SYSTEM
};

char *tclpFileAttrStrings[] = {
	"-archive", "-hidden", "-readonly", "-system", (char *) NULL
};

const TclFileAttrProcs tclpFileAttrProcs[] = {
        {GetOS2FileAttributes, SetOS2FileAttributes},
        {GetOS2FileAttributes, SetOS2FileAttributes},
        {GetOS2FileAttributes, SetOS2FileAttributes},
        {GetOS2FileAttributes, SetOS2FileAttributes}
};

/*
 * Prototype for the TraverseOS2Tree callback function.
 */

typedef int (TraversalProc)(Tcl_DString *srcPtr, Tcl_DString *dstPtr,
                             FILESTATUS3 *fsSource, int type,
                             Tcl_DString *errorPtr);

/*
 * Declarations for local procedures defined in this file:
 */

static void             StatError (Tcl_Interp *interp, CONST char *fileName);
#if 0
static int              ConvertFileNameFormat (Tcl_Interp *interp,
                            int objIndex, CONST char *fileName, int longShort,
                            Tcl_Obj **attributePtrPtr);
#endif
static int              DoCopyFile(Tcl_DString *srcPtr, Tcl_DString *dstPtr);
static int              DoCreateDirectory(Tcl_DString *pathPtr);
static int              DoDeleteFile(Tcl_DString *pathPtr);
static int              DoRemoveDirectory(Tcl_DString *pathPtr, int recursive,
                            Tcl_DString *errorPtr);
static int              DoRenameFile(char *nativeSrc, Tcl_DString *dstPtr);
static int		TraversalCopy(Tcl_DString *srcPtr, Tcl_DString *dstPtr,
			    FILESTATUS3 *fsSource, int type,
                            Tcl_DString *errorPtr);
static int		TraversalDelete(Tcl_DString *srcPtr,
                            Tcl_DString *dstPtr, FILESTATUS3 *fsSource,
                            int type, Tcl_DString *errorPtr);
static int		TraverseOS2Tree(TraversalProc *traverseProc,
			    Tcl_DString *sourcePtr, Tcl_DString *dstPtr,
			    Tcl_DString *errorPtr);


/*
 *---------------------------------------------------------------------------
 *
 * TclpRenameFile, DoRenameFile --
 *
 *      Changes the name of an existing file or directory, from src to dst.
 *	If src and dst refer to the same file or directory, does nothing
 *	and returns success.  Otherwise if dst already exists, it will be
 *	deleted and replaced by src subject to the following conditions:
 *	    If src is a directory, dst may be an empty directory.
 *	    If src is a file, dst may be a file.
 *	In any other situation where dst already exists, the rename will
 *	fail.  
 *
 * Results:
 *	If the file or directory was successfully renamed, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR and errno is set to
 *	indicate the error.  Some possible values for errno are:
 *
 *      ENAMETOOLONG: src or dst names are too long.
 *	EACCES:     src or dst parent directory can't be read and/or written.
 *	EEXIST:	    dst is a non-empty directory.
 *	EINVAL:	    src is a root directory or dst is a subdirectory of src.
 *	EISDIR:	    dst is a directory, but src is not.
 *	ENOENT:	    src doesn't exist.  src or dst is "".
 *	ENOTDIR:    src is a directory, but dst is not.  
 *	EXDEV:	    src and dst are on different filesystems.
 *
 *	EACCES:     exists an open file already referring to src or dst.
 *	EACCES:     src or dst specify the current working directory (NT).
 *	EACCES:	    src specifies a char device (nul:, com1:, etc.) 
 *	EEXIST:	    dst specifies a char device (nul:, com1:, etc.) (NT)
 *	EACCES:	    dst specifies a char device (nul:, com1:, etc.) (95)
 *	
 * Side effects:
 *	The implementation supports cross-filesystem renames of files,
 *	but the caller should be prepared to emulate cross-filesystem
 *	renames of directories if errno is EXDEV.
 *
 *---------------------------------------------------------------------------
 */

int
TclpRenameFile(
    CONST char *src,			/* Pathname of file or dir to be renamed
                                         * (UTF-8). */ 
    CONST char *dst)			/* New pathname for file or directory
                                         * (UTF-8). */
{
    int result;
    Tcl_DString srcString, dstString;

    Tcl_UtfToExternalDString(NULL, src, -1, &srcString);
    Tcl_UtfToExternalDString(NULL, dst, -1, &dstString);
    if ((Tcl_DStringLength(&srcString) >= MAX_PATH - 1) ||
        (Tcl_DStringLength(&dstString) >= MAX_PATH - 1)) {
        errno = ENAMETOOLONG;
        result = TCL_ERROR;
    } else {
        result = DoRenameFile(Tcl_DStringValue(&srcString), &dstString);
    }
    Tcl_DStringFree(&srcString);
    Tcl_DStringFree(&dstString);
    return result;
}

static int
DoRenameFile(
    char *nativeSrc,            /* Pathname of file or dir to be renamed
                                 * (native). */
    Tcl_DString *dstPtr)        /* New pathname of file or directory
                                 * (native). */
{
    char *nativeDst;
    FILESTATUS3 filestatSrc, filestatDst;
    ULONG srcAttr = 0, dstAttr = 0;
    Tcl_PathType srcPathType, dstPathType;

    nativeDst = Tcl_DStringValue(dstPtr);
    
    rc = DosMove(nativeSrc, nativeDst);
    if (rc == NO_ERROR) {
#ifdef VERBOSE
        printf("DoRenameFile DosMove [%s] -> [%s] OK", nativeSrc, nativeDst);
        fflush(stdout);
#endif
        return TCL_OK;
    }
#ifdef VERBOSE
    printf("DoRenameFile DosMove [%s] -> [%s] ERROR %d\n", nativeSrc, nativeDst, rc);
    fflush(stdout);
#endif

    TclOS2ConvertError(rc);

    rc = DosQueryPathInfo(nativeSrc, FIL_STANDARD, &filestatSrc,
                          sizeof(FILESTATUS3));
    if (rc == NO_ERROR) {
        srcAttr = filestatSrc.attrFile;
    }
#ifdef VERBOSE
      else {
        printf("DoRenameFile DosQueryPathInfo nativeSrc %s ERROR %d\n",
               nativeSrc, rc);
        fflush(stdout);
    }
#endif
    srcPathType = Tcl_GetPathType(nativeSrc);
    rc = DosQueryPathInfo(nativeDst, FIL_STANDARD, &filestatDst,
                          sizeof(FILESTATUS3));
    if (rc == NO_ERROR) {
        dstAttr = filestatDst.attrFile;
    }
#ifdef VERBOSE
      else {
        printf("DoRenameFile DosQueryPathInfo nativeDst %s ERROR %d\n",
               nativeDst, rc);
        fflush(stdout);
    }
#endif
    dstPathType = Tcl_GetPathType(nativeDst);

#ifdef VERBOSE
    printf("   srcAttr %x, dstAttr %x, errno %s\n", srcAttr, dstAttr,
           errno == EBADF ? "EBADF" : (errno==EACCES ? "EACCES" : "?"));
#endif
    if (errno == EBADF) {
	errno = EACCES;
	return TCL_ERROR;
    }
    if (errno == EACCES) {
	decode:
	if (srcAttr & FILE_DIRECTORY) {
	    char srcPath[MAX_PATH], dstPath[MAX_PATH];
	    int srcArgc, dstArgc, len;
	    char **srcArgv, **dstArgv;

	    /* Get full paths */
	    if (srcPathType == TCL_PATH_ABSOLUTE) {
	        strcpy(srcPath, nativeSrc);
	    } else {
	        /* TCL_PATH_RELATIVE or TCL_PATH_VOLUME_RELATIVE */
	        ULONG len = MAX_PATH - 3;
	        ULONG diskNum = 3;
	        if (srcPathType == TCL_PATH_VOLUME_RELATIVE) {
	            srcPath[0] = nativeSrc[0];
	            srcPath[1] = nativeSrc[1];
	            srcPath[2] = '\\';
	            diskNum = nativeSrc[0] - 'A' + 1;
	            if (nativeSrc[0] >= 'a') {
	                diskNum -= ('a' - 'A');
	            }
	            rc = DosQueryCurrentDir(diskNum, srcPath+3, &len);
	            if (rc != NO_ERROR) {
#ifdef VERBOSE
                        printf("   DosQueryCurrentDir nativeSrc ERROR %d\n", rc);
                        fflush(stdout);
#endif
	                return TCL_ERROR;
	            }
#ifdef VERBOSE
                    printf("   DosQueryCurrentDir nativeSrc [%s] OK\n", srcPath);
                    fflush(stdout);
#endif
	            strcat(srcPath, "\\");
	            strcat(srcPath, nativeSrc);
	        } else {
	            ULONG logical;
	            rc = DosQueryCurrentDisk(&diskNum, &logical);
	            if (rc != NO_ERROR) {
#ifdef VERBOSE
                        printf("   DosQueryCurrentDisk nativeSrc ERROR %d\n", rc);
                        fflush(stdout);
#endif
	                return TCL_ERROR;
	            }
#ifdef VERBOSE
                    printf("   DosQueryCurrentDisk nativeSrc OK %d\n", diskNum);
                    fflush(stdout);
#endif
	            srcPath[0] = diskNum + 'A' - 1;
	            srcPath[1] = ':';
	            srcPath[2] = '\\';
	            rc = DosQueryCurrentDir(0, srcPath+3, &len);
	            if (rc != NO_ERROR) {
#ifdef VERBOSE
                        printf("   DosQueryCurrentDir nativeSrc ERROR %d\n", rc);
                        fflush(stdout);
#endif
	                return TCL_ERROR;
	            }
#ifdef VERBOSE
                    printf("   DosQueryCurrentDir nativeSrc [%s] OK\n", srcPath);
                    fflush(stdout);
#endif
	            strcat(srcPath, "\\");
	            strcat(srcPath, nativeSrc);
	        }
	    }
	    if (dstPathType == TCL_PATH_ABSOLUTE) {
	        strcpy(dstPath, nativeDst);
	    } else {
	        /* TCL_PATH_RELATIVE or TCL_PATH_VOLUME_RELATIVE */
	        ULONG len = MAX_PATH - 3;
	        ULONG diskNum = 3;
	        if (dstPathType == TCL_PATH_VOLUME_RELATIVE) {
	            dstPath[0] = nativeDst[0];
	            dstPath[1] = nativeDst[1];
	            dstPath[2] = '\\';
	            diskNum = nativeDst[0] - 'A' + 1;
	            if (nativeDst[0] >= 'a') {
	                diskNum -= ('a' - 'A');
	            }
	            rc = DosQueryCurrentDir(diskNum, dstPath+3, &len);
	            if (rc != NO_ERROR) {
#ifdef VERBOSE
                        printf("   DosQueryCurrentDir nativeDst ERROR %d\n", rc);
                        fflush(stdout);
#endif
	                return TCL_ERROR;
	            }
#ifdef VERBOSE
                    printf("   DosQueryCurrentDir nativeDst [%s] OK\n", dstPath);
                    fflush(stdout);
#endif
	            strcat(dstPath, "\\");
	            strcat(dstPath, nativeDst);
	        } else {
	            ULONG logical;
	            rc = DosQueryCurrentDisk(&diskNum, &logical);
	            if (rc != NO_ERROR) {
#ifdef VERBOSE
                        printf("   DosQueryCurrentDisk nativeDst ERROR %d\n", rc);
                        fflush(stdout);
#endif
	                return TCL_ERROR;
	            }
#ifdef VERBOSE
                    printf("   DosQueryCurrentDisk nativeDst OK %d\n", diskNum);
                    fflush(stdout);
#endif
	            dstPath[0] = diskNum + 'A' - 1;
	            dstPath[1] = ':';
	            dstPath[2] = '\\';
	            rc = DosQueryCurrentDir(0, dstPath+3, &len);
	            if (rc != NO_ERROR) {
#ifdef VERBOSE
                        printf("   DosQueryCurrentDir nativeDst ERROR %d\n", rc);
                        fflush(stdout);
#endif
	                return TCL_ERROR;
	            }
#ifdef VERBOSE
                    printf("   DosQueryCurrentDir nativeDst [%s] OK\n", dstPath);
                    fflush(stdout);
#endif
	            strcat(dstPath, "\\");
	            strcat(dstPath, nativeDst);
	        }
	    }
	    len = strlen(srcPath);
	    if (strnicmp(srcPath, dstPath, len) == 0 &&
	        (*(dstPath+len) == '\0' || *(dstPath+len) == '\\' )) {
		/*
		 * Trying to move a directory into itself.
		 */

#ifdef VERBOSE
                printf("   strnicmp(%s,%s)==0 ==> EINVAL\n", srcPath, dstPath);
                fflush(stdout);
#endif
		errno = EINVAL;
		return TCL_ERROR;
	    }
	    Tcl_SplitPath(srcPath, &srcArgc, &srcArgv);
#ifdef VERBOSE
            printf("   strnicmp(%s,%s) != 0\n", srcPath, dstPath);
            fflush(stdout);
#endif
	    Tcl_SplitPath(dstPath, &dstArgc, &dstArgv);
	    if (srcArgc == 1) {
		/*
		 * They are trying to move a root directory.  Whether
		 * or not it is across filesystems, this cannot be
		 * done.
		 */

#ifdef VERBOSE
                printf("   srcArgc == 1 ==> EINVAL\n");
                fflush(stdout);
#endif

		Tcl_SetErrno(EINVAL);
	    } else if ((srcArgc > 0) && (dstArgc > 0) &&
		    (stricmp(srcArgv[0], dstArgv[0]) != 0)) {
		/*
		 * If nativeSrc is a directory and dst filesystem != nativeSrc
		 * filesystem, errno should be EXDEV.  It is very
		 * important to get this behavior, so that the caller
		 * can respond to a cross filesystem rename by
		 * simulating it with copy and delete.  The DosMove
		 * system call already returns EXDEV (ERROR_NOT_SAME_DEVICE)
		 * when moving a file between filesystems.
		 */

#ifdef VERBOSE
                printf("   EXDEV\n");
                fflush(stdout);
#endif

		Tcl_SetErrno(EXDEV);
	    }

	    ckfree((char *) srcArgv);
	    ckfree((char *) dstArgv);
	}

        /*
         * Other types of access failure is that dst is a read-only
         * filesystem, that an open file referred to src or dest, or that
         * src or dest specified the current working directory on the
         * current filesystem.  EACCES is returned for those cases.
         */

    } else if (Tcl_GetErrno() == EEXIST) {
        /*
         * Reports EEXIST any time the target already exists.  If it makes
         * sense, remove the old file and try renaming again.
         */
#ifdef VERBOSE
        printf("   EEXIST\n");
        fflush(stdout);
#endif

	if (srcAttr & FILE_DIRECTORY) {
	    if (dstAttr & FILE_DIRECTORY) {
		/*
		 * Overwrite empty dst directory with src directory.  The
		 * following call will remove an empty directory.  If it
		 * fails, it's because it wasn't empty.
		 */

		if (DoRemoveDirectory(dstPtr, 0, NULL) == TCL_OK) {
		    /*
		     * Now that that empty directory is gone, we can try
		     * renaming again.  If that fails, we'll put this empty
		     * directory back, for completeness.
		     */

                    rc = DosMove(nativeSrc, nativeDst);
                    if (rc == NO_ERROR) {
#ifdef VERBOSE
                        printf("   retry DosMove [%s]->[%s] OK\n", nativeSrc,
                               nativeDst);
                        fflush(stdout);
#endif
			return TCL_OK;
		    }
#ifdef VERBOSE
                    printf("   retry DosMove [%s]->[%s] ERROR %d\n", nativeSrc,
                           nativeDst, rc);
                    fflush(stdout);
#endif

		    /*
		     * Some new error has occurred.  Don't know what it
		     * could be, but report this one.
		     */

		    TclOS2ConvertError(rc);
		    rc = DosCreateDir(nativeDst, (PEAOP2)NULL);
#ifdef VERBOSE
                    printf("   DosCreateDir %s returns %d\n", nativeDst, rc);
                    fflush(stdout);
#endif
                    rc = DosSetPathInfo(nativeDst, FIL_STANDARD, &filestatDst,
                                        sizeof(FILESTATUS3), (ULONG)0);
#ifdef VERBOSE
                    printf("   DosSetPathInfo %s returns %d\n", nativeDst, rc);
                    fflush(stdout);
#endif
		    if (Tcl_GetErrno() == EACCES) {
			/*
			 * Decode the EACCES to a more meaningful error.
			 */

			goto decode;
		    }
		}
	    } else {	/* (dstAttr & FILE_DIRECTORY) == 0 */
		Tcl_SetErrno(ENOTDIR);
	    }
	} else {    /* (srcAttr & FILE_DIRECTORY) == 0 */
	    if (dstAttr & FILE_DIRECTORY) {
		Tcl_SetErrno(EISDIR);
	    } else {
		/*
		 * Overwrite existing file by:
		 * 
		 * 1. Rename existing file to temp name.
		 * 2. Rename old file to new name.
		 * 3. If success, delete temp file.  If failure,
		 *    put temp file back to old name.
		 */

		char tempName[MAX_PATH];
		int result;
		ULONG timeVal[2];
		
	        if (dstPathType == TCL_PATH_ABSOLUTE) {
	            strcpy(tempName, nativeDst);
	        } else {
	            /* TCL_PATH_RELATIVE or TCL_PATH_VOLUME_RELATIVE */
	            ULONG len = MAX_PATH - 3;
	            ULONG diskNum = 3;
	            if (dstPathType == TCL_PATH_VOLUME_RELATIVE) {
#ifdef VERBOSE
                        printf("   nativeDst TCL_PATH_VOLUME_RELATIVE\n");
#endif
	                tempName[0] = nativeDst[0];
	                tempName[1] = nativeDst[1];
	                tempName[2] = '\\';
	                diskNum = nativeDst[0] - 'A' + 1;
	                if (nativeDst[0] >= 'a') {
	                    diskNum -= ('a' - 'A');
	                }
	                rc = DosQueryCurrentDir(diskNum, tempName+3, &len);
	                if (rc != NO_ERROR) {
	                    return TCL_ERROR;
	                }
	            } else {
	                ULONG logical;
#ifdef VERBOSE
                        printf("   nativeDst != TCL_PATH_VOLUME_RELATIVE\n");
#endif
	                rc = DosQueryCurrentDisk(&diskNum, &logical);
	                if (rc != NO_ERROR) {
	                    return TCL_ERROR;
	                }
	                tempName[0] = diskNum + 'A' - 1;
	                tempName[1] = ':';
	                tempName[2] = '\\';
	                rc = DosQueryCurrentDir(0, tempName+3, &len);
	                if (rc != NO_ERROR) {
	                    return TCL_ERROR;
	                }
	            }
	        }
		result = TCL_ERROR;
                /* Determine unique value from time */
                rc = DosQuerySysInfo(QSV_TIME_LOW - 1, QSV_TIME_HIGH - 1,
                                     (PVOID)timeVal, sizeof(timeVal));
		if (rc == NO_ERROR) {
                    /* Add unique name to path */
                    sprintf(tempName, "%s\\tclr%04hx.TMP", tempName,
                            (SHORT)timeVal[0]);
		    /*
		     * Strictly speaking, need the following DosDelete and
		     * DosMove to be joined as an atomic operation so no
		     * other app comes along in the meantime and creates the
		     * same temp file.
		     */
		     
		    DosDelete(tempName);
                    rc = DosMove(nativeDst, tempName);
#ifdef VERBOSE
                    printf("   DosMove %s->%s returns %d\n", nativeDst,
                           tempName, rc);
#endif
                    if (rc == NO_ERROR) {
                        rc = DosMove(nativeSrc, nativeDst);
#ifdef VERBOSE
                        printf("   DosMove %s->%s returns %d\n", nativeSrc,
                               nativeDst, rc);
#endif
                        if (rc == NO_ERROR) {
                            filestatDst.attrFile = FILE_NORMAL;
                            rc = DosSetPathInfo(tempName, FIL_STANDARD,
                                                &filestatDst,
                                                sizeof(FILESTATUS3), (ULONG)0);
			    DosDelete(tempName);
			    return TCL_OK;
			} else {
			    DosDelete(nativeDst);
                            rc = DosMove(tempName, nativeDst);
#ifdef VERBOSE
                            printf("   DosMove %s->%s (restore) returns %d\n",
                                   tempName, nativeDst, rc);
#endif
			}
		    } 

		    /*
		     * Can't backup dst file or move src file.  Return that
		     * error.  Could happen if an open file refers to dst.
		     */

		    TclOS2ConvertError(rc);
		    if (Tcl_GetErrno() == EACCES) {
			/*
			 * Decode the EACCES to a more meaningful error.
			 */

			goto decode;
		    }
		}
		return result;
	    }
	}
    }
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpCopyFile, DoCopyFile --
 *
 *      Copy a single file (not a directory).  If dst already exists and
 *	is not a directory, it is removed.
 *
 * Results:
 *	If the file was successfully copied, returns TCL_OK.  Otherwise
 *	the return value is TCL_ERROR and errno is set to indicate the
 *	error.  Some possible values for errno are:
 *
 *	EACCES:     src or dst parent directory can't be read and/or written.
 *	EISDIR:	    src or dst is a directory.
 *	ENOENT:	    src doesn't exist.  src or dst is "".
 *
 *	EACCES:     exists an open file already referring to dst (95).
 *	EACCES:	    src specifies a char device (nul:, com1:, etc.) (NT)
 *	ENOENT:	    src specifies a char device (nul:, com1:, etc.) (95)
 *
 * Side effects:
 *	It is not an error to copy to a char device.
 *
 *---------------------------------------------------------------------------
 */

int 
TclpCopyFile(
    CONST char *src,		/* Pathname of file to be copied (UTF-8). */
    CONST char *dst)		/* Pathname of file to copy to (UTF-8). */
{
    int result;
    Tcl_DString srcString, dstString;

    Tcl_UtfToExternalDString(NULL, src, -1, &srcString);
    Tcl_UtfToExternalDString(NULL, dst, -1, &dstString);
    result = DoCopyFile(&srcString, &dstString);
    Tcl_DStringFree(&srcString);
    Tcl_DStringFree(&dstString);
    return result;
}

static int
DoCopyFile(srcPtr, dstPtr)
    Tcl_DString *srcPtr;        /* Pathname of file to be copied (native). */
    Tcl_DString *dstPtr;        /* Pathname of file to copy to (native). */
{
    CONST char *src, *dst;
    FILESTATUS3 filestatSrc, filestatDst;
#ifdef VERBOSE
    printf("TclpCopyFile [%s] -> [%s]\n", src, dst);
    fflush(stdout);
#endif

    src = Tcl_DStringValue(srcPtr);
    dst = Tcl_DStringValue(dstPtr);

    rc = DosCopy(src, dst, DCPY_EXISTING);
    if (rc == NO_ERROR) {
        return TCL_OK;
    }
#ifdef VERBOSE
    printf("TclpCopyFile DosCopy %s->%s ERROR %d\n", src, dst, rc);
    fflush(stdout);
#endif

    TclOS2ConvertError(rc);
#ifdef VERBOSE
    printf("   errno %s\n",
           errno == EBADF ? "EBADF" : (errno==EACCES ? "EACCES" : "?"));
#endif
    if (Tcl_GetErrno() == EBADF) {
	Tcl_SetErrno(EACCES);
	return TCL_ERROR;
    }
    if (Tcl_GetErrno() == EACCES) {
	ULONG srcAttr = 0, dstAttr = 0;

        rc = DosQueryPathInfo(src, FIL_STANDARD, &filestatSrc,
                              sizeof(FILESTATUS3));
#ifdef VERBOSE
        printf("   DosQueryPathInfo src [%s] returns %d\n", src, rc);
        fflush(stdout);
#endif
        if (rc == NO_ERROR) {
            srcAttr = filestatSrc.attrFile;
            rc = DosQueryPathInfo(dst, FIL_STANDARD, &filestatDst,
                                  sizeof(FILESTATUS3));
#ifdef VERBOSE
            printf("   DosQueryPathInfo dst [%s] returns %d\n", dst, rc);
            fflush(stdout);
#endif
            if (rc == NO_ERROR) {
                dstAttr = filestatDst.attrFile;
	    }
	    if ((srcAttr & FILE_DIRECTORY) ||
		    (dstAttr & FILE_DIRECTORY)) {
#ifdef VERBOSE
                printf("   errno => EISDIR\n");
                fflush(stdout);
#endif
		Tcl_SetErrno(EISDIR);
	    }
	    if (dstAttr & FILE_READONLY) {
                filestatDst.attrFile = dstAttr & ~FILE_READONLY;
                rc = DosSetPathInfo(dst, FIL_STANDARD, &filestatDst,
                                    sizeof(FILESTATUS3), (ULONG)0);
#ifdef VERBOSE
                printf("   DosSetPathInfo dst [%s] returns %d\n", dst, rc);
                fflush(stdout);
#endif
                rc = DosCopy(src, dst, DCPY_EXISTING);
#ifdef VERBOSE
                printf("   DosCopy [%s]->[%s] returns %d\n", src, dst, rc);
                fflush(stdout);
#endif
                if (rc == NO_ERROR) {
		    return TCL_OK;
		}
		/*
		 * Still can't copy onto dst.  Return that error, and
		 * restore attributes of dst.
		 */

		TclOS2ConvertError(rc);
                filestatDst.attrFile = dstAttr;
                rc = DosSetPathInfo(dst, FIL_STANDARD, &filestatDst,
                                    sizeof(FILESTATUS3), (ULONG)0);
	    }
	}
    }
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpDeleteFile, DoDeleteFile --
 *
 *      Removes a single file (not a directory).
 *
 * Results:
 *	If the file was successfully deleted, returns TCL_OK.  Otherwise
 *	the return value is TCL_ERROR and errno is set to indicate the
 *	error.  Some possible values for errno are:
 *
 *	EACCES:     a parent directory can't be read and/or written.
 *	EISDIR:	    path is a directory.
 *	ENOENT:	    path doesn't exist or is "".
 *
 *	EACCES:     exists an open file already referring to path.
 *	EACCES:	    path is a char device (nul:, com1:, etc.)
 *
 * Side effects:
 *      The file is deleted, even if it is read-only.
 *
 *---------------------------------------------------------------------------
 */

int
TclpDeleteFile(
    CONST char *path)		/* Pathname of file to be removed (UTF-8). */
{
    int result;
    Tcl_DString pathString;

#ifdef VERBOSE
    printf("TclpDeleteFile [%s]\n", path);
    fflush(stdout);
#endif
    Tcl_UtfToExternalDString(NULL, path, -1, &pathString);
    result = DoDeleteFile(&pathString);
    Tcl_DStringFree(&pathString);
    return result;
}

static int
DoDeleteFile(pathPtr)
    Tcl_DString *pathPtr;       /* Pathname of file to be removed (native). */
{
    CONST char *path;
    FILESTATUS3 filestat;
    ULONG attr = 0;

    path = Tcl_DStringValue(pathPtr);
#ifdef VERBOSE
    printf("DoDeleteFile [%s]\n", path);
    fflush(stdout);
#endif
    rc = DosDelete(path);
    if (rc == NO_ERROR) {
#ifdef VERBOSE
        printf("   DosDelete [%s] OK\n", path);
        fflush(stdout);
#endif
	return TCL_OK;
    }
#ifdef VERBOSE
    printf("DoDeleteFile DosDelete %s ERROR %d\n", path, rc);
    fflush(stdout);
#endif
    TclOS2ConvertError(rc);
    if (Tcl_GetErrno() == EACCES) {
        rc = DosQueryPathInfo(path, FIL_STANDARD, &filestat,
                              sizeof(FILESTATUS3));
#ifdef VERBOSE
        printf("   DosQueryPathInfo [%s] returns %d\n", path, rc);
        fflush(stdout);
#endif
        if (rc == NO_ERROR) {
            attr = filestat.attrFile;
	    if (attr & FILE_DIRECTORY) {
		Tcl_SetErrno(EISDIR);
	    } else if (attr & FILE_READONLY) {
                filestat.attrFile = attr & ~FILE_READONLY;
                rc = DosSetPathInfo(path, FIL_STANDARD, &filestat,
                                    sizeof(FILESTATUS3), (ULONG)0);
#ifdef VERBOSE
                printf("   DosSetPathInfo [%s] returns %d\n", path, rc);
                fflush(stdout);
#endif
                rc = DosDelete(path);
#ifdef VERBOSE
                printf("   DosDelete [%s] returns %d\n", path, rc);
                fflush(stdout);
#endif
                if (rc == NO_ERROR) {
		    return TCL_OK;
		}
		TclOS2ConvertError(rc);
                filestat.attrFile = attr;
                rc = DosSetPathInfo(path, FIL_STANDARD, &filestat,
                                    sizeof(FILESTATUS3), (ULONG)0);
#ifdef VERBOSE
                printf("   DosSetPathInfo [%s] returns %d\n", path, rc);
                fflush(stdout);
#endif
	    }
	}
    }

    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpCreateDirectory, DoCreateDirectory --
 *
 *      Creates the specified directory.  All parent directories of the
 *	specified directory must already exist.  The directory is
 *	automatically created with permissions so that user can access
 *	the new directory and create new files or subdirectories in it.
 *
 * Results:
 *	If the directory was successfully created, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR and errno is set to
 *	indicate the error.  Some possible values for errno are:
 *
 *	EACCES:     a parent directory can't be read and/or written.
 *	EEXIST:	    path already exists.
 *	ENOENT:	    a parent directory doesn't exist.
 *
 * Side effects:
 *      A directory is created.
 *
 *---------------------------------------------------------------------------
 */

int
TclpCreateDirectory(
    CONST char *path)		/* Pathname of directory to create (UTF-8). */
{
    int result;
    Tcl_DString pathString;

    Tcl_UtfToExternalDString(NULL, path, -1, &pathString);
    result = DoCreateDirectory(&pathString);
    Tcl_DStringFree(&pathString);
    return result;
}

static int
DoCreateDirectory(pathPtr)
    Tcl_DString *pathPtr;       /* Pathname of directory to create (native). */
{
    CONST char *path;
    FILESTATUS3 filestat;

    path = Tcl_DStringValue(pathPtr);
#ifdef VERBOSE
    printf("TclpCreateDirectory [%s]\n", path);
    fflush(stdout);
#endif
    rc = DosCreateDir(path, (PEAOP2)NULL);
    if (rc != NO_ERROR) {
#ifdef VERBOSE
        printf("TclpCreateDirectory DosCreateDir %s ERROR %d\n", path, rc);
        fflush(stdout);
#endif
	TclOS2ConvertError(rc);
	/*
	 * fCmd.test 10.5 shows we have to generate an EEXIST in case this
	 * directory already exists (OS/2 generates EACCES).
	 */
        if (Tcl_GetErrno() == EACCES) {
            rc = DosQueryPathInfo(path, FIL_STANDARD, &filestat,
                                  sizeof(FILESTATUS3));
#ifdef VERBOSE
            printf("   DosQueryPathInfo [%s] returns %d\n", path, rc);
            fflush(stdout);
#endif
            if (rc == NO_ERROR) {
                Tcl_SetErrno(EEXIST);
#ifdef VERBOSE
                printf("   errno => EEXIST\n");
                fflush(stdout);
#endif
	    }
	}
	return TCL_ERROR;
    }   
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpCopyDirectory --
 *
 *      Recursively copies a directory.  The target directory dst must
 *	not already exist.  Note that this function does not merge two
 *	directory hierarchies, even if the target directory is an an
 *	empty directory.
 *
 * Results:
 *	If the directory was successfully copied, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR, errno is set to indicate
 *	the error, and the pathname of the file that caused the error
 *	is stored in errorPtr.  See TclpCreateDirectory and TclpCopyFile
 *	for a description of possible values for errno.
 *
 * Side effects:
 *      An exact copy of the directory hierarchy src will be created
 *	with the name dst.  If an error occurs, the error will
 *      be returned immediately, and remaining files will not be
 *	processed.
 *
 *---------------------------------------------------------------------------
 */

int
TclpCopyDirectory(src, dst, errorPtr)
    CONST char *src;            /* Pathname of directory to be copied
                                 * (UTF-8). */
    CONST char *dst;            /* Pathname of target directory (UTF-8). */
    Tcl_DString *errorPtr;      /* If non-NULL, uninitialized or free
                                 * DString filled with UTF-8 name of file
                                 * causing error. */
{
    int result;
    Tcl_DString srcString, dstString;

    Tcl_UtfToExternalDString(NULL, src, -1, &srcString);
    Tcl_UtfToExternalDString(NULL, dst, -1, &dstString);
#ifdef VERBOSE
    printf("TclpCopyDirectory [%s] -> [%s]\n", src, dst);
    fflush(stdout);
#endif

    result = TraverseOS2Tree(TraversalCopy, &srcString, &dstString, errorPtr);
    Tcl_DStringFree(&srcString);
    Tcl_DStringFree(&dstString);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpRemoveDirectory, DoRemoveDirectory -- 
 *
 *	Removes directory (and its contents, if the recursive flag is set).
 *
 * Results:
 *	If the directory was successfully removed, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR, errno is set to indicate
 *	the error, and the pathname of the file that caused the error
 *	is stored in errorPtr.  Some possible values for errno are:
 *
 *	EACCES:     path directory can't be read and/or written.
 *	EEXIST:	    path is a non-empty directory.
 *	EINVAL:	    path is root directory or current directory.
 *	ENOENT:	    path doesn't exist or is "".
 * 	ENOTDIR:    path is not a directory.
 *
 *	EACCES:	    path is a char device (nul:, com1:, etc.) (95)
 *	EINVAL:	    path is a char device (nul:, com1:, etc.) (NT)
 *
 * Side effects:
 *	Directory removed.  If an error occurs, the error will be returned
 *	immediately, and remaining files will not be deleted.
 *
 *----------------------------------------------------------------------
 */

int
TclpRemoveDirectory(path, recursive, errorPtr)
    CONST char *path;           /* Pathname of directory to be removed
                                 * (UTF-8). */
    int recursive;              /* If non-zero, removes directories that
                                 * are nonempty.  Otherwise, will only remove
                                 * empty directories. */
    Tcl_DString *errorPtr;      /* If non-NULL, uninitialized or free
                                 * DString filled with UTF-8 name of file
                                 * causing error. */
{
    int result;
    Tcl_DString pathString;

#ifdef VERBOSE
    printf("TclpRemoveDirectory [%s] recursive %d\n", path, recursive);
    fflush(stdout);
#endif
    Tcl_UtfToExternalDString(NULL, path, -1, &pathString);
    result = DoRemoveDirectory(&pathString, recursive, errorPtr);
    Tcl_DStringFree(&pathString);

    return result;
}

static int
DoRemoveDirectory(pathPtr, recursive, errorPtr)
    Tcl_DString *pathPtr;       /* Pathname of directory to be removed
                                 * (native). */
    int recursive;              /* If non-zero, removes directories that
                                 * are nonempty.  Otherwise, will only remove
                                 * empty directories. */
    Tcl_DString *errorPtr;      /* If non-NULL, uninitialized or free
                                 * DString filled with UTF-8 name of file
                                 * causing error. */
{
    CONST char *path;
    FILESTATUS3 filestat;
    ULONG attr = 0;

    path = Tcl_DStringValue(pathPtr);
#ifdef VERBOSE
    printf("DoRemoveDirectory [%s] (recursive %d)\n", path, recursive);
    fflush(stdout);
#endif
    rc = DosDeleteDir(path);
    if (rc == NO_ERROR) {
	return TCL_OK;
    }
#ifdef VERBOSE
    printf("DoRemoveDirectory DosDeleteDir %s ERROR %d\n", path, rc);
    fflush(stdout);
#endif
    TclOS2ConvertError(rc);
    if (Tcl_GetErrno() == EACCES) {
        rc = DosQueryPathInfo(path, FIL_STANDARD, &filestat,
                              sizeof(FILESTATUS3));
#ifdef VERBOSE
        printf("   DosQueryPathInfo [%s] returns %d\n", path, rc);
        fflush(stdout);
#endif
        if (rc == NO_ERROR) {
            Tcl_DString buffer;
            char *find;
            int len;
            HDIR handle;
            FILEFINDBUF3 data;
            ULONG filesAtATime = 1;

            attr = filestat.attrFile;
	    if ((attr & FILE_DIRECTORY) == 0) {
		/* 
		 * OS/2 reports calling DosDeleteDir on a file as an 
		 * EACCES, not an ENOTDIR.
		 */
		
		Tcl_SetErrno(ENOTDIR);
		goto end;
	    }

	    if (attr & FILE_READONLY) {
                filestat.attrFile = attr & ~FILE_READONLY;
                rc = DosSetPathInfo(path, FIL_STANDARD, &filestat,
                                    sizeof(FILESTATUS3), (ULONG)0);
                rc = DosDeleteDir(path);
                if (rc == NO_ERROR) {
		    return TCL_OK;
		}
		TclOS2ConvertError(rc);
                filestat.attrFile = attr;
                rc = DosSetPathInfo(path, FIL_STANDARD, &filestat,
                                    sizeof(FILESTATUS3), (ULONG)0);
	    }

	    /* 
	     * OS/2 reports removing a non-empty directory as
	     * an EACCES, not an EEXIST.  If the directory is not empty,
	     * change errno so caller knows what's going on.
	     */

            Tcl_DStringInit(&buffer);
            find = Tcl_DStringAppend(&buffer, path, -1);
            len = Tcl_DStringLength(&buffer);
            if ((len > 0) && (find[len - 1] != '\\')) {
                Tcl_DStringAppend(&buffer, "\\", 1);
            }
            find = Tcl_DStringAppend(&buffer, "*.*", 3);
            /* Use a new handle since we don't know if another find is active */
            handle = HDIR_CREATE;
            rc = DosFindFirst(find, &handle, FILE_NORMAL | FILE_DIRECTORY,
                              &data, sizeof(data), &filesAtATime, FIL_STANDARD);
#ifdef VERBOSE
            printf("   DosFindFirst %s returns %x (%s) (%d)\n", find, rc,
                   data.achName, filesAtATime);
#endif
            if (rc == NO_ERROR) {
                while (1) {
            	    if ((strcmp(data.achName, ".") != 0)
            		&& (strcmp(data.achName, "..") != 0)) {
            	        /*
            	         * Found something in this directory.
            	         */
            
            	        Tcl_SetErrno(EEXIST);
            	        break;
            	    }
            	    rc = DosFindNext(handle, &data, sizeof(data),
            	                     &filesAtATime);
#ifdef VERBOSE
                    printf("   DosFindNext returns %x (%s) (%d)\n", rc,
                           data.achName, filesAtATime);
#endif
            	    if (rc != NO_ERROR) {
            	        break;
            	    }
                }
                DosFindClose(handle);
            }
            Tcl_DStringFree(&buffer);
	}
    }
    if (errno == ENOTEMPTY) {
	/* 
	 * The caller depends on EEXIST to signify that the directory is
	 * not empty, not ENOTEMPTY. 
	 */

	Tcl_SetErrno(EEXIST);
    }
    if ((recursive != 0) && (Tcl_GetErrno() == EEXIST)) {
	/*
	 * The directory is nonempty, but the recursive flag has been
	 * specified, so we recursively remove all the files in the directory.
	 */

	return TraverseOS2Tree(TraversalDelete, pathPtr, NULL, errorPtr);
    }

    end:
    if (errorPtr != NULL) {
        Tcl_ExternalToUtfDString(NULL, path, -1, errorPtr);
    }
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * TraverseOS2Tree --
 *
 *      Traverse directory tree specified by sourcePtr, calling the function 
 *	traverseProc for each file and directory encountered.  If destPtr 
 *	is non-null, each of name in the sourcePtr directory is appended to 
 *	the directory specified by destPtr and passed as the second argument 
 *	to traverseProc() .
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None caused by TraverseOS2Tree, however the user specified 
 *	traverseProc() may change state.  If an error occurs, the error will
 *      be returned immediately, and remaining files will not be processed.
 *
 *---------------------------------------------------------------------------
 */

static int 
TraverseOS2Tree(
    TraversalProc *traverseProc,/* Function to call for every file and
				 * directory in source hierarchy. */
    Tcl_DString *sourcePtr,	/* Pathname of source directory to be
				 * traversed. */
    Tcl_DString *targetPtr,	/* Pathname of directory to traverse in
				 * parallel with source directory. */
    Tcl_DString *errorPtr)	/* If non-NULL, uninitialized or free
                                 * DString filled with UTF-8 name of file
                                 * causing error. */
{
    FILESTATUS3 filestatSrc;
    ULONG sourceAttr = 0;
    char *nativeSource, *nativeErrfile;
    int result, found, sourceLen = 0, targetLen = 0, oldSourceLen, oldTargetLen;
    HDIR handle;
    FILEFINDBUF3 data;
    ULONG filesAtATime = 1;
#ifdef VERBOSE
    printf("TraverseOS2Tree [%s] -> [%s]\n", Tcl_DStringValue(sourcePtr),
           targetPtr ? Tcl_DStringValue(targetPtr) : "NULL");
    fflush(stdout);
#endif

    nativeErrfile = NULL;
    result = TCL_OK;
    oldTargetLen = 0;		/* lint. */

    nativeSource = Tcl_DStringValue(sourcePtr);
    oldSourceLen = Tcl_DStringLength(sourcePtr);

    /* Get File Attributes source BEGIN */
    rc = DosQueryPathInfo(nativeSource, FIL_STANDARD, &filestatSrc,
                          sizeof(FILESTATUS3));		/* INTL: Native. */
#ifdef VERBOSE
    printf("   DosQueryPathInfo nativeSource [%s] returns %d\n", nativeSource,
           rc);
    fflush(stdout);
#endif
    if (rc == NO_ERROR) {
        sourceAttr = filestatSrc.attrFile;
    } else {
	nativeErrfile = nativeSource;
	goto end;
    }
    /* Get File Attributes source END */

    if ((sourceAttr & FILE_DIRECTORY) == 0) {
	/*
	 * Process the regular file
	 */

	return (*traverseProc)(sourcePtr, targetPtr, &filestatSrc, DOTREE_F,
                               errorPtr);
    }

    nativeSource = Tcl_DStringAppend(sourcePtr, "\\*.*", 4);
    /* Use a new handle since we can be doing this recursively */
    handle = HDIR_CREATE;
    /* INTL: Native: */
    rc = DosFindFirst(nativeSource, &handle, FILE_NORMAL | FILE_DIRECTORY,
                      &data, sizeof(data), &filesAtATime, FIL_STANDARD);
#ifdef VERBOSE
    printf("   DosFindFirst %s returns %x (%s) (%d)\n", nativeSource, rc,
           data.achName, filesAtATime);
#endif
    if (rc != NO_ERROR) {
	/* 
	 * Can't read directory
	 */

	TclOS2ConvertError(rc);
	nativeErrfile = nativeSource;
	goto end;
    }

    nativeSource[oldSourceLen + 1] = '\0';
    Tcl_DStringSetLength(sourcePtr, oldSourceLen);
    result = (*traverseProc)(sourcePtr, targetPtr, &filestatSrc, DOTREE_PRED,
                             errorPtr);
    if (result != TCL_OK) {
	DosFindClose(handle);
	return result;
    }

    sourceLen = oldSourceLen + 1;
    Tcl_DStringAppend(sourcePtr, "\\", 1);

    if (targetPtr != NULL) {
        oldTargetLen = Tcl_DStringLength(targetPtr);

	targetLen = oldTargetLen + 1;
	Tcl_DStringAppend(targetPtr, "\\", 1);
    }

    found = 1;
    while (found) {
	if ((strcmp(data.achName, ".") != 0)
	        && (strcmp(data.achName, "..") != 0)) {
	    /* 
	     * Append name after slash, and recurse on the file. 
	     */

#ifdef VERBOSE
    printf("   sourcePtr was [%s]", Tcl_DStringValue(sourcePtr));
#endif
	    Tcl_DStringAppend(sourcePtr, data.achName, -1);
#ifdef VERBOSE
    printf(", now [%s]\n", Tcl_DStringValue(sourcePtr));
    fflush(stdout);
#endif
	    if (targetPtr != NULL) {
		Tcl_DStringAppend(targetPtr, data.achName, -1);
	    }
	    result = TraverseOS2Tree(traverseProc, sourcePtr, targetPtr, 
		    errorPtr);
	    if (result != TCL_OK) {
		break;
	    }

	    /*
	     * Remove name after slash.
	     */

	    Tcl_DStringSetLength(sourcePtr, sourceLen);
	    if (targetPtr != NULL) {
		Tcl_DStringSetLength(targetPtr, targetLen);
	    }
	}
        rc = DosFindNext(handle, &data, sizeof(data), &filesAtATime);
#ifdef VERBOSE
        printf("   DosFindNext returns %x (%s) (%d)\n", rc, data.achName,
               filesAtATime);
#endif
        if (rc != NO_ERROR) {
	    found = 0;
	}
    }
    DosFindClose(handle);

    /*
     * Strip off the trailing slash we added
     */

    Tcl_DStringSetLength(sourcePtr, oldSourceLen);
    if (targetPtr != NULL) {
	Tcl_DStringSetLength(targetPtr, oldTargetLen);
    }

    if (result == TCL_OK) {
	/*
	 * Call traverseProc() on a directory after visiting all the
	 * files in that directory.
	 */

	result = (*traverseProc)(sourcePtr, targetPtr, &filestatSrc,
                  DOTREE_POSTD, errorPtr);
    }
    end:
    if (nativeErrfile != NULL) {
	TclOS2ConvertError(rc);
	if (errorPtr != NULL) {
	    Tcl_ExternalToUtfDString(NULL, nativeErrfile, -1, errorPtr);
	}
	result = TCL_ERROR;
    }
	    
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TraversalCopy
 *
 *      Called from TraverseOS2Tree in order to execute a recursive
 *      copy of a directory.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      Depending on the value of type, src may be copied to dst.
 *      
 *----------------------------------------------------------------------
 */

static int 
TraversalCopy(
    Tcl_DString *srcPtr,	/* Source pathname to copy (native). */
    Tcl_DString *dstPtr,	/* Destination pathname of copy (native). */
    FILESTATUS3 *fsSource,      /* File status for srcPtr. */
    int type,			/* Reason for call - see TraverseOS2Tree() */
    Tcl_DString *errorPtr)	/* If non-NULL, uninitialized or free
 				 * DString filled with UTF-8 name of file
 				 * causing error. */
{
#ifdef VERBOSE
    printf("TraversalCopy [%s] -> [%s] (type %s)\n", Tcl_DStringValue(srcPtr),
           Tcl_DStringValue(dstPtr),
           type == DOTREE_PRED ? "DOTREE_PRED"
                   : (type == DOTREE_POSTD ? "DOTREE_POSTD"
                              : (type == DOTREE_F ? "DOTREE_F"
                                         : "???")));
    fflush(stdout);
#endif
    switch (type) {
	case DOTREE_F: {
	    if (DoCopyFile(srcPtr, dstPtr) == TCL_OK) {
		return TCL_OK;
	    }
	    break;
	}

	case DOTREE_PRED: {
	    if (DoCreateDirectory(dstPtr) == TCL_OK) {
                if (CopyFileAtts(Tcl_DStringValue(srcPtr),
                        Tcl_DStringValue(dstPtr), fsSource) == TCL_OK) {
                    return TCL_OK;
                }
	    }
	    break;
	}

        case DOTREE_POSTD: {
	    return TCL_OK;
	}
    }

    /*
     * There shouldn't be a problem with src, because we already
     * checked it to get here.
     */

    if (errorPtr != NULL) {
	Tcl_ExternalToUtfDString(NULL, Tcl_DStringValue(dstPtr),
                                 Tcl_DStringLength(dstPtr), errorPtr);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * TraversalDelete --
 *
 *      Called by procedure TraverseOS2Tree for every file and
 *      directory that it encounters in a directory hierarchy. This
 *      procedure unlinks files, and removes directories after all the
 *      containing files have been processed.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      Files or directory specified by src will be deleted. If an
 *      error occurs, the windows error is converted to a Posix error
 *      and errno is set accordingly.
 *
 *----------------------------------------------------------------------
 */

static int
TraversalDelete( 
    Tcl_DString *srcPtr,	/* Source pathname (native). */
    Tcl_DString *ignore,	/* Destination pathname (not used). */
    FILESTATUS3 *fsSource,      /* File status for src (not used). */
    int type,			/* Reason for call - see TraverseOS2Tree(). */
    Tcl_DString *errorPtr)	/* If non-NULL, uninitialized or free
 				 * DString filled with UTF-8 name of file
 				 * causing error. */
{
#ifdef VERBOSE
    printf("TraversalDelete [%s] -> [(ignore)] (type %s)\n",
           Tcl_DStringValue(srcPtr),
           type == DOTREE_PRED ? "DOTREE_PRED"
                   : (type == DOTREE_POSTD ? "DOTREE_POSTD"
                              : (type == DOTREE_F ? "DOTREE_F"
                                         : "???")));
    fflush(stdout);
#endif
    switch (type) {
	case DOTREE_F:
	    if (DoDeleteFile(srcPtr) == TCL_OK) {
		return TCL_OK;
	    }
	    break;

	case DOTREE_PRED:
	    return TCL_OK;

	case DOTREE_POSTD:
	    if (DoRemoveDirectory(srcPtr, 0, NULL) == TCL_OK) {
		return TCL_OK;
	    }
	    break;

    }

    if (errorPtr != NULL) {
	Tcl_ExternalToUtfDString(NULL, Tcl_DStringValue(srcPtr),
                                 Tcl_DStringLength(srcPtr), errorPtr);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * StatError --
 *
 *      Sets the object result with the appropriate error.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The interp's object result is set with an error message
 *      based on the objIndex, fileName and errno.
 *
 *----------------------------------------------------------------------
 */

static void
StatError(
    Tcl_Interp *interp,         /* The interp that has the error */
    CONST char *fileName)       /* The name of the file which caused the
                                 * error. */
{
    TclOS2ConvertError(rc);
    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
            "could not read \"", fileName, "\": ", Tcl_PosixError(interp),
            (char *) NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * GetOS2FileAttributes --
 *
 *      Returns a Tcl_Obj containing the value of a file attribute.
 *      This routine gets the -hidden, -readonly or -system attribute.
 *
 * Results:
 *      Standard Tcl result and a Tcl_Obj in attributePtrPtr. The object
 *      will have ref count 0. If the return value is not TCL_OK,
 *      attributePtrPtr is not touched.
 *
 * Side effects:
 *      A new object is allocated if the file is valid.
 *
 *----------------------------------------------------------------------
 */

static int
GetOS2FileAttributes(
    Tcl_Interp *interp,             /* The interp we are using for errors. */
    int objIndex,                   /* The index of the attribute. */
    CONST char *fileName,           /* The name of the file (UTF-8). */
    Tcl_Obj **attributePtrPtr)      /* A pointer to return the object with. */
{
    FILESTATUS3 fileStatus;
    Tcl_DString nativeName;

    Tcl_DStringInit(&nativeName);
    Tcl_UtfToExternalDString(NULL, fileName, -1, &nativeName);
    rc = DosQueryPathInfo(Tcl_DStringValue(&nativeName), FIL_STANDARD,
                          &fileStatus, sizeof(FILESTATUS3));
#ifdef VERBOSE
    printf("GetOS2FileAttributes [%s] returns %d\n", nativeName, rc);
#endif
    if (rc != NO_ERROR) {
        StatError(interp, Tcl_DStringValue(&nativeName));
        return TCL_ERROR;
    }
    Tcl_DStringFree(&nativeName);

    *attributePtrPtr = Tcl_NewBooleanObj(fileStatus.attrFile
                                         & attributeArray[objIndex]);
    return TCL_OK;
}

#if 0
/*
 *----------------------------------------------------------------------
 *
 * ConvertFileNameFormat --
 *
 *      Returns a Tcl_Obj containing either the long or short version of the
 *      file name.
 *
 * Results:
 *      Standard Tcl result and a Tcl_Obj in attributePtrPtr. The object
 *      will have ref count 0. If the return value is not TCL_OK,
 *      attributePtrPtr is not touched.
 *
 * Side effects:
 *      A new object is allocated if the file is valid.
 *
 *----------------------------------------------------------------------
 */

static int
ConvertFileNameFormat(
    Tcl_Interp *interp,             /* The interp we are using for errors. */
    int objIndex,                   /* The index of the attribute. */
    char *fileName,                 /* The name of the file. */
    int longShort,                  /* 0 to short name, 1 to long name. */
    Tcl_Obj **attributePtrPtr)      /* A pointer to return the object with. */
{
    HDIR findHandle;
    FILEFINDBUF3 findData;
    ULONG filesAtATime = 1;
    int pathArgc, i;
    char **pathArgv, **newPathArgv;
    char *currentElement, *resultStr;
    Tcl_DString resultDString;
    int result = TCL_OK;

    Tcl_SplitPath(fileName, &pathArgc, &pathArgv);
    newPathArgv = (char **) ckalloc(pathArgc * sizeof(char *));

    i = 0;
    if ((pathArgv[0][0] == '/')
            || ((strlen(pathArgv[0]) == 3) && (pathArgv[0][1] == ':'))) {
        newPathArgv[0] = (char *) ckalloc(strlen(pathArgv[0]) + 1);
        strcpy(newPathArgv[0], pathArgv[0]);
        i = 1;
    }
    for ( ; i < pathArgc; i++) {
        if (strcmp(pathArgv[i], ".") == 0) {
            currentElement = ckalloc(2);
            strcpy(currentElement, ".");
        } else if (strcmp(pathArgv[i], "..") == 0) {
            currentElement = ckalloc(3);
            strcpy(currentElement, "..");
        } else {
            char *str;
            CONST char *nativeName;
            int useLong;

            Tcl_DStringInit(&resultDString);
            str = Tcl_JoinPath(i + 1, pathArgv, &resultDString);
            Tcl_UtfToExternalDString(NULL, str, -1, &nativeName);
            /* Use a new handle since we don't know if another find is active */
            findHandle = HDIR_CREATE;
            rc = DosFindFirst(resultStr, &findHandle,
                              FILE_NORMAL | FILE_DIRECTORY,
                              &findData, sizeof(findData), &filesAtATime,
                              FIL_STANDARD);
            if (rc != NO_ERROR && rc != ERROR_NO_MORE_FILES) {
                pathArgc = i - 1;
                StatError(interp, fileName);
                result = TCL_ERROR;
                Tcl_DStringFree(&resultDString);
                goto cleanup;
            }
            /*
             * If rc == ERROR_NO_MORE_FILES, we might have a case where we're
             * trying to find a long-name file on a short-name File System
             * such as DOS, where the long name is kept in the .LONGNAME
             * extended attribute by the WPS and EA-aware applications.
             * This is the only thing comparable to the Windows 95/NT long
             * name-to-alternate name mapping.
             * In that case, retrieve/set the long name from/in the EA.
             */
            if (longShort) {
                if (findData.achName[0] != '\0') {
                    useLong = 1;
                } else {
                    useLong = 0;
                }
            } else {
                if (findData.cAlternateFileName[0] == '\0') {
                    useLong = 1;
                } else {
                    useLong = 0;
                }
            }
            if (useLong) {
                currentElement = ckalloc(strlen(findData.achName) + 1);
                strcpy(currentElement, findData.achName);
            } else {
                currentElement = ckalloc(strlen(findData.cAlternateFileName)
                        + 1);
                strcpy(currentElement, findData.cAlternateFileName);
            }
            Tcl_DStringFree(&resultDString);
            FindClose(findHandle);
        }
        newPathArgv[i] = currentElement;
    }

    Tcl_DStringInit(&resultDString);
    resultStr = Tcl_JoinPath(pathArgc, newPathArgv, &resultDString);
    *attributePtrPtr = Tcl_NewStringObj(resultStr,
                                        Tcl_DStringLength(&resultDString));
    Tcl_DStringFree(&resultDString);

cleanup:
    for (i = 0; i < pathArgc; i++) {
        ckfree(newPathArgv[i]);
    }
    ckfree((char *) newPathArgv);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * GetOS2FileLongName --
 *
 *      Returns a Tcl_Obj containing the long version of the file name.
 *
 * Results:
 *      Standard Tcl result and a Tcl_Obj in attributePtrPtr. The object
 *      will have ref count 0. If the return value is not TCL_OK,
 *      attributePtrPtr is not touched.
 *
 * Side effects:
 *      A new object is allocated if the file is valid.
 *
 *----------------------------------------------------------------------
 */

static int
GetOS2FileLongName(
    Tcl_Interp *interp,             /* The interp we are using for errors. */
    int objIndex,                   /* The index of the attribute. */
    char *fileName,                 /* The name of the file. */
    Tcl_Obj **attributePtrPtr)      /* A pointer to return the object with. */
{
    return ConvertFileNameFormat(interp, objIndex, fileName, 1,
                                 attributePtrPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GetOS2FileShortName --
 *
 *      Returns a Tcl_Obj containing the short version of the file
 *      name.
 *
 * Results:
 *      Standard Tcl result and a Tcl_Obj in attributePtrPtr. The object
 *      will have ref count 0. If the return value is not TCL_OK,
 *      attributePtrPtr is not touched.
 *
 * Side effects:
 *      A new object is allocated if the file is valid.
 *
 *----------------------------------------------------------------------
 */

static int
GetOS2FileShortName(
    Tcl_Interp *interp,             /* The interp we are using for errors. */
    int objIndex,                   /* The index of the attribute. */
    char *fileName,                 /* The name of the file. */
    Tcl_Obj **attributePtrPtr)      /* A pointer to return the object with. */
{
    return ConvertFileNameFormat(interp, objIndex, fileName, 0,
                                 attributePtrPtr);
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * SetOS2FileAttributes --
 *
 *      Set the file attributes to the value given by attributePtr.
 *      This routine sets the -hidden, -readonly, or -system attributes.
 *
 * Results:
 *      Standard TCL error.
 *
 * Side effects:
 *      The file's attribute is set.
 *
 *----------------------------------------------------------------------
 */

static int
SetOS2FileAttributes(
    Tcl_Interp *interp,             /* The interp we are using for errors. */
    int objIndex,                   /* The index of the attribute. */
    CONST char *fileName,           /* The name of the file (UTF-8). */
    Tcl_Obj *attributePtr)          /* The new value of the attribute. */
{
    FILESTATUS3 fileStatus;
    CONST char *native;
    Tcl_DString ds;
    int yesNo;
    int result;

    native = Tcl_UtfToExternalDString(NULL, fileName, -1, &ds);
    rc = DosQueryPathInfo(native, FIL_STANDARD, &fileStatus,
                          sizeof(FILESTATUS3));
    if (rc != NO_ERROR) {
        StatError(interp, fileName);
        return TCL_ERROR;
    }

    result = Tcl_GetBooleanFromObj(interp, attributePtr, &yesNo);
    if (result != TCL_OK) {
        return result;
    }

    if (yesNo) {
        fileStatus.attrFile |= (attributeArray[objIndex]);
    } else {
        fileStatus.attrFile &= ~(attributeArray[objIndex]);
    }

    rc = DosSetPathInfo(native, FIL_STANDARD, &fileStatus,
                        sizeof(FILESTATUS3), (ULONG)0);
    if (rc != NO_ERROR) {
        StatError(interp, fileName);
        return TCL_ERROR;
    }
    return TCL_OK;
}

#if 0
/*
 *----------------------------------------------------------------------
 *
 * CannotGetAttribute --
 *
 *      The attribute in question cannot be gotten.
 *
 * Results:
 *      TCL_ERROR
 *
 * Side effects:
 *      The object result is set to a pertinent error message.
 *
 *----------------------------------------------------------------------
 */

static int
CannotGetAttribute(
    Tcl_Interp *interp,             /* The interp we are using for errors. */
    int objIndex,                   /* The index of the attribute. */
    char *fileName,                 /* The name of the file. */
    Tcl_Obj **attributePtr)         /* The value of the attribute. */
{
    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
            "cannot get attribute \"", tclpFileAttrStrings[objIndex],
            "\" for file \"", fileName, "\" : attribute is unavailable",
            (char *) NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * CannotSetAttribute --
 *
 *      The attribute in question cannot be set.
 *
 * Results:
 *      TCL_ERROR
 *
 * Side effects:
 *      The object result is set to a pertinent error message.
 *
 *----------------------------------------------------------------------
 */

static int
CannotSetAttribute(
    Tcl_Interp *interp,             /* The interp we are using for errors. */
    int objIndex,                   /* The index of the attribute. */
    char *fileName,                 /* The name of the file. */
    Tcl_Obj *attributePtr)          /* The new value of the attribute. */
{
    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
            "cannot set attribute \"", tclpFileAttrStrings[objIndex],
            "\" for file \"", fileName, "\" : attribute is unavailable",
            (char *) NULL);
    return TCL_ERROR;
}

#endif
/*
 *---------------------------------------------------------------------------
 *
 * TclpListVolumes --
 *
 *      Lists the currently mounted volumes
 *
 * Results:
 *      A standard Tcl result.  Will always be TCL_OK, since there is no way
 *      that this command can fail.  Also, the interpreter's result is set to
 *      the list of volumes.
 *
 * Side effects:
 *      None
 *
 *---------------------------------------------------------------------------
 */

int
TclpListVolumes(
    Tcl_Interp *interp)    /* Interpreter to which to pass the volume list */
{
    Tcl_Obj *resultPtr, *elemPtr;
    char buf[4 * MAXDRIVES];
    int i;
    char *p;
    ULONG curDiskNum, volBitmap;
    FSINFO infoBuf;

    resultPtr = Tcl_GetObjResult(interp);

    /* Only query logical drives that exist */
    rc = DosQueryCurrentDisk(&curDiskNum, &volBitmap);

    buf[1] = ':';
    buf[2] = '/';
    buf[3] = '\0';

    for (i = 0, p = buf; i < 26 && *p != '\0'; i++, p += 4) {
        if ((volBitmap << (31-i)) >> 31) {
            buf[0] = (char) ('a' + i);
            rc = DosQueryFSInfo(i+1, FSIL_VOLSER, &infoBuf, sizeof(infoBuf));
            if ( rc == NO_ERROR || rc == ERROR_NOT_READY) {
                elemPtr = Tcl_NewStringObj(buf, -1);
                Tcl_ListObjAppendElement(NULL, resultPtr, elemPtr);
            }
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CopyFileAtts
 *
 *      Copy the file attributes such as owner, group, permissions, and
 *      modification date from one file to another.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      user id, group id, permission bits, last modification time, and
 *      last access time are updated in the new file to reflect the old
 *      file.
 *
 *----------------------------------------------------------------------
 */

int
CopyFileAtts (
    char *src,		/* Path name of source file (native) */
    char *dst,		/* Path name of target file (native) */
    FILESTATUS3 *fsSource)	/* File status of source file */
{
    rc = DosSetPathInfo(dst, FIL_STANDARD, fsSource, sizeof (*fsSource), 0L);
    if (rc != NO_ERROR) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/* 
 * tclOS2Reg.c --
 *
 *	This file contains the implementation of the "registry" Tcl
 *	built-in command.  This command is built as a dynamically
 *	loadable extension in a separate DLL.
 *
 * Copyright (c) 1997 by Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 by Scriptics Corporation.
 * Copyright (c) 1999-2002 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tclOS2Int.h"

/*
 * The following tables contain the mapping from registry root names
 * to the system predefined keys.
 */

static char *iniFileNames[] = {
    "BOTH", "USER", "SYSTEM", NULL
};

static HINI iniHandles[] = {
    HINI_PROFILE, HINI_USERPROFILE, HINI_SYSTEMPROFILE, NULLHANDLE
};

#define USERPROFILE	0
#define SYSTEMPROFILE	1

/* Array for paths of profiles at time of loading. */
static char userIniPath[CCHMAXPATH+1];
static char sysIniPath[CCHMAXPATH+1];

/*
 * The following gives the possible types to write keys as and mappings from
 * the possible type argument to the registry command.
 */

#define BINARY	0
#define LONG	1
#define STRING	2
#define SZ	3
#define	DWORD	4
#define MAXTYPE	DWORD

static char *typeNames[] = {
    "binary", "long", "string",
    "sz", "dword",  /* for Windows compatibility */
    NULL
};

static ULONG ret;


/*
 * Declarations for functions defined in this file.
 */

static void		AppendSystemError(Tcl_Interp *interp, ULONG error);
static int		RegistryObjCmd(ClientData clientData,
                	    Tcl_Interp *interp, int objc,
                            Tcl_Obj * CONST objv[]);

/* Windows compatible functions */
static int		TclOS2RegDelete(Tcl_Interp *interp, Tcl_Obj *keyNameObj,
			    Tcl_Obj *valueNameObj);
static int		TclOS2GetValue(Tcl_Interp *interp, Tcl_Obj *keyNameObj,
			    Tcl_Obj *valueNameObj, Tcl_Obj *typeObj);
static int		TclOS2GetKeyNames(Tcl_Interp *interp,
			    Tcl_Obj *keyNameObj, Tcl_Obj *patternObj);
static int		TclOS2SetKey(Tcl_Interp *interp, Tcl_Obj *keyNameObj,
			    Tcl_Obj *valueNameObj, Tcl_Obj *dataObj,
			    Tcl_Obj *typeObj);

/* Utility functions */
static int		TclOS2OpenProfile(Tcl_Interp *interp, char *name,
                	    char **iniFilePtr, char **keyNamePtr,
                	    HINI *iniHandlePtr);
static int		TclOS2CloseProfile(HINI iniHandle);
static int              QueryAppNames(HINI iniHandle, char **buffer,
                            ULONG *length);
static int              QueryKeyNames(HINI iniHandle, char *appName,
                            char **buffer, ULONG *length);

/* OS/2 specific functionality */
static int              TclOS2GetAppNames(Tcl_Interp *interp,
                            Tcl_Obj *iniFileObj, Tcl_Obj *patternObj);
static int              TclOS2GetAppKeyNames(Tcl_Interp *interp,
                            Tcl_Obj *appNameObj, Tcl_Obj *patternObj);
static int              TclOS2SetAppKey(Tcl_Interp *interp, Tcl_Obj *appNameObj,
                            Tcl_Obj *keyNameObj, Tcl_Obj *dataObj,
                            Tcl_Obj *typeObj);

int	Registry_Init(Tcl_Interp *interp);

/*
 *----------------------------------------------------------------------
 *
 * Registry_Init --
 *
 *	This procedure initializes the registry command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Registry_Init(
    Tcl_Interp *interp)
{
    PRFPROFILE prfProfile;

#ifdef VERBOSE
    printf("Registry_Init\n");
    fflush(stdout);
#endif
    if (!Tcl_InitStubs(interp, "8.0", 0)) {
        return TCL_ERROR;
    }

    /*
     * Store paths of profiles into their array.
     * Since the info isn't used (yet), we won't consider failure a
     * fatal error.
     */

    prfProfile.pszUserName = userIniPath;
    prfProfile.cchUserName = sizeof(userIniPath);
    userIniPath[prfProfile.cchUserName-1] = '\0';
    prfProfile.pszSysName = sysIniPath;
    prfProfile.cchSysName = sizeof(sysIniPath);
    sysIniPath[prfProfile.cchSysName-1] = '\0';

#ifdef VERBOSE
    printf("TclOS2GetHAB(): %x\n", TclOS2GetHAB());
#endif
    if (PrfQueryProfile(TclOS2GetHAB(), &prfProfile) == TRUE) {
#ifdef VERBOSE
        printf("User Profile [%s] (%d)\nSystem Profile [%s] (%d)\n",
               prfProfile.pszUserName, prfProfile.cchUserName,
               prfProfile.pszSysName, prfProfile.cchSysName);
#endif
    } else {
#ifdef VERBOSE
        printf("PrfQueryProfile ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
#endif
        userIniPath[0] = '\0';
        sysIniPath[0] = '\0';
    }

    Tcl_CreateObjCommand(interp, "profile", RegistryObjCmd, NULL, NULL);
    Tcl_PkgProvide(interp, "profile", "1.0");
    Tcl_CreateObjCommand(interp, "ini", RegistryObjCmd, NULL, NULL);
    Tcl_PkgProvide(interp, "ini", "1.0");
    Tcl_CreateObjCommand(interp, "registry", RegistryObjCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "registry", "1.0");
}

/*
 *----------------------------------------------------------------------
 *
 * RegistryObjCmd --
 *
 *	This function implements the Tcl "registry" command, also known
 *	as "profile" and "ini" in the OS/2 version.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
RegistryObjCmd(
    ClientData clientData,	/* Not used. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj * CONST objv[])	/* Argument values. */
{
    int index;
    char *errString = NULL;

    static char *subcommands[] = { "delete", "get", "keys", "set", "type",
                                   "values",
                                   "apps", "appkeys", "appset",
                                   (char *) NULL };
    enum SubCmdIdx { DeleteIdx, GetIdx, KeysIdx, SetIdx, TypeIdx, ValuesIdx,
                     AppsIdx, AppKeysIdx, AppSetIdx };
#ifdef VERBOSE
    printf("RegistryObjCmd()\n");
    fflush(stdout);
#endif

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, objc, objv, "option ?arg arg ...?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], subcommands, "option", 0, &index)
            != TCL_OK) {
        return TCL_ERROR;
    }

    switch (index) {
        case DeleteIdx:			/* delete */
            if (objc == 3) {
                return TclOS2RegDelete(interp, objv[2], NULL);
            } else if (objc == 4) {
                return TclOS2RegDelete(interp, objv[2], objv[3]);
            }
            errString = "keyName ?valueName?";
            break;
        case GetIdx:			/* get */
            if (objc == 4) {
                return TclOS2GetValue(interp, objv[2], objv[3], NULL);
            } else if (objc == 5) {
                return TclOS2GetValue(interp, objv[2], objv[3], objv[4]);
            }
            errString = "keyName valueName ?asType?";
            break;
        case KeysIdx:			/* keys */
            if (objc == 3) {
                return TclOS2GetKeyNames(interp, objv[2], NULL);
            } else if (objc == 4) {
                return TclOS2GetKeyNames(interp, objv[2], objv[3]);
            }
            errString = "keyName ?pattern?";
            break;
        case SetIdx:			/* set */
            if (objc == 3) {
                /* Only the application isn't possible but will not complain */
                return TclOS2SetKey(interp, objv[2], NULL, NULL, NULL);
            } else if (objc == 5 || objc == 6) {
                Tcl_Obj *typeObj = (objc == 5) ? NULL : objv[5];
                return TclOS2SetKey(interp, objv[2], objv[3], objv[4], typeObj);
            }
            errString = "keyName ?valueName data ?type??";
            break;
        case ValuesIdx:                 /* values */
            if (objc == 3 || objc == 4) {
                return TclOS2GetValue(interp, objv[2], objv[3], NULL);
            }
            errString = "keyName ?pattern?";
            break;
        case AppsIdx:			/* apps */
            if (objc == 3) {
                return TclOS2GetAppNames(interp, objv[2], NULL);
            } else if (objc == 4) {
                return TclOS2GetAppNames(interp, objv[2], objv[3]);
            }
            errString = "iniFile ?pattern?";
            break;
        case AppKeysIdx:		/* appkeys */
            if (objc == 3) {
                return TclOS2GetAppKeyNames(interp, objv[2], NULL);
            } else if (objc == 4) {
                return TclOS2GetAppKeyNames(interp, objv[2], objv[3]);
            }
            errString = "iniFile\\appName ?pattern?";
            break;
        case AppSetIdx:			/* appset */
            if (objc == 3) {
                /* Only the application */
                return TclOS2SetAppKey(interp, objv[2], NULL, NULL, NULL);
            } else if (objc == 5 || objc == 6) {
                Tcl_Obj *typeObj = (objc == 5) ? NULL : objv[5];
                return TclOS2SetAppKey(interp, objv[2], objv[3], objv[4],
                                       typeObj);
            }
            errString = "appName ?keyName data ?type??";
            break;
    }
    Tcl_WrongNumArgs(interp, 2, objv, errString);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2RegDelete --
 *
 *	This function deletes an application or key.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2RegDelete(
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Obj *keyNameObj,	/* Name of app to delete. */
    Tcl_Obj *valueNameObj)	/* Name of key to delete. */
{
    char *buffer, *iniFile, *appName, *valueName;
    HINI iniHandle;
    int length;
    Tcl_Obj *resultPtr;
    Tcl_DString ds;

    appName = Tcl_GetStringFromObj(keyNameObj, &length);
    appName = Tcl_UtfToExternalDString(NULL, appName, -1, &ds);
    buffer = ckalloc((unsigned int) length + 1);
    strcpy(buffer, appName);
    valueName = (valueNameObj != NULL)
                              ? Tcl_GetStringFromObj(valueNameObj, &length)
                              : NULL;
    Tcl_UtfToExternalDString(NULL, valueName, -1, &ds);
#ifdef VERBOSE
    printf("TclOS2RegDelete(%s, %s)\n", Tcl_DStringValue(&ds), valueName);
    fflush(stdout);
#endif

    if (TclOS2OpenProfile(interp, buffer, &iniFile, &appName, &iniHandle)
            != TCL_OK) {
        ckfree(buffer);
        return TCL_ERROR;
    }
    ckfree(buffer);

    resultPtr = Tcl_GetObjResult(interp);
    if (valueName != NULL && *valueName == '\0') {
        Tcl_AppendToObj(resultPtr, "bad key: cannot delete null keys", -1);
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    /* Deleting application is done by passing NULL pszKey value */
    ret = PrfWriteProfileData(iniHandle, appName, (PSZ)Tcl_DStringValue(&ds),
                              (PVOID)NULL, 0);
#ifdef VERBOSE
    printf("PrfWriteProfileData(%x, %s, %s, NULL, 0) returns %d\n", iniHandle,
           appName, Tcl_DStringValue(&ds), ret);
    fflush(stdout);
#endif
    Tcl_DStringFree(&ds);
    if (ret != TRUE) {
        Tcl_AppendStringsToObj(resultPtr, "unable to delete key \"",
            Tcl_GetStringFromObj(valueNameObj, NULL), "\" from application \"",
            Tcl_GetStringFromObj(keyNameObj, NULL), "\": ", NULL);
        AppendSystemError(interp, WinGetLastError(TclOS2GetHAB()));
        return TCL_ERROR;
    }

    TclOS2CloseProfile(iniHandle);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetValue --
 *
 *	This function querys the profile for the value of a key.
 *
 * Results:
 *	A standard Tcl result.
 *	Returns the list of applications in the result object of the
 *	interpreter, or an error message on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2GetValue(
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Obj *keyNameObj,	/* Name of app. */
    Tcl_Obj *valueNameObj,	/* Name of key to query. */
    Tcl_Obj *typeObj)		/* Type of data to be written. */
{
    char *iniFile, *appName, *valueName, *path;
    HINI iniHandle;
    ULONG length;
    Tcl_Obj *resultPtr;
    Tcl_DString data, buf, ds;

/* IMPLEMENTATION STILL IGNORES TYPE */
    path = Tcl_GetStringFromObj(keyNameObj, (int *) &length);
    path = Tcl_UtfToExternalDString(NULL, path, -1, &ds);
    valueName = Tcl_GetStringFromObj(valueNameObj, (int *) &length);
    valueName = Tcl_UtfToExternalDString(NULL, valueName, (int) length, &buf);
#ifdef VERBOSE
    printf("TclOS2RegGetValue(%s, %s)\n", appName, valueName);
    fflush(stdout);
#endif

    if (TclOS2OpenProfile(interp, path, &iniFile, &appName, &iniHandle)
            != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Initialize a Dstring to maximum statically allocated size
     * we could get one more byte by avoiding Tcl_DStringSetLength()
     * and just setting maxBuf to TCL_DSTRING_STATIC_SIZE, but this
     * should be safer if the implementation of Dstrings changes.
     */

    Tcl_DStringInit(&data);
    ret = PrfQueryProfileSize(iniHandle, appName, valueName, &length);
    if (ret != TRUE) {
        length = TCL_DSTRING_STATIC_SIZE - 1;
    }
    Tcl_DStringSetLength(&data, (int) length);

    resultPtr = Tcl_GetObjResult(interp);

    ret = PrfQueryProfileData(iniHandle, appName, valueName,
                             (PVOID) Tcl_DStringValue(&data), &length);
#ifdef VERBOSE
    printf("PrfQueryProfileData(%x, %s, %s, <>, %d) returns %d\n", iniHandle,
           appName, valueName, length, ret);
    printf("   WinGetLastError %x, length now %d\n",
           WinGetLastError(TclOS2GetHAB()), length);
    fflush(stdout);
#endif
    Tcl_DStringFree(&buf); /* valueName invalid */
    Tcl_DStringFree(&ds); /* appName invalid */
    TclOS2CloseProfile(iniHandle);
    if (ret != TRUE) {
        Tcl_AppendStringsToObj(resultPtr, "unable to get key \"",
            Tcl_GetString(valueNameObj), "\" from application \"",
            Tcl_GetString(keyNameObj), "\": ", NULL);
        AppendSystemError(interp, WinGetLastError(TclOS2GetHAB()));
        Tcl_DStringFree(&data);
        return TCL_ERROR;
    }

    /*
     * OS/2 Profile data has no inherent type, only how applications wish to
     * view them. Therefore, store it as a binary string.
     */

    Tcl_SetByteArrayObj(resultPtr, Tcl_DStringValue(&data), (int) length);
    Tcl_DStringFree(&data);
    return (ret == TRUE) ? TCL_OK : TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetKeyNames --
 *
 *	This function enumerates the keys in a profile.
 *	If the optional pattern is supplied, then only key
 *	names that match the pattern will be returned.
 *
 * Results:
 *	Returns the list of key names in the result object of the
 *	interpreter, or an error message on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2GetKeyNames(
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Obj *keyNameObj,	/* Key to enumerate. */
    Tcl_Obj *patternObj)	/* Optional match pattern. */
{
    char *p, *iniFile, *appName, *path;
    char *apps;
    char *keys;
    char *fullName, *name;
    ULONG appsLength, keysLength;
    HINI iniHandle;
    int length, len2 = 0;
    Tcl_Obj *resultPtr;
    int result = TCL_OK;
    char *pattern;
    Tcl_DString ds;

    path = Tcl_GetStringFromObj(keyNameObj, &length);
    path = Tcl_UtfToExternalDString(NULL, path, -1, &ds);
#ifdef VERBOSE
    printf("TclOS2GetKeyNames, path [%s]\n", path);
    fflush(stdout);
#endif

    if (TclOS2OpenProfile(interp, path, &iniFile, &appName, &iniHandle)
            != TCL_OK) {
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    /*
     * If the appName now is the empty string, that means we have to
     * enumerate ALL the applications and their keys.
     */

    if ( strcmp(appName, "") == 0 ) {
        /* Determine applications */
        if (QueryAppNames(iniHandle, &apps, &appsLength) == TCL_ERROR) {
            TclOS2CloseProfile(iniHandle);
            Tcl_DStringFree(&ds);
            return TCL_ERROR;
        }
        /*
         * apps now contains the names of the applications, separated by NULL
         * characters; the last is terminated with two successive NULLs.
         * appsLength now contains the total length of the list in apps
         * excluding the final NULL character.
         */
#ifdef VERBOSE
        printf("    PrfQueryProfileData returns %d in apps (first %s)\n",
               appsLength, apps);
        fflush(stdout);
#endif

    } else {
        /* Put single appname with second NULL character behind it in apps */
        apps = ckalloc(strlen(appName) + 2);
        if (apps == NULL) {
#ifdef VERBOSE
            printf("    can't alloc %d for single app %s", appsLength, appName);
            fflush(stdout);
#endif
            Tcl_DStringFree(&ds);
            TclOS2CloseProfile(iniHandle);
            return TCL_ERROR;
        }
        strcpy(apps, appName);
        p = apps + strlen(appName) + 1;
        *p = '\0';
        appsLength = strlen(p) + 1;
    }
    Tcl_DStringFree(&ds);

    /* for appName in list of applications */
    for (appName = apps; *appName != '\0'; appName += strlen(appName)+1) {
        /* Determine keys */
        if (QueryKeyNames(iniHandle, appName, &keys, &keysLength)== TCL_ERROR) {
            TclOS2CloseProfile(iniHandle);
            return TCL_ERROR;
        }

        /*
         * keys now contains the names of the keys, separated by NULL
         * characters; the last is terminated with two successive NULLs.
         * keysLength now contains the total length of the list in keys
         * excluding the final NULL character.
         */
#ifdef VERBOSE
        printf("    PrfQueryProfileData returns %d in buffer (first %s)\n",
               keysLength, keys);
        fflush(stdout);
#endif

        if (patternObj) {
            pattern = Tcl_GetString(patternObj);
        } else {
            pattern = NULL;
        }

        /*
         * Enumerate over the keys until we get to the double NULL, indicating
         * the end of the list.
         */

        resultPtr = Tcl_GetObjResult(interp);
        length = strlen(appName);
        for (p = keys; *p != '\0'; p += len2+1) {
            len2 = strlen(p);
#ifdef VERBOSE
            printf("    appName [%s] len %d, p [%s] len %d\n", appName, length,
                   p, len2);
            fflush(stdout);
#endif
            fullName = ckalloc(length + 1 + len2 + 1);
            if (keys == NULL) {
#ifdef VERBOSE
                printf("    can't alloc %d for fullName [%s\\%s]", keysLength,
                       appName, p);
                fflush(stdout);
#endif
                continue;
            }
            strcpy(fullName, appName);
            strcat(fullName, "\\");
            strcat(fullName, p);
            Tcl_ExternalToUtfDString(NULL, fullName, length + len2 + 2, &ds);
            name = Tcl_DStringValue(&ds);
            if (!pattern || Tcl_StringMatch(name, pattern)) {
                result = Tcl_ListObjAppendElement(interp, resultPtr,
                        Tcl_NewStringObj(name, Tcl_DStringLength(&ds)));
                if (result != TCL_OK) {
                    Tcl_DStringFree(&ds);
                    break;
                }
#ifdef VERBOSE
                printf("    Adding %s\n", fullName);
                fflush(stdout);
#endif
            }
            Tcl_DStringFree(&ds);
        }

        ckfree(keys);
    }
    Tcl_DStringFree(&ds);
    ckfree(apps);

    TclOS2CloseProfile(iniHandle);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2SetKey --
 *
 *	This function sets the contents of a profile value.  If
 *	the application or key does not exist, it will be created.  If it
 *	does exist, then the data will be replaced.
 *	Only writing as binary data and string is possible.
 *
 * Results:
 *	Returns a normal Tcl result.
 *
 * Side effects:
 *	May create new apps or keys.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2SetKey(
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Obj *keyNameObj,	/* Name of application. */
    Tcl_Obj *valueNameObj,	/* Name of value to set. */
    Tcl_Obj *dataObj,		/* Data to be written. */
    Tcl_Obj *typeObj)		/* Type of data to be written. */
{
    char *buffer, *iniFile, *appName, *valueName;
    ULONG type;
    HINI iniHandle;
    int length;
    Tcl_Obj *resultPtr;
    Tcl_DString ds, nameBuf;
#ifdef VERBOSE
    printf("TclOS2SetKey()\n");
    fflush(stdout);
#endif

    appName = Tcl_GetStringFromObj(keyNameObj, &length);
    appName = Tcl_UtfToExternalDString(NULL, appName, -1, &ds);
    buffer = ckalloc(length + 1);
    strcpy(buffer, appName);
    valueName = valueNameObj != NULL
                             ? Tcl_GetStringFromObj(valueNameObj, &length)
                             : "";
#ifdef VERBOSE
    printf("TclOS2SetKey(%s, [%s])\n", appName, valueName);
    fflush(stdout);
#endif

    /*
     * CAREFUL: NULL keyName will delete complete application from profile
     * Require TclOS2RegDelete to be used for that, so don't allow NULL.
     */
    if (valueName == NULL) {
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    if (typeObj == NULL) {
        type = STRING;
    } else if (Tcl_GetIndexFromObj(interp, typeObj, typeNames, "type",
            0, (int *) &type) != TCL_OK) {
        if (Tcl_GetIntFromObj(NULL, typeObj, (int*) &type) != TCL_OK) {
            Tcl_DStringFree(&ds);
            return TCL_ERROR;
        }
        Tcl_ResetResult(interp);
    }

    if (TclOS2OpenProfile(interp, buffer, &iniFile, &appName, &iniHandle)
            != TCL_OK) {
        ckfree(buffer);
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }
    ckfree(buffer);

    valueName = Tcl_UtfToExternalDString(NULL, valueName, length, &nameBuf);
    resultPtr = Tcl_GetObjResult(interp);

    if (type == STRING || type == SZ) {
        Tcl_DString buf;
        char *data = dataObj != NULL ? Tcl_GetStringFromObj(dataObj, &length)
                                     : NULL;
        data = Tcl_UtfToExternalDString(NULL, data, length, &buf);

        /*
         * Include the null in the length.
         */
        length = Tcl_DStringLength(&buf) + 1;

        ret = PrfWriteProfileData(iniHandle, appName, (PSZ)valueName,
                                  (PVOID)data, length);
#ifdef VERBOSE
        printf("PrfWriteProfileData(%x, %s, %s, <data>, %d) returns %d\n",
               iniHandle, appName, valueName, length, ret);
        fflush(stdout);
#endif
        Tcl_DStringFree(&buf);
    } else {
        char *data;

        /*
         * Store binary data in the registry.
         */

        data = Tcl_GetByteArrayFromObj(dataObj, &length);

        ret = PrfWriteProfileData(iniHandle, appName, (PSZ)valueName,
                                 (PVOID)&data, (ULONG)length);
#ifdef VERBOSE
        printf("PrfWriteProfileData(%x, %s, %s, [%x], %d) returns %d\n",
               iniHandle, appName, valueName, data, length, ret);
        fflush(stdout);
#endif
    }
    Tcl_DStringFree(&nameBuf);
    Tcl_DStringFree(&ds);
    TclOS2CloseProfile(iniHandle);
    if (ret != TRUE) {
        Tcl_AppendToObj(resultPtr, "unable to set value: ", -1);
        AppendSystemError(interp, WinGetLastError(TclOS2GetHAB()));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2OpenProfile --
 *
 *	This function parses a key name into the iniFile, application
 *	and key parts and if necessary opens the iniFile. 
 *
 * Results:
 *	The pointers to the start of the iniFile, application and key
 *	names are returned in the iniFilePtr, appNamePtr and valueNamePtr
 *	variables.
 *	The handle for the opened profile is returned in iniFilePtr.
 *	In the case of using both user and system profiles, the full
 *	of the user or system profiles are returned in iniFilePtr
 *	separated by '\0'.
 *	Returns a standard Tcl result.
 *
 *
 * Side effects:
 *	Modifies the name string by inserting nulls.
 *	Opens any user specified profile.
 *	A not-yet existing profile will be created empty by OS/2.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2OpenProfile(
    Tcl_Interp *interp,		/* Current interpreter. */
    char *name,
    char **iniFilePtr,
    char **appNamePtr,
    HINI *iniHandlePtr)
{
    char *rootName;
    int result, index;
    Tcl_Obj *rootObj, *resultPtr = Tcl_GetObjResult(interp);
#ifdef VERBOSE
    printf("TclOS2OpenProfile()\n");
    fflush(stdout);
#endif

    /*
     * Split the key into host and root portions.
     */

    *iniFilePtr = *appNamePtr = NULL;
    *iniHandlePtr = HINI_PROFILE;
    rootName = name;

    /*
     * Split into iniFile and application portions.
     */

    for (*appNamePtr = rootName; **appNamePtr != '\0'; (*appNamePtr)++) {
        if (**appNamePtr == '\\') {
            **appNamePtr = '\0';
            (*appNamePtr)++;
            break;
        }
    }

    /*
     * Look for a matching root name.
     */

#ifdef VERBOSE
    printf("    rootName %s\n", rootName);
    fflush(stdout);
#endif
    rootObj = Tcl_NewStringObj(rootName, -1);
    result = Tcl_GetIndexFromObj(NULL, rootObj, iniFileNames, "root name",
                                 TCL_EXACT, &index);
    Tcl_DecrRefCount(rootObj);
    if (result != TCL_OK) {
        /* Not BOTH, USER or SYSTEM, so assume a file name has been given */
        *iniHandlePtr = PrfOpenProfile(TclOS2GetHAB(), rootName);
        if (*iniHandlePtr == NULLHANDLE) {
#ifdef VERBOSE
            printf("    PrfOpenProfile %s ERROR %x\n", rootName, *iniFilePtr);
            fflush(stdout);
#endif
            Tcl_AppendStringsToObj(resultPtr, "bad file name \"", rootName,
                                   "\"", NULL);
            return TCL_ERROR;
        }
#ifdef VERBOSE
        printf("    PrfOpenProfile %s: HINI %x\n", rootName, *iniHandlePtr);
        fflush(stdout);
#endif
    } else {
        *iniHandlePtr = iniHandles[index];
        /* Determine path of user/system profile */
        *iniFilePtr = iniFileNames[index];
#ifdef VERBOSE
        printf("    standard profile %s: HINI %x (%s) appName [%s]\n", rootName,
               *iniHandlePtr, *iniFilePtr, *appNamePtr);
        fflush(stdout);
#endif
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2CloseProfile --
 *
 *	This function closes an iniFile.
 *
 * Results:
 *	Only for a user-specified profile is actually closed; the user
 *	and system profiles stay open all the time and cannot be closed
 *	successfully anyway.
 *	Returns a standard Tcl result.
 *
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2CloseProfile(
    HINI iniHandle)
{
#ifdef VERBOSE
    printf("TclOS2CloseProfile()\n");
    fflush(stdout);
#endif
    if ( iniHandle != HINI_PROFILE && iniHandle != HINI_USERPROFILE &&
         iniHandle != HINI_SYSTEMPROFILE) {
        ret = PrfCloseProfile(iniHandle);
        if (ret != TRUE) {
#ifdef VERBOSE
            printf("PrfCloseProfile(%d) ERROR %x\n", iniHandle,
                   WinGetLastError(TclOS2GetHAB()));
#endif
            return TCL_ERROR;	/* Ignored anyway */
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * AppendSystemError --
 *
 *	This routine formats an OS/2 system error message and places
 *	it into the interpreter result.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendSystemError(
    Tcl_Interp *interp,		/* Current interpreter. */
    ULONG error)		/* Result code from error. */
{
    Tcl_DString ds;
    char id[TCL_INTEGER_SPACE], *msg;
    int length;
    Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);
#ifdef VERBOSE
    printf("AppendSystemError()\n");
    fflush(stdout);
#endif

    sprintf(id, "%lx", error);
    Tcl_ExternalToUtfDString(NULL, "System Error", -1, &ds);
    msg = Tcl_DStringValue(&ds);
    length = Tcl_DStringLength(&ds);
    Tcl_SetErrorCode(interp, "OS/2", id, msg, (char *) NULL);
    Tcl_AppendToObj(resultPtr, msg, length);

    if (length != 0) {
        Tcl_DStringFree(&ds);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetAppNames --
 *
 *      This function enumerates the applications in a profile. If the
 *      optional pattern is supplied, then only keys that match the
 *      pattern will be returned.
 *
 * Results:
 *      Returns the list of applications in the result object of the
 *      interpreter, or an error message on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2GetAppNames(
    Tcl_Interp *interp,         /* Current interpreter. */
    Tcl_Obj *iniFileObj,        /* Profile to enumerate. */
    Tcl_Obj *patternObj)        /* Optional match pattern. */
{
    char *p, *iniFile, *appName, *pattern, *name, *path;
    char *apps;
    HINI iniHandle;
    int length;
    ULONG appsLength;
    Tcl_Obj *resultPtr;
    int result = TCL_OK;
    Tcl_DString buf, ds;
#ifdef VERBOSE
    printf("TclOS2GetAppNames()\n");
    fflush(stdout);
#endif
    path = Tcl_GetStringFromObj(iniFileObj, &length);
    path = Tcl_UtfToExternalDString(NULL, path, -1, &buf);

    if (TclOS2OpenProfile(interp, path, &iniFile, &appName, &iniHandle)
            != TCL_OK) {
        Tcl_DStringFree(&buf);
        return TCL_ERROR;
    }
    Tcl_DStringFree(&buf);

    /* Determine applications */
    if (QueryAppNames(iniHandle, &apps, &appsLength) == TCL_ERROR) {
        TclOS2CloseProfile(iniHandle);
        return TCL_ERROR;
    }
    /*
     * apps now contains the names of the applications, separated by NULL
     * characters; the last is terminated with two successive NULLs.
     * appsLength now contains the total length of the list in apps excluding
     * the final NULL character.
     */
#ifdef VERBOSE
    printf("    PrfQueryProfileData returns %d in buffer (first %s)\n",
           appsLength, apps);
    fflush(stdout);
#endif

    if (patternObj) {
        pattern = Tcl_GetString(patternObj);
    } else {
        pattern = NULL;
    }

    /*
     * Enumerate over the apps until we get to the double NULL, indicating the
     * end of the list.
     */

    resultPtr = Tcl_GetObjResult(interp);
    for (p = apps; *p != '\0'; p += strlen(p)+1) {
        Tcl_ExternalToUtfDString(NULL, p, strlen(p), &ds);
        name = Tcl_DStringValue(&ds);
        if (!pattern || Tcl_StringMatch(name, pattern)) {
            result = Tcl_ListObjAppendElement(interp, resultPtr,
                    Tcl_NewStringObj(name, Tcl_DStringLength(&ds)));
            if (result != TCL_OK) {
                Tcl_DStringFree(&ds);
                break;
            }
#ifdef VERBOSE
            printf("    Adding %s\n", p);
            fflush(stdout);
#endif
        }
        Tcl_DStringFree(&ds);
    }
    ckfree(apps);

    TclOS2CloseProfile(iniHandle);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2GetAppKeyNames --
 *
 *      This function enumerates the keys of a given application in a
 *      profile.  If the optional pattern is supplied, then only key
 *      names that match the pattern will be returned.
 *
 * Results:
 *      Returns the list of key names in the result object of the
 *      interpreter, or an error message on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2GetAppKeyNames(
    Tcl_Interp *interp,         /* Current interpreter. */
    Tcl_Obj *appNameObj,        /* App to enumerate. */
    Tcl_Obj *patternObj)        /* Optional match pattern. */
{
    char *p, *iniFile, *path, *appName, *name;
    char *keys;
    ULONG keysLength;
    HINI iniHandle;
    int length;
    Tcl_Obj *resultPtr;
    int result = TCL_OK;
    char *pattern;
    Tcl_DString ds;
#ifdef VERBOSE
    printf("TclOS2GetKeyNames()\n");
    fflush(stdout);
#endif

    path = Tcl_GetStringFromObj(appNameObj, &length);
    path = Tcl_UtfToExternalDString(NULL, path, -1, &ds);

    if (TclOS2OpenProfile(interp, path, &iniFile, &appName, &iniHandle)
            != TCL_OK) {
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    /* Determine keys */
    if (QueryKeyNames(iniHandle, appName, &keys, &keysLength) == TCL_ERROR) {
        TclOS2CloseProfile(iniHandle);
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }
    Tcl_DStringFree(&ds);

    /*
     * keys now contains the names of the keys, separated by NULL characters;
     * the last is terminated with two successive NULLs.
     * keysLength now contains the total length of the list in keys excluding
     * the final NULL character.
     */
#ifdef VERBOSE
    printf("    PrfQueryProfileData returns %d in buffer (first %s)\n", keysLength,
           keys);
    fflush(stdout);
#endif

    if (patternObj) {
        pattern = Tcl_GetStringFromObj(patternObj, NULL);
    } else {
        pattern = NULL;
    }

    /*
     * Enumerate over the keys until we get to the double NULL, indicating the
     * end of the list.
     */

    resultPtr = Tcl_GetObjResult(interp);
    for (p = keys; *p != '\0'; p += length+1) {
        length = strlen(p);
        Tcl_ExternalToUtfDString(NULL, p, length, &ds);
        name = Tcl_DStringValue(&ds);
        if (!pattern || Tcl_StringMatch(name, pattern)) {
            result = Tcl_ListObjAppendElement(interp, resultPtr,
                    Tcl_NewStringObj(name, Tcl_DStringLength(&ds)));
            if (result != TCL_OK) {
                Tcl_DStringFree(&ds);
                break;
            }
#ifdef VERBOSE
            printf("    Adding %s\n", name);
            fflush(stdout);
#endif
        }
        Tcl_DStringFree(&ds);
    }
    ckfree(keys);

    TclOS2CloseProfile(iniHandle);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOS2SetAppKey --
 *
 *      This function sets the contents of a profile value.  If
 *      the application or key does not exist, it will be created.  If it
 *      does exist, then the data will be replaced.
 *      Only writing as binary data and string is possible.
 *
 * Results:
 *      Returns a normal Tcl result.
 *
 * Side effects:
 *      May create new apps or keys.
 *
 *----------------------------------------------------------------------
 */

static int
TclOS2SetAppKey(
    Tcl_Interp *interp,         /* Current interpreter. */
    Tcl_Obj *appNameObj,        /* Name of application. */
    Tcl_Obj *keyNameObj,        /* Name of key to set. */
    Tcl_Obj *dataObj,           /* Data to be written. */
    Tcl_Obj *typeObj)           /* Type of data to be written. */
{
    char *buffer, *iniFile, *appName, *keyName;
    ULONG type;
    HINI iniHandle;
    int length;
    Tcl_Obj *resultPtr;
    Tcl_DString ds, nameBuf;
#ifdef VERBOSE
    printf("TclOS2SetKey()\n");
    fflush(stdout);
#endif

    appName = Tcl_GetStringFromObj(appNameObj, &length);
    appName = Tcl_UtfToExternalDString(NULL, appName, -1, &ds);
    buffer = ckalloc(length + 1);
    strcpy(buffer, appName);
    keyName = keyNameObj != NULL ? Tcl_GetStringFromObj(keyNameObj, &length)
                                 : NULL;
#ifdef VERBOSE
    printf("TclOS2SetKey(%s, %s)\n", appName, keyName);
    fflush(stdout);
#endif

    /*
     * CAREFUL: NULL keyName will delete complete application from profile
     * Require TclOS2RegDelete to be used for that, so don't allow NULL.
     */
    if (keyName == NULL) {
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    if (typeObj == NULL) {
        type = STRING;
    } else if (Tcl_GetIndexFromObj(interp, typeObj, typeNames, "type",
            0, (int *) &type) != TCL_OK) {
        if (Tcl_GetIntFromObj(NULL, typeObj, (int*) &type) != TCL_OK) {
            Tcl_DStringFree(&ds);
            return TCL_ERROR;
        }
        Tcl_ResetResult(interp);
    }

    if (TclOS2OpenProfile(interp, buffer, &iniFile, &appName, &iniHandle)
            != TCL_OK) {
        ckfree(buffer); 
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }
    ckfree(buffer);

    keyName = Tcl_UtfToExternalDString(NULL, keyName, length, &nameBuf);
    resultPtr = Tcl_GetObjResult(interp);

    if (type == STRING || type == SZ) {
        char *data = Tcl_GetStringFromObj(dataObj, &length);

        ret = PrfWriteProfileData(iniHandle, appName, (PSZ)keyName, (PVOID)data,
                                 length);
#ifdef VERBOSE
        printf("PrfWriteProfileData(%x, %s, %s, <data>, %d) returns %d\n",
               iniHandle, appName, keyName, length, ret);
        fflush(stdout);
#endif
    } else {
        ULONG value;
        if (Tcl_GetIntFromObj(interp, dataObj, (int*) &value) != TCL_OK) {
            TclOS2CloseProfile(iniHandle);
            Tcl_DStringFree(&ds);
            return TCL_ERROR;
        }

        ret = PrfWriteProfileData(iniHandle, appName, (PSZ)keyName,
                                 (PVOID)&value, sizeof(value));
#ifdef VERBOSE
        printf("PrfWriteProfileData(%x, %s, %s, %x, %d) returns %d\n",
               iniHandle, appName, keyName, value, sizeof(value), ret);
        fflush(stdout);
#endif
    }
    Tcl_DStringFree(&ds); /* appName invalid */
    Tcl_DStringFree(&nameBuf); /* keyName invalid */
    TclOS2CloseProfile(iniHandle);
    if (ret != TRUE) {
        Tcl_AppendToObj(resultPtr, "unable to set value: ", -1);
        AppendSystemError(interp, WinGetLastError(TclOS2GetHAB()));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * QueryAppNames --
 *
 *      This function enumerates the applications in a profile.
 *
 * Results:
 *      Returns the list of applications in a buffer.
 *
 * Side effects:
 *      Allocates memory for the buffer, which the caller must free
 *      with ckfree();
 *
 *----------------------------------------------------------------------
 */

static int
QueryAppNames(
    HINI iniHandle,     /* Handle of already opened profile */
    char **buffer,      /* Address where to store pointer to apps-buffer */
    ULONG *length)      /* Address where to store the length of buffer */
{
    char *apps;
    ULONG appsLength;

    /* Determine required storage space */
    if (! PrfQueryProfileSize(iniHandle, NULL, NULL, &appsLength)) {
#ifdef VERBOSE
        printf("    QueryAppNames: PrfQueryProfileSize ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
        fflush(stdout);
#endif
        return TCL_ERROR;
    }

    /* Allocate storage space */
    apps = ckalloc(appsLength+1);
    if (apps == NULL) {
#ifdef VERBOSE
        printf("    QueryAppNames: can't alloc %d for apps-list", appsLength+1);
        fflush(stdout);
#endif
        return TCL_ERROR;
    }

    /* Fill the storage space with the app names */
    if ( PrfQueryProfileData(iniHandle, NULL, NULL, apps, &appsLength)
         != TRUE) {
#ifdef VERBOSE
        printf("    QueryAppNames: PrfQueryProfileData ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
        fflush(stdout);
#endif
        ckfree(apps);
        return TCL_ERROR;
    }
#ifdef VERBOSE
    printf("    QueryAppNames: PrfQueryProfileData OK first [%s]\n", apps);
    fflush(stdout);
#endif
    /* Add terminating NULL */
    apps[appsLength] = '\0';

    /* Only on succesful execution change the given memory locations */
    *buffer = apps;
    *length = appsLength+1;
    /* Caller has to ckfree() the storage space */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * QueryKeyNames --
 *
 *      This function enumerates the keys of an application in a profile.
 *
 * Results:
 *      Returns the list of keys in a buffer.
 *
 * Side effects:
 *      Allocates memory for the buffer, which the caller must free
 *      with ckfree();
 *
 *----------------------------------------------------------------------
 */

static int
QueryKeyNames(
    HINI iniHandle,     /* Handle of already opened profile */
    char *appName,      /* Application to query for its keys, not-NULL */
    char **buffer,      /* Address where to store pointer to keys-buffer */
    ULONG *length)      /* Address where to store the length of buffer */
{
    char *keys;
    ULONG keysLength;

    /* Sanity check */
    if (appName == NULL) return TCL_ERROR;

    /* Determine required storage space */
    if (! PrfQueryProfileSize(iniHandle, appName, NULL, &keysLength)) {
#ifdef VERBOSE
        printf("    QueryKeyNames: PrfQueryProfileSize ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
        fflush(stdout);
#endif
        return TCL_ERROR;
    }
#ifdef VERBOSE
    printf("    QueryKeyNames: PrfQueryProfileSize hIni 0x%x app %s: %d\n",
           iniHandle, appName, keysLength);
    fflush(stdout);
#endif

    /* Allocate storage space */
    keys = ckalloc(keysLength+1);
    if (keys == NULL) {
#ifdef VERBOSE
        printf("    QueryKeyNames: can't alloc %d for keys", keysLength+1);
        fflush(stdout);
#endif
        return TCL_ERROR;
    }

    /* Fill the storage space with the key names */
    if ( PrfQueryProfileData(iniHandle, appName, NULL, keys, &keysLength)
         != TRUE) {
#ifdef VERBOSE
        printf("    QueryKeyNames: PrfQueryProfileData ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
        fflush(stdout);
#endif
        ckfree(keys);
        return TCL_ERROR;
    }
    /* Add terminating NULL */
    keys[keysLength] = '\0';

    /* Only on succesful execution change the given memory locations */
    *buffer = keys;
    *length = keysLength+1;
    /* Caller has to ckfree() the storage space */
    return TCL_OK;
}

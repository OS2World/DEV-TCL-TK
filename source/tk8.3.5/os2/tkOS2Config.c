/* 
 * tkOS2Config.c --
 *
 *	This module implements the OS/2 system defaults for
 *	the configuration package.
 *
 * Copyright (c) 1997 by Sun Microsystems, Inc.
 * Copyright (c) 2002-2003 by Illya Vaes.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tk.h"
#include "tkInt.h"
#include "tkOS2Int.h"


/*
 *----------------------------------------------------------------------
 *
 * TkpGetSystemDefault --
 *
 *	Given a dbName and className for a configuration option,
 *	return a string representation of the option.
 *
 * Results:
 *	Returns a Tk_Uid that is the string identifier that identifies
 *	this option. Returns NULL if there are no system defaults
 *	that match this pair.
 *
 * Side effects:
 *	None, once the package is initialized.
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *
TkpGetSystemDefault(
    Tk_Window tkwin,		/* A window to use. */
    char *dbName,		/* The option database name. */
    char *className)		/* The name of the option class. */
{
    Tcl_Obj *valueObjPtr;
    Tk_Uid classUid;

    if (tkwin == NULL) {
	return NULL;
    }

    valueObjPtr = NULL;
    classUid = Tk_Class(tkwin);

    if (strcmp(classUid, "Menu") == 0) {
	valueObjPtr = TkOS2GetMenuSystemDefault(tkwin, dbName, className);
    }

    return valueObjPtr;
}

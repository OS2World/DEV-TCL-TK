/* 
 * tclOS2Dde.c --
 *
 *	This file provides procedures that implement the "send"
 *	command, allowing commands to be passed from interpreter
 *	to interpreter.
 *      The OS/2 version implements the functions of the Windows DDEML
 *      library that are used by the original Windows version of Tcl.
 *      It's available only when TclOS2GetUsePm() returns TRUE (ie. in a
 *      TclPM shell or in Tk).
 *      
 *      Generic notes:
 *      - "service name" was called "application name" in older DDE docs.
 *      - DDEML instances are specific to the threads that created them.
 *
 * Copyright (c) 1997 by Sun Microsystems, Inc.
 * Copyright (c) 2002 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tclPort.h"
#include "tclOS2Dde.h"

/*
 * TCL_STORAGE_CLASS is set unconditionally to DLLEXPORT because the
 * Registry_Init declaration is in the source file itself, which is only
 * accessed when we are building a library.
 */

#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT

/* 
 * The following structure is used to keep track of the interpreters
 * registered by this process.
 */

typedef struct RegisteredInterp {
    struct RegisteredInterp *nextPtr;
				/* The next interp this application knows
				 * about. */
    char *name;			/* Interpreter's name (malloc-ed). */
    Tcl_Interp *interp;		/* The interpreter attached to this name. */
} RegisteredInterp;

/*
 * Used to keep track of conversations.
 */

typedef struct Conversation {
    struct Conversation *nextPtr;
				/* The next conversation in the list. */
    RegisteredInterp *riPtr;	/* The info we know about the conversation. */
    HCONV hConv;		/* The DDE handle for this conversation. */
    Tcl_Obj *returnPackagePtr;	/* The result package for this conversation. */
} Conversation;

typedef struct ThreadSpecificData {
    Conversation *currentConversations;
                                /* A list of conversations currently
				 * being processed. */
    RegisteredInterp *interpListPtr;
                                /* List of all interpreters registered
				 * in the current process. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * The following variables cannot be placed in thread-local storage.
 * The Mutex ddeMutex guards access to the ddeInstance.
 */
static HSZ ddeService = 0;
static DWORD ddeInstance;       /* The application instance handle given
				 * to us by DdeInitialize. */
static int ddeIsServer = 0;

#define TCL_DDE_VERSION "1.1"
#define TCL_DDE_PACKAGE_NAME "dde"
#define TCL_DDE_SERVICE_NAME "TclEval"

TCL_DECLARE_MUTEX(ddeMutex)

/*
 * The following types and variables are used for the implementation /
 * emulation of DDEML and cannot be place in thread-local storage.
 * They are guarded by the Mutex tclOS2DDEMLMutex.
 */

typedef struct TclOS2DDEML_Conversation {
    struct TclOS2DDEML_Conversation *nextPtr;
				/* The next conversation in the list. */
    HCONV hConv;		/* The DDE handle for this conversation. */
} TclOS2DDEML_Conversation;

typedef struct TclOS2DDEML_Instance {
    struct TclOS2DDEML_Instance *prevPtr; /* Next on list */
    struct TclOS2DDEML_Instance *nextPtr; /* Previous on list */
    DWORD idInst;		/* Address of Instance Identifier */
    PFNCALLBACK pfnCallback;	/* Address of callback function */
    DWORD afCmd;		/* Set of command and filter flags */
    DWORD uRes;			/* Reserved */
    TclOS2DDEML_Conversation *convList; /* List of conversations for this app */
} TclOS2DDEML_Instance;

static TclOS2DDEML_Instance *tclOS2DDEMLInstanceListHead = NULL;
static TclOS2DDEML_Instance *tclOS2DDEMLInstanceListTail = NULL;

TCL_DECLARE_MUTEX(tclOS2DDEMLMutex)

/*
 * Forward declarations for procedures defined later in this file.
 */

static void		    DdeExitProc _ANSI_ARGS_((ClientData clientData));
static void		    DeleteProc _ANSI_ARGS_((ClientData clientData));
static Tcl_Obj *	    ExecuteRemoteObject _ANSI_ARGS_((
				RegisteredInterp *riPtr, 
				Tcl_Obj *ddeObjectPtr));
static int		    MakeDdeConnection _ANSI_ARGS_((Tcl_Interp *interp,
				char *name, HCONV *ddeConvPtr));
static HDDEDATA CALLBACK    DdeServerProc _ANSI_ARGS_((UINT uType,
				UINT uFmt, HCONV hConv, HSZ ddeTopic,
				HSZ ddeItem, HDDEDATA hData, DWORD dwData1, 
				DWORD dwData2));
static void		    SetDdeError _ANSI_ARGS_((Tcl_Interp *interp));
int Tcl_DdeObjCmd(ClientData clientData,	/* Used only for deletion */
	Tcl_Interp *interp,		/* The interp we are sending from */
	int objc,			/* Number of arguments */
	Tcl_Obj *CONST objv[]);	/* The arguments */

EXTERN int Dde_Init(Tcl_Interp *interp);

/*
 *----------------------------------------------------------------------
 *
 * Dde_Init --
 *
 *	This procedure initializes the dde command.
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
Dde_Init(
    Tcl_Interp *interp)
{
    ThreadSpecificData *tsdPtr;
    
    if (!Tcl_InitStubs(interp, "8.0", 0)) {
	return TCL_ERROR;
    }

    Tcl_CreateObjCommand(interp, "dde", Tcl_DdeObjCmd, NULL, NULL);

    tsdPtr = (ThreadSpecificData *)
	     Tcl_GetThreadData((Tcl_ThreadDataKey *) &dataKey,
                               sizeof(ThreadSpecificData));
    
    if (tsdPtr == NULL) {
	tsdPtr = TCL_TSD_INIT(&dataKey);
	tsdPtr->currentConversations = NULL;
	tsdPtr->interpListPtr = NULL;
    }
    Tcl_CreateExitHandler(DdeExitProc, NULL);

    return Tcl_PkgProvide(interp, "dde", "1.0");
}

static void
Initialize()
{
    int nameFound = 0;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
    
    /*
     * See if the application is already registered; if so, remove its
     * current name from the registry. The deletion of the command
     * will take care of disposing of this entry.
     */

    if (tsdPtr->interpListPtr != NULL) {
	nameFound = 1;
    }

    /*
     * Make sure that the DDE server is there. This is done only once,
     * add an exit handler tear it down.
     */

    if (ddeInstance == 0) {
	Tcl_MutexLock(&ddeMutex);
	if (ddeInstance == 0) {
	    if (DdeInitialize(&ddeInstance, DdeServerProc,
		    CBF_SKIP_REGISTRATIONS
		    | CBF_SKIP_UNREGISTRATIONS
		    | CBF_FAIL_POKES, 0) 
		    != DMLERR_NO_ERROR) {
		DdeUninitialize(ddeInstance);
		ddeInstance = 0;
	    }
	}
	Tcl_MutexUnlock(&ddeMutex);
    }
    if ((ddeService == 0) && (nameFound != 0)) {
	Tcl_MutexLock(&ddeMutex);
	if ((ddeService == 0) && (nameFound != 0)) {
	    ddeIsServer = 1;
	    Tcl_CreateExitHandler(DdeExitProc, NULL);
	    ddeService = DdeCreateStringHandle(ddeInstance, "TclEval", 0);
	    DdeNameService(ddeInstance, ddeService, 0L, DNS_REGISTER);
	} else {
	    ddeIsServer = 0;
	}
	Tcl_MutexUnlock(&ddeMutex);
    }
}    


/*
 *--------------------------------------------------------------
 *
 * DdeSetServerName --
 *
 *	This procedure is called to associate an ASCII name with a Dde
 *	server.  If the interpreter has already been named, the
 *	name replaces the old one.
 *
 * Results:
 *	The return value is the name actually given to the interp.
 *	This will normally be the same as name, but if name was already
 *	in use for a Dde Server then a name of the form "name #2" will
 *	be chosen,  with a high enough number to make the name unique.
 *
 * Side effects:
 *	Registration info is saved, thereby allowing the "send" command
 *	to be used later to invoke commands in the application.  In
 *	addition, the "send" command is created in the application's
 *	interpreter.  The registration will be removed automatically
 *	if the interpreter is deleted or the "send" command is removed.
 *
 *--------------------------------------------------------------
 */

static char *
DdeSetServerName(interp, name)
    Tcl_Interp *interp;
    char *name;			/* The name that will be used to
				 * refer to the interpreter in later
				 * "send" commands.  Must be globally
				 * unique. */
{
    int suffix, offset;
    RegisteredInterp *riPtr, *prevPtr;
    Tcl_DString dString;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    /*
     * See if the application is already registered; if so, remove its
     * current name from the registry. The deletion of the command
     * will take care of disposing of this entry.
     */

    for (riPtr = tsdPtr->interpListPtr, prevPtr = NULL; riPtr != NULL; 
	    prevPtr = riPtr, riPtr = riPtr->nextPtr) {
	if (riPtr->interp == interp) {
	    if (name != NULL) {
		if (prevPtr == NULL) {
		    tsdPtr->interpListPtr = tsdPtr->interpListPtr->nextPtr;
		} else {
		    prevPtr->nextPtr = riPtr->nextPtr;
		}
		break;
	    } else {
		/*
		 * the name was NULL, so the caller is asking for
		 * the name of the current interp.
		 */

		return riPtr->name;
	    }
	}
    }

    if (name == NULL) {
	/*
	 * the name was NULL, so the caller is asking for
	 * the name of the current interp, but it doesn't
	 * have a name.
	 */

	return "";
    }
    
    /*
     * Pick a name to use for the application.  Use "name" if it's not
     * already in use.  Otherwise add a suffix such as " #2", trying
     * larger and larger numbers until we eventually find one that is
     * unique.
     */

    suffix = 1;
    offset = 0;
    Tcl_DStringInit(&dString);

    /*
     * We have found a unique name. Now add it to the registry.
     */

    riPtr = (RegisteredInterp *) ckalloc(sizeof(RegisteredInterp));
    riPtr->interp = interp;
    riPtr->name = ckalloc(strlen(name) + 1);
    riPtr->nextPtr = tsdPtr->interpListPtr;
    tsdPtr->interpListPtr = riPtr;
    strcpy(riPtr->name, name);

    Tcl_CreateObjCommand(interp, "dde", Tcl_DdeObjCmd,
	    (ClientData) riPtr, DeleteProc);
    if (Tcl_IsSafe(interp)) {
	Tcl_HideCommand(interp, "dde", "dde");
    }
    Tcl_DStringFree(&dString);

    return riPtr->name;
}

/*
 *--------------------------------------------------------------
 *
 * DeleteProc
 *
 *	This procedure is called when the command "dde" is destroyed.
 *
 * Results:
 *	none
 *
 * Side effects:
 *	The interpreter given by riPtr is unregistered.
 *
 *--------------------------------------------------------------
 */

static void
DeleteProc(clientData)
    ClientData clientData;	/* The interp we are deleting passed
				 * as ClientData. */
{
    RegisteredInterp *riPtr = (RegisteredInterp *) clientData;
    RegisteredInterp *searchPtr, *prevPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    for (searchPtr = tsdPtr->interpListPtr, prevPtr = NULL;
	    (searchPtr != NULL) && (searchPtr != riPtr);
	    prevPtr = searchPtr, searchPtr = searchPtr->nextPtr) {
	/*
	 * Empty loop body.
	 */
    }

    if (searchPtr != NULL) {
	if (prevPtr == NULL) {
	    tsdPtr->interpListPtr = tsdPtr->interpListPtr->nextPtr;
	} else {
	    prevPtr->nextPtr = searchPtr->nextPtr;
	}
    }
    ckfree(riPtr->name);
    Tcl_EventuallyFree(clientData, TCL_DYNAMIC);
}

/*
 *--------------------------------------------------------------
 *
 * ExecuteRemoteObject --
 *
 *	Takes the package delivered by DDE and executes it in
 *	the server's interpreter.
 *
 * Results:
 *	A list Tcl_Obj * that describes what happened. The first
 *	element is the numerical return code (TCL_ERROR, etc.).
 *	The second element is the result of the script. If the
 *	return result was TCL_ERROR, then the third element
 *	will be the value of the global "errorCode", and the
 *	fourth will be the value of the global "errorInfo".
 *	The return result will have a refCount of 0.
 *
 * Side effects:
 *	A Tcl script is run, which can cause all kinds of other
 *	things to happen.
 *
 *--------------------------------------------------------------
 */

static Tcl_Obj *
ExecuteRemoteObject(
    RegisteredInterp *riPtr,	    /* Info about this server. */
    Tcl_Obj *ddeObjectPtr)	    /* The object to execute. */
{
    Tcl_Obj *errorObjPtr;
    Tcl_Obj *returnPackagePtr;
    int result;

    result = Tcl_EvalObjEx(riPtr->interp, ddeObjectPtr, TCL_EVAL_GLOBAL);
    returnPackagePtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
    Tcl_ListObjAppendElement(NULL, returnPackagePtr,
	    Tcl_NewIntObj(result));
    Tcl_ListObjAppendElement(NULL, returnPackagePtr,
	    Tcl_GetObjResult(riPtr->interp));
    if (result == TCL_ERROR) {
	errorObjPtr = Tcl_GetVar2Ex(riPtr->interp, "errorCode", NULL,
		TCL_GLOBAL_ONLY);
	Tcl_ListObjAppendElement(NULL, returnPackagePtr, errorObjPtr);
	errorObjPtr = Tcl_GetVar2Ex(riPtr->interp, "errorInfo", NULL,
		TCL_GLOBAL_ONLY);
        Tcl_ListObjAppendElement(NULL, returnPackagePtr, errorObjPtr);
    }

    return returnPackagePtr;
}

/*
 *--------------------------------------------------------------
 *
 * DdeServerProc --
 *
 *	Handles all transactions for this server. Can handle
 *	execute, request, and connect protocols. Dde will
 *	call this routine when a client attempts to run a dde
 *	command using this server.
 *
 * Results:
 *	A DDE Handle with the result of the dde command.
 *
 * Side effects:
 *	Depending on which command is executed, arbitrary
 *	Tcl scripts can be run.
 *
 *--------------------------------------------------------------
 */

static HDDEDATA CALLBACK
DdeServerProc (
    UINT uType,			/* The type of DDE transaction we
				 * are performing. */
    UINT uFmt,			/* The format that data is sent or
				 * received. */
    HCONV hConv,		/* The conversation associated with the 
				 * current transaction. */
    HSZ ddeTopic,		/* A string handle. Transaction-type 
				 * dependent. */
    HSZ ddeItem,		/* A string handle. Transaction-type 
				 * dependent. */
    HDDEDATA hData,		/* DDE data. Transaction-type dependent. */
    DWORD dwData1,		/* Transaction-dependent data. */
    DWORD dwData2)		/* Transaction-dependent data. */
{
    Tcl_DString dString;
    ULONG len;
    char *utilString;
    Tcl_Obj *ddeObjectPtr;
    HDDEDATA ddeReturn = NULL;
    RegisteredInterp *riPtr;
    Conversation *convPtr, *prevConvPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    switch(uType) {
	case XTYP_CONNECT:

	    /*
	     * Dde is trying to initialize a conversation with us. Check
	     * and make sure we have a valid topic.
	     */

	    len = DdeQueryString(ddeInstance, ddeTopic, NULL, 0, 0);
	    Tcl_DStringInit(&dString);
	    Tcl_DStringSetLength(&dString, len);
	    utilString = Tcl_DStringValue(&dString);
	    DdeQueryString(ddeInstance, ddeTopic, utilString, len + 1,
		    CP_WINANSI);

	    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL;
		    riPtr = riPtr->nextPtr) {
		if (stricmp(utilString, riPtr->name) == 0) {
		    Tcl_DStringFree(&dString);
		    return (HDDEDATA) TRUE;
		}
	    }

	    Tcl_DStringFree(&dString);
	    return (HDDEDATA) FALSE;

	case XTYP_CONNECT_CONFIRM:

	    /*
	     * Dde has decided that we can connect, so it gives us a 
	     * conversation handle. We need to keep track of it
	     * so we know which execution result to return in an
	     * XTYP_REQUEST.
	     */

	    len = DdeQueryString(ddeInstance, ddeTopic, NULL, 0, 0);
	    Tcl_DStringInit(&dString);
	    Tcl_DStringSetLength(&dString, len);
	    utilString = Tcl_DStringValue(&dString);
	    DdeQueryString(ddeInstance, ddeTopic, utilString, len + 1, 
		    CP_WINANSI);
	    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; 
		    riPtr = riPtr->nextPtr) {
		if (stricmp(riPtr->name, utilString) == 0) {
		    convPtr = (Conversation *) ckalloc(sizeof(Conversation));
		    convPtr->nextPtr = tsdPtr->currentConversations;
		    convPtr->returnPackagePtr = NULL;
		    convPtr->hConv = hConv;
		    convPtr->riPtr = riPtr;
		    tsdPtr->currentConversations = convPtr;
		    break;
		}
	    }
	    Tcl_DStringFree(&dString);
	    return (HDDEDATA) TRUE;

	case XTYP_DISCONNECT:

	    /*
	     * The client has disconnected from our server. Forget this
	     * conversation.
	     */

	    for (convPtr = tsdPtr->currentConversations, prevConvPtr = NULL;
		    convPtr != NULL; 
		    prevConvPtr = convPtr, convPtr = convPtr->nextPtr) {
		if (hConv == convPtr->hConv) {
		    if (prevConvPtr == NULL) {
			tsdPtr->currentConversations = convPtr->nextPtr;
		    } else {
			prevConvPtr->nextPtr = convPtr->nextPtr;
		    }
		    if (convPtr->returnPackagePtr != NULL) {
			Tcl_DecrRefCount(convPtr->returnPackagePtr);
		    }
		    ckfree((char *) convPtr);
		    break;
		}
	    }
	    return (HDDEDATA) TRUE;

	case XTYP_REQUEST:

	    /*
	     * This could be either a request for a value of a Tcl variable,
	     * or it could be the send command requesting the results of the
	     * last execute.
	     */

	    if (uFmt != CF_TEXT) {
		return (HDDEDATA) FALSE;
	    }

	    ddeReturn = (HDDEDATA) FALSE;
	    for (convPtr = tsdPtr->currentConversations; (convPtr != NULL)
		    && (convPtr->hConv != hConv); convPtr = convPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */
	    }

	    if (convPtr != NULL) {
		char *returnString;

		len = DdeQueryString(ddeInstance, ddeItem, NULL, 0,
			CP_WINANSI);
		Tcl_DStringInit(&dString);
		Tcl_DStringSetLength(&dString, len);
		utilString = Tcl_DStringValue(&dString);
		DdeQueryString(ddeInstance, ddeItem, utilString, 
                        len + 1, CP_WINANSI);
		if (stricmp(utilString, "$TCLEVAL$EXECUTE$RESULT") == 0) {
		    returnString =
		        Tcl_GetStringFromObj(convPtr->returnPackagePtr,
                                             (int *)&len);
		    ddeReturn = DdeCreateDataHandle(ddeInstance,
			    returnString, len+1, 0, ddeItem, CF_TEXT,
			    0);
		} else {
		    Tcl_Obj *variableObjPtr = Tcl_GetVar2Ex(
			    convPtr->riPtr->interp, utilString, NULL, 
			    TCL_GLOBAL_ONLY);
		    if (variableObjPtr != NULL) {
			returnString = Tcl_GetStringFromObj(variableObjPtr,
				                            (int *)&len);
			ddeReturn = DdeCreateDataHandle(ddeInstance,
				returnString, len+1, 0, ddeItem, CF_TEXT, 0);
		    } else {
			ddeReturn = NULL;
		    }
		}
		Tcl_DStringFree(&dString);
	    }
	    return ddeReturn;

	case XTYP_EXECUTE: {

	    /*
	     * Execute this script. The results will be saved into
	     * a list object which will be retreived later. See
	     * ExecuteRemoteObject.
	     */

	    Tcl_Obj *returnPackagePtr;

	    for (convPtr = tsdPtr->currentConversations; (convPtr != NULL)
		    && (convPtr->hConv != hConv); convPtr = convPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */

	    }

	    if (convPtr == NULL) {
		return (HDDEDATA) DDE_NOTPROCESSED;
	    }

	    utilString = (char *) DdeAccessData(hData, &len);
	    ddeObjectPtr = Tcl_NewStringObj(utilString, -1);
	    Tcl_IncrRefCount(ddeObjectPtr);
	    DdeUnaccessData(hData);
	    if (convPtr->returnPackagePtr != NULL) {
		Tcl_DecrRefCount(convPtr->returnPackagePtr);
	    }
	    convPtr->returnPackagePtr = NULL;
	    returnPackagePtr = 
		    ExecuteRemoteObject(convPtr->riPtr, ddeObjectPtr);
	    for (convPtr = tsdPtr->currentConversations; (convPtr != NULL)
 		    && (convPtr->hConv != hConv); convPtr = convPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */

	    }
	    if (convPtr != NULL) {
		Tcl_IncrRefCount(returnPackagePtr);
		convPtr->returnPackagePtr = returnPackagePtr;
	    }
	    Tcl_DecrRefCount(ddeObjectPtr);
	    if (returnPackagePtr == NULL) {
		return (HDDEDATA) DDE_NOTPROCESSED;
	    } else {
		return (HDDEDATA) DDE_FACK;
	    }
	}
	    
	case XTYP_WILDCONNECT: {

	    /*
	     * Dde wants a list of services and topics that we support.
	     */

	    HSZPAIR *returnPtr;
	    int i;
	    int numItems;

	    for (i = 0, riPtr = tsdPtr->interpListPtr; riPtr != NULL;
		    i++, riPtr = riPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */

	    }

	    numItems = i;
	    ddeReturn = DdeCreateDataHandle(ddeInstance, NULL,
		    (numItems + 1) * sizeof(HSZPAIR), 0, 0, 0, 0);
	    returnPtr = (HSZPAIR *) DdeAccessData(ddeReturn, &len);
	    for (i = 0, riPtr = tsdPtr->interpListPtr; i < numItems; 
		    i++, riPtr = riPtr->nextPtr) {
		returnPtr[i].hszSvc = DdeCreateStringHandle(
                        ddeInstance, "TclEval", CP_WINANSI);
		returnPtr[i].hszTopic = DdeCreateStringHandle(
                        ddeInstance, riPtr->name, CP_WINANSI);
	    }
	    returnPtr[i].hszSvc = NULL;
	    returnPtr[i].hszTopic = NULL;
	    DdeUnaccessData(ddeReturn);
	    return ddeReturn;
	}

    }
    return NULL;
}


/*
 *--------------------------------------------------------------
 *
 * DdeExitProc --
 *
 *	Gets rid of our DDE server when we go away.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The DDE server is deleted.
 *
 *--------------------------------------------------------------
 */

static void
DdeExitProc(
    ClientData clientData)	    /* Not used in this handler. */
{
    /* Not used
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
    */

    DdeNameService(ddeInstance, NULL, 0, DNS_UNREGISTER);
    DdeUninitialize(ddeInstance);
    ddeInstance = 0;
}

/*
 *--------------------------------------------------------------
 *
 * MakeDdeConnection --
 *
 *	This procedure is a utility used to connect to a DDE
 *	server when given a server name and a topic name.
 *
 * Results:
 *	A standard Tcl result.
 *	
 *
 * Side effects:
 *	Passes back a conversation through ddeConvPtr
 *
 *--------------------------------------------------------------
 */

static int
MakeDdeConnection(
    Tcl_Interp *interp,		/* Used to report errors. */
    char *name,			/* The connection to use. */
    HCONV *ddeConvPtr)
{
    HSZ ddeTopic, ddeService;
    HCONV ddeConv;
    /* Not used
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
    */
    
    ddeService = DdeCreateStringHandle(ddeInstance, "TclEval", 0);
    ddeTopic = DdeCreateStringHandle(ddeInstance, name, 0);

    ddeConv = DdeConnect(ddeInstance, ddeService, ddeTopic, NULL);
    DdeFreeStringHandle(ddeInstance, ddeService);
    DdeFreeStringHandle(ddeInstance, ddeTopic);

    if (ddeConv == (HCONV) NULL) {
	if (interp != NULL) {
	    Tcl_AppendResult(interp, "no registered server named \"",
		    name, "\"", (char *) NULL);
	}
	return TCL_ERROR;
    }

    *ddeConvPtr = ddeConv;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * SetDdeError --
 *
 *	Sets the interp result to a cogent error message
 *	describing the last DDE error.
 *
 * Results:
 *	None.
 *	
 *
 * Side effects:
 *	The interp's result object is changed.
 *
 *--------------------------------------------------------------
 */

static void
SetDdeError(
    Tcl_Interp *interp)	    /* The interp to put the message in.*/
{
    Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);
    int err;
    /* Not used
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);
    */

    err = DdeGetLastError(ddeInstance);
    switch (err) {
	case DMLERR_DATAACKTIMEOUT:
	case DMLERR_EXECACKTIMEOUT:
	case DMLERR_POKEACKTIMEOUT:
	    Tcl_SetStringObj(resultPtr,
		    "remote interpreter did not respond", -1);
	    break;

	case DMLERR_BUSY:
	    Tcl_SetStringObj(resultPtr, "remote server is busy", -1);
	    break;

	case DMLERR_NOTPROCESSED:
	    Tcl_SetStringObj(resultPtr, 
		    "remote server cannot handle this command", -1);
	    break;

	default:
	    Tcl_SetStringObj(resultPtr, "dde command failed", -1);
    }
}

/*
 *--------------------------------------------------------------
 *
 * Tcl_DdeObjCmd --
 *
 *	This procedure is invoked to process the "dde" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
Tcl_DdeObjCmd(
    ClientData clientData,	/* Used only for deletion */
    Tcl_Interp *interp,		/* The interp we are sending from */
    int objc,			/* Number of arguments */
    Tcl_Obj *CONST objv[])	/* The arguments */
{
    enum {
	DDE_SERVERNAME,
	DDE_EXECUTE,
	DDE_REQUEST,
	DDE_SERVICES,
	DDE_EVAL
    };

    static char *ddeCommands[] = {"servername", "execute",
          "request", "services", "eval", 
	  (char *) NULL};
    static char *ddeOptions[] = {"-async", (char *) NULL};
    int index, argIndex;
    int async = 0;
    int result = TCL_OK;
    HSZ ddeService = NULL;
    HSZ ddeTopic = NULL;
    HSZ ddeItem = NULL;
    HDDEDATA ddeData = NULL;
    HDDEDATA ddeItemData = NULL;
    HCONV hConv;
    HSZ ddeCookie = 0;
    char *serviceName, *topicName, *itemString, *dataString;
    char *string;
    int firstArg = 0, length;
    LONG dataLength;
    DWORD ddeResult;
    HDDEDATA ddeReturn;
    RegisteredInterp *riPtr;
    Tcl_Interp *sendInterp;
    Tcl_Obj *objPtr;
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKey);

    /*
     * Initialize DDE server/client
     */
    
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, 
		"?-async? serviceName topicName value");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], ddeCommands, "command", 0,
	    &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch (index) {
	case DDE_SERVERNAME:
	    if ((objc != 3) && (objc != 2)) {
		Tcl_WrongNumArgs(interp, 1, objv, 
			"servername ?serverName?");
		return TCL_ERROR;
	    }
	    firstArg = (objc - 1);
	    break;
	case DDE_EXECUTE:
	    if ((objc < 5) || (objc > 6)) {
		Tcl_WrongNumArgs(interp, 1, objv, 
			"execute ?-async? serviceName topicName value");
		return TCL_ERROR;
	    }
	    if (Tcl_GetIndexFromObj(NULL, objv[2], ddeOptions, "option", 0,
		    &argIndex) != TCL_OK) {
		if (objc != 5) {
		    Tcl_WrongNumArgs(interp, 1, objv,
			    "execute ?-async? serviceName topicName value");
		    return TCL_ERROR;
		}
		async = 0;
		firstArg = 2;
	    } else {
		if (objc != 6) {
		    Tcl_WrongNumArgs(interp, 1, objv,
			    "execute ?-async? serviceName topicName value");
		    return TCL_ERROR;
		}
		async = 1;
		firstArg = 3;
	    }
	    break;
	case DDE_REQUEST:
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 1, objv, 
			"request serviceName topicName value");
		return TCL_ERROR;
	    }
	    firstArg = 2;
	    break;
	case DDE_SERVICES:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 1, objv,
			"services serviceName topicName");
		return TCL_ERROR;
	    }
	    firstArg = 2;
	    break;
	case DDE_EVAL:
	    if (objc < 4) {
		Tcl_WrongNumArgs(interp, 1, objv, 
			"eval ?-async? serviceName args");
		return TCL_ERROR;
	    }
	    if (Tcl_GetIndexFromObj(NULL, objv[2], ddeOptions, "option", 0,
		    &argIndex) != TCL_OK) {
		if (objc < 4) {
		    Tcl_WrongNumArgs(interp, 1, objv,
			    "eval ?-async? serviceName args");
		    return TCL_ERROR;
		}
		async = 0;
		firstArg = 2;
	    } else {
		if (objc < 5) {
		    Tcl_WrongNumArgs(interp, 1, objv,
			    "eval ?-async? serviceName args");
		    return TCL_ERROR;
		}
		async = 1;
		firstArg = 3;
	    }
	    break;
    }

    if (firstArg != 1) {
	serviceName = Tcl_GetStringFromObj(objv[firstArg], &length);
    } else {
	serviceName = NULL;
    }

    if (length == 0) {
	serviceName = NULL;
    } else if (index != DDE_SERVERNAME) {
	ddeService = DdeCreateStringHandle(ddeInstance, serviceName,
		CP_WINANSI);
    }

    if ((index != DDE_SERVERNAME) &&(index != DDE_EVAL)) {
	topicName = Tcl_GetStringFromObj(objv[firstArg + 1], &length);
	if (length == 0) {
	    topicName = NULL;
	} else {
	    ddeTopic = DdeCreateStringHandle(ddeInstance, 
		    topicName, CP_WINANSI);
	}
    }

    switch (index) {
	case DDE_SERVERNAME: {
	    serviceName = DdeSetServerName(interp, serviceName);
	    if (serviceName != NULL) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp),
			serviceName, -1);
		Initialize();
	    } else {
		Tcl_ResetResult(interp);
	    }
	    break;
	}
	case DDE_EXECUTE: {
	    Initialize();
	    dataString = Tcl_GetStringFromObj(objv[firstArg + 2],
                                              (int *)&dataLength);
	    if (dataLength == 0) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp),
			"cannot execute null data", -1);
		result = TCL_ERROR;
		break;
	    }
	    hConv = DdeConnect(ddeInstance, ddeService, ddeTopic, 
                    NULL);

	    if (hConv == NULL) {
		SetDdeError(interp);
		result = TCL_ERROR;
		break;
	    }

	    ddeData = DdeCreateDataHandle(ddeInstance, dataString,
		    dataLength+1, 0, 0, CF_TEXT, 0);
	    if (ddeData != NULL) {
		if (async) {
		    DdeClientTransaction((LPBYTE) ddeData, 0xFFFFFFFF, hConv, 0, 
			    CF_TEXT, XTYP_EXECUTE, TIMEOUT_ASYNC, &ddeResult);
		    DdeAbandonTransaction(ddeInstance, hConv, 
                            ddeResult);
		} else {
		    ddeReturn = DdeClientTransaction((LPBYTE) ddeData, 0xFFFFFFFF,
			    hConv, 0, CF_TEXT, XTYP_EXECUTE, 30000, NULL);
		    if (ddeReturn == 0) {
			SetDdeError(interp);
			result = TCL_ERROR;
		    }
		}
		DdeFreeDataHandle(ddeData);
	    } else {
		SetDdeError(interp);
		result = TCL_ERROR;
	    }
	    DdeDisconnect(hConv);
	    break;
	}
	case DDE_REQUEST: {
	    Initialize();
	    itemString = Tcl_GetStringFromObj(objv[firstArg + 2], &length);
	    if (length == 0) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp),
			"cannot request value of null data", -1);
		return TCL_ERROR;
	    }
	    hConv = DdeConnect(ddeInstance, ddeService, ddeTopic, 
                    NULL);
	    
	    if (hConv == NULL) {
		SetDdeError(interp);
		result = TCL_ERROR;
	    } else {
		Tcl_Obj *returnObjPtr;
		ddeItem = DdeCreateStringHandle(ddeInstance, 
                        itemString, CP_WINANSI);
		if (ddeItem != NULL) {
		    ddeData = DdeClientTransaction(NULL, 0, hConv, ddeItem,
			    CF_TEXT, XTYP_REQUEST, 5000, NULL);
		    if (ddeData == NULL) {
			SetDdeError(interp);
			result = TCL_ERROR;
		    } else {
			dataString = DdeAccessData(ddeData, &dataLength);
			returnObjPtr = Tcl_NewStringObj(dataString, -1);
			DdeUnaccessData(ddeData);
			DdeFreeDataHandle(ddeData);
			Tcl_SetObjResult(interp, returnObjPtr);
		    }
		} else {
		    SetDdeError(interp);
		    result = TCL_ERROR;
		}
		DdeDisconnect(hConv);
	    }

	    break;
	}
	case DDE_SERVICES: {
	    HCONVLIST hConvList;
	    CONVINFO convInfo;
	    Tcl_Obj *convListObjPtr, *elementObjPtr;
	    Tcl_DString dString;
	    char *name;
	    
	    Initialize();
	    convInfo.cb = sizeof(CONVINFO);
	    hConvList = DdeConnectList(ddeInstance, ddeService, 
                    ddeTopic, 0, NULL);
	    hConv = 0;
	    convListObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
	    Tcl_DStringInit(&dString);

	    while (hConv = DdeQueryNextServer(hConvList, hConv), hConv != 0) {
		elementObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
/*
		DdeQueryConvInfo(hConv, QID_SYNC, &convInfo);
*/
		length = DdeQueryString(ddeInstance, 
                        convInfo.hszSvcPartner, NULL, 0, CP_WINANSI);
		Tcl_DStringSetLength(&dString, length);
		name = Tcl_DStringValue(&dString);
		DdeQueryString(ddeInstance, convInfo.hszSvcPartner, 
                        name, length + 1, CP_WINANSI);
		Tcl_ListObjAppendElement(interp, elementObjPtr,
			Tcl_NewStringObj(name, length));
		length = DdeQueryString(ddeInstance, convInfo.hszTopic,
			NULL, 0, CP_WINANSI);
		Tcl_DStringSetLength(&dString, length);
		name = Tcl_DStringValue(&dString);
		DdeQueryString(ddeInstance, convInfo.hszTopic, name,
			length + 1, CP_WINANSI);
		Tcl_ListObjAppendElement(interp, elementObjPtr,
			Tcl_NewStringObj(name, length));
		Tcl_ListObjAppendElement(interp, convListObjPtr, elementObjPtr);
	    }
	    DdeDisconnectList(hConvList);
	    Tcl_SetObjResult(interp, convListObjPtr);
	    Tcl_DStringFree(&dString);
	    break;
	}
	case DDE_EVAL: {
	    Initialize();
	    objc -= (async + 3);
	    ((Tcl_Obj **) objv) += (async + 3);

            /*
	     * See if the target interpreter is local.  If so, execute
	     * the command directly without going through the DDE server.
	     * Don't exchange objects between interps.  The target interp could
	     * compile an object, producing a bytecode structure that refers to 
	     * other objects owned by the target interp.  If the target interp 
	     * is then deleted, the bytecode structure would be referring to 
	     * deallocated objects.
	     */
	    
	    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr
		     = riPtr->nextPtr) {
		if (stricmp(serviceName, riPtr->name) == 0) {
		    break;
		}
	    }
	    
	    if (riPtr != NULL) {
		/*
		 * This command is to a local interp. No need to go through
		 * the server.
		 */
		
		Tcl_Preserve((ClientData) riPtr);
		sendInterp = riPtr->interp;
		Tcl_Preserve((ClientData) sendInterp);
		
		/*
		 * Don't exchange objects between interps.  The target interp would
		 * compile an object, producing a bytecode structure that refers to 
		 * other objects owned by the target interp.  If the target interp 
		 * is then deleted, the bytecode structure would be referring to 
		 * deallocated objects.
		 */

		if (objc == 1) {
		    result = Tcl_EvalObjEx(sendInterp, objv[0], TCL_EVAL_GLOBAL);
		} else {
		    objPtr = Tcl_ConcatObj(objc, objv);
		    Tcl_IncrRefCount(objPtr);
		    result = Tcl_EvalObjEx(sendInterp, objPtr, TCL_EVAL_GLOBAL);
		    Tcl_DecrRefCount(objPtr);
		}
		if (interp != sendInterp) {
		    if (result == TCL_ERROR) {
			/*
			 * An error occurred, so transfer error information from the
			 * destination interpreter back to our interpreter.  
			 */
			
			Tcl_ResetResult(interp);
			objPtr = Tcl_GetVar2Ex(sendInterp, "errorInfo", NULL, 
				TCL_GLOBAL_ONLY);
			string = Tcl_GetStringFromObj(objPtr, &length);
			Tcl_AddObjErrorInfo(interp, string, length);
			
			objPtr = Tcl_GetVar2Ex(sendInterp, "errorCode", NULL,
				TCL_GLOBAL_ONLY);
			Tcl_SetObjErrorCode(interp, objPtr);
		    }
		    Tcl_SetObjResult(interp, Tcl_GetObjResult(sendInterp));
		}
		Tcl_Release((ClientData) riPtr);
		Tcl_Release((ClientData) sendInterp);
	    } else {
		/*
		 * This is a non-local request. Send the script to the server and poll
		 * it for a result.
		 */
		
		if (MakeDdeConnection(interp, serviceName, &hConv) != TCL_OK) {
		    goto error;
		}
		
		objPtr = Tcl_ConcatObj(objc, objv);
		string = Tcl_GetStringFromObj(objPtr, &length);
		ddeItemData = DdeCreateDataHandle(ddeInstance, string, length+1, 0, 0,
			CF_TEXT, 0);
		
		if (async) {
		    ddeData = DdeClientTransaction((LPBYTE) ddeItemData, 0xFFFFFFFF, hConv, 0,
			    CF_TEXT, XTYP_EXECUTE, TIMEOUT_ASYNC, &ddeResult);
		    DdeAbandonTransaction(ddeInstance, hConv, ddeResult);
		} else {
		    ddeData = DdeClientTransaction((LPBYTE) ddeItemData, 0xFFFFFFFF, hConv, 0,
			    CF_TEXT, XTYP_EXECUTE, 30000, NULL);
		    if (ddeData != 0) {
			
			ddeCookie = DdeCreateStringHandle(ddeInstance, 
				"$TCLEVAL$EXECUTE$RESULT", CP_WINANSI);
			ddeData = DdeClientTransaction(NULL, 0, hConv, ddeCookie,
				CF_TEXT, XTYP_REQUEST, 30000, NULL);
		    }
		}
		
		
		Tcl_DecrRefCount(objPtr);
		
		if (ddeData == 0) {
		    SetDdeError(interp);
		    DdeFreeDataHandle(ddeItemData);
		    DdeDisconnect(hConv);
		    goto error;
		}
		
		if (async == 0) {
		    Tcl_Obj *resultPtr;
		    
		    /*
		     * The return handle has a two or four element list in it.
                     * The first element is the return code (TCL_OK, TCL_ERROR,
                     * etc.). The second is the result of the script. If the
                     * return code is TCL_ERROR, then the third element is the
                     * value of the variable "errorCode", and the fourth is the
		     * value of the variable "errorInfo".
		     */
		    
		    resultPtr = Tcl_NewObj();
		    length = DdeGetData(ddeData, NULL, 0, 0);
		    Tcl_SetObjLength(resultPtr, length);
		    string = Tcl_GetString(resultPtr);
		    DdeGetData(ddeData, string, length, 0);
		    Tcl_SetObjLength(resultPtr, strlen(string));
		    
		    if (Tcl_ListObjIndex(NULL, resultPtr, 0, &objPtr) != TCL_OK) {
			Tcl_DecrRefCount(resultPtr);
			goto error;
		    }
		    if (Tcl_GetIntFromObj(NULL, objPtr, &result) != TCL_OK) {
			Tcl_DecrRefCount(resultPtr);
			goto error;
		    }
		    if (result == TCL_ERROR) {
			Tcl_ResetResult(interp);
			
			if (Tcl_ListObjIndex(NULL, resultPtr, 3, &objPtr) != TCL_OK) {
			    Tcl_DecrRefCount(resultPtr);
			    goto error;
			}
			length = -1;
			string = Tcl_GetStringFromObj(objPtr, &length);
			Tcl_AddObjErrorInfo(interp, string, length);
			
			Tcl_ListObjIndex(NULL, resultPtr, 2, &objPtr);
			Tcl_SetObjErrorCode(interp, objPtr);
		    }
		    if (Tcl_ListObjIndex(NULL, resultPtr, 1, &objPtr) != TCL_OK) {
			Tcl_DecrRefCount(resultPtr);
			goto error;
		    }
		    Tcl_SetObjResult(interp, objPtr);
		    Tcl_DecrRefCount(resultPtr);
		}
	    }
	}
    }
    if (ddeCookie != NULL) {
	DdeFreeStringHandle(ddeInstance, ddeCookie);
    }
    if (ddeItem != NULL) {
	DdeFreeStringHandle(ddeInstance, ddeItem);
    }
    if (ddeItemData != NULL) {
	DdeFreeDataHandle(ddeItemData);
    }
    if (ddeData != NULL) {
	DdeFreeDataHandle(ddeData);
    }
    if (hConv != NULL) {
	DdeDisconnect(hConv);
    }
    return result;

    error:
    Tcl_SetStringObj(Tcl_GetObjResult(interp),
	    "invalid data returned from server", -1);
    if (ddeCookie != NULL) {
	DdeFreeStringHandle(ddeInstance, ddeCookie);
    }
    if (ddeItem != NULL) {
	DdeFreeStringHandle(ddeInstance, ddeItem);
    }
    if (ddeItemData != NULL) {
	DdeFreeDataHandle(ddeItemData);
    }
    if (ddeData != NULL) {
	DdeFreeDataHandle(ddeData);
    }
    if (hConv != NULL) {
	DdeDisconnect(hConv);
    }
    return TCL_ERROR;
}

/* DDEML implementation / emulation functions */
/*
 *--------------------------------------------------------------
 *
 * DdeAbandonTransaction --
 *
 *	This function abandons the specified asynchronous transaction
 *      and releases all resources associated with the transaction.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

BOOL
DdeAbandonTransaction (
    DWORD idInst,		/* Instance Identifier */
    HCONV hConv,		/* Handle of conversation */
    DWORD idTransaction)	/* Transaction Identifier */
{
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * DdeAccessData --
 *
 *	This function provides access to the data in the specified
 *      DDE object. An application must call the DdeUnaccessData
 *      function when it has finished accessing the data in the object.
 *
 * Results:
 *	NULL on failure, pointer to first byte of data in the
 *      DDE object on success.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

LPBYTE
DdeAccessData (
    HDDEDATA hData,	/* Handle of DDE object */
    LPDWORD lpcbData)	/* Address of variable that receives data length */
{
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeClientTransaction --
 *
 *	This function begins a data transaction between a client
 *      and a server. Only a DDE client application can call this
 *      function, and the application can use it only after
 *      establishing a conversation with the server.
 *
 * Results:
 *	Zero for all unsuccessful transactions, nonzero for all
 *      successful asynchronous transactions and for synchronous
 *      transactions in which the client does not expect data from
 *      the server.
 *      The value is a data handle that identifies the data for
 *      successful synchronous transactions in which the client
 *      does expect data from the server.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

HDDEDATA DdeClientTransaction (
    LPBYTE lpbData,	/* Address of data to pass to server */
    DWORD cbData,	/* Length of data */
    HCONV hConv,	/* Handle of conversation */
    HSZ hszItem,	/* Handle of Item Name string */
    UINT uFmt,		/* Clipboard data format */
    UINT uType,		/* Transaction type */
    DWORD uTimeout,	/* Time-out duration */
    LPDWORD lpdwResult)	/* Address of transaction result */
{
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeConnect --
 *
 *	This function establishes a conversation with a server
 *      application that supports the specified Service Name and
 *      Topic Name pair. If more than one exists, the system selects
 *      only one.
 *
 * Results:
 *	0L on failure, handle of the established conversation otherwise.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *      All members of the default CONVCONTEXT structure are set to zero
 *      except cb, which specifies the size of the structure, and
 *      iCodePage, which specifies CP_WINANSI or CP_WINUNICODE.
 *
 *--------------------------------------------------------------
 */

HCONV DdeConnect (
    DWORD idInst,	/* Instance Identifier */
    HSZ hszService,	/* Handle of Service Name string */
    HSZ hszTopic,	/* Handle of Topic Name string */
    PCONVCONTEXT pCC)	/* Address of structure with context data */
{
    return (HCONV)NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeConnectList --
 *
 *	This function establishes a conversation with all server
 *      applications that support the specified Service Name and 
 *      Topic Name pair.
 *      An application can also use this function to obtain a
 *      list of conversation handles by passing the function an
 *      existing conversation handle. DDEML removes the handles
 *      of any terminated conversations from the conversation
 *      list. The resulting conversation list contains the handles
 *      of all currently established conversations that support
 *      the specified Service Name and Topic Name.
 *      NB. An application must free the conversation list handle
 *      returned by this function, regardless of whether any
 *      conversation handles within the list are active. To free
 *      the handle, an application can call DdeDisconnectList.
 *
 * Results:
 *	0L on failure, the handle of the old conversation list is
 *      no longer valid.
 *      Handle of the new conversation list otherwise.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

HCONVLIST DdeConnectList (
    DWORD idInst,		/* Instance Identifier */
    HSZ hszService,		/* Handle of Service Name string */
    HSZ hszTopic,		/* Handle of Topic Name string */
    HCONVLIST hConvList,	/* Handle of conversation list */
    PCONVCONTEXT pCC)		/* Address of structure with context data */
{
    return (HCONVLIST)NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeCreateDataHandle --
 *
 *	This function creates a DDE object and fills the object
 *      with data from the specified buffer. A DDE application
 *      uses this function during transactions that involve
 *      passing data to the partner application.
 *
 * Results:
 *	0L on failure, data handle otherwise.
 *      After the handle has been used as a parameter in another
 *      DDEML function or has been returned by a DDE callback function,
 *      the handle may be used only for read access to the DDE
 *      object identified by the handle.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

HDDEDATA DdeCreateDataHandle (
    DWORD idInst,	/* Instance Identifier */
    LPBYTE lpbSrcBuf,	/* Address of source buffer */
    DWORD cbInitData,	/* Length of DDE object */
    DWORD offSrcBuf,	/* Offset from beginning of source buffer */
    HSZ hszItem,	/* Handle of Item Name string */
    UINT uFmt,		/* Clipboard data format */
    UINT afCmd)		/* Creation flags */
{
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeCreateStringHandle --
 *
 *	This function creates a handle that identifies the string
 *      pointed to by the lpszString parameter.
 *
 * Results:
 *	0L on failure, handle of the string otherwise.
 *      When DdeKeepStringHandle has been called to keep a string
 *      handle (created or received in callback function), the
 *      application must free that string handle when it is no
 *      longer needed.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

HSZ DdeCreateStringHandle (
    DWORD idInst,	/* Instance Identifier */
    LPCSTR lpszString,	/* Address of null-terminated string */
    int codepage)	/* Code page identifier, CP_WINANSI or CP_WINUNICODE */
{
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeDisconnect --
 *
 *	This function terminates a conversation started by either
 *      DdeConnect or DdeConnectList function and invalidates the
 *      specified conversation handle.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

BOOL DdeDisconnect (
    HCONV hConv)	/* Handle of conversation */
{
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * DdeDisconnectList --
 *
 *	This function destroys the specified conversation list
 *      and terminates all conversations associated with the list.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Get error value with DdeGetLastError.
 *
 *--------------------------------------------------------------
 */

BOOL DdeDisconnectList (
    HCONVLIST hConvList)	/* Handle of conversation list */
{
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * DdeFreeDataHandle --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

BOOL DdeFreeDataHandle (
    HDDEDATA hData)	/* Handle of DDE object */
{
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * DdeFreeStringHandle --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

BOOL DdeFreeStringHandle (
    DWORD idInst,	/* Instance Identifier */
    HSZ hsz)		/* Handle of string */
{
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * DdeGetData --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

DWORD DdeGetData (
    HDDEDATA hData,	/* Handle of DDE object */
    LPBYTE lpbDest,	/* Address of destination buffer */
    DWORD cbMax,	/* Amount of data to copy */
    DWORD offSrc)	/* Offset to beginning of data */
{
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * DdeGetLastError --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

UINT DdeGetLastError (
    DWORD idInst)	/* Instance Identifier */
{
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * DdeInitialize --
 *
 *      The DdeInitialize function registers an application with
 *      the Dynamic Data Exchange Management Library (DDEML).
 *      An application must call this function before calling 
 *      any other DDEML function
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

UINT DdeInitialize (
    LPDWORD lpidInst,		/* Address of Instance Identifier */
    PFNCALLBACK pfnCallback,	/* Address of callback function */
    DWORD afCmd,		/* Set of command and filter flags */
    DWORD uRes)			/* Reserved */
{
    TclOS2DDEML_Instance *instance;
    /*
     * If lpidInst points to a nonzero value, reinitialization of the
     * DDEML is implied. In this case, lpidInst must point to a valid
     * application-instance identifier.
     */
    if (lpidInst != (LPDWORD) 0) {
        /* Check the application-instance identifier */
        /* If found supposedly reinitialize DDEML */
        TclOS2DDEML_Instance *searchInstance;
        for ( searchInstance = tclOS2DDEMLInstanceListHead;
              searchInstance != NULL && searchInstance->idInst != *lpidInst;
              searchInstance = searchInstance->nextPtr) {
            /* Nothing */
        }
        if (searchInstance == NULL) {
            return DMLERR_INVALIDPARAMETER;
        }
        return DMLERR_NO_ERROR;
    }

    /* Insert into list of instances */
    instance = (TclOS2DDEML_Instance *) ckalloc(sizeof(TclOS2DDEML_Instance));
    if (instance == NULL) {
        return DMLERR_SYS_ERROR;
    }

    Tcl_MutexLock(&tclOS2DDEMLMutex);

    if (tclOS2DDEMLInstanceListHead == NULL) {
        /* No entries yet, start with ID 1 */
        instance->idInst = 1;
        tclOS2DDEMLInstanceListHead = instance;
    } else {
        /* Already entries in list, use next ID */
        if (tclOS2DDEMLInstanceListTail->idInst + 1
            < tclOS2DDEMLInstanceListTail->idInst) {
            /* Wrapped */
            ckfree((char *) instance);
            return DMLERR_SYS_ERROR;
        }
        instance->idInst = tclOS2DDEMLInstanceListTail->idInst + 1;
        tclOS2DDEMLInstanceListTail->nextPtr = instance;
    }
    instance->prevPtr = tclOS2DDEMLInstanceListTail;
    instance->nextPtr = NULL;
    tclOS2DDEMLInstanceListTail = instance;
    instance->pfnCallback = pfnCallback;
    instance->afCmd = afCmd;
    instance->uRes = uRes;
    instance->convList = NULL;

    Tcl_MutexUnlock(&tclOS2DDEMLMutex);

    return DMLERR_NO_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * DdeNameService --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

HDDEDATA DdeNameService (
    DWORD idInst,	/* Instance Identifier */
    HSZ hszService,	/* Handle of Service Name string */
    HSZ hszRes,		/* reserved */
    UINT afCmd)		/* Service Name flags */
{
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeQueryNextServer --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

HCONV DdeQueryNextServer (
    HCONVLIST hConvList,	/* Handle of conversation list */
    HCONV hConvPrev)		/* Previous conversation handle */
{
    return (HCONV)NULL;
}

/*
 *--------------------------------------------------------------
 *
 * DdeQueryString --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

DWORD DdeQueryString (
    DWORD idInst,	/* Instance Identifier */
    HSZ hsz,		/* Handle of string */
    LPCSTR lpsz,	/* Address of destination buffer */
    DWORD cchMax,	/* Length of buffer */
    int codepage)	/* Code page identifier, CP_WINANSI or CP_WINUNICODE */
{
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * DdeUnaccessData --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

BOOL
DdeUnaccessData (
    HDDEDATA hData)	/* Handle of DDE object */
{
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * DdeUninitialize --
 *
 *	This function frees all DDEML resources associated with
 *      the calling application.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Terminates any conversations currently open for the
 *      application.
 *
 *--------------------------------------------------------------
 */

BOOL
DdeUninitialize (
    DWORD idInst)	/* Instance Identifier */
{
    /* Check the application-instance identifier */
    TclOS2DDEML_Instance *instance;
    TclOS2DDEML_Conversation *conversation;

    for ( instance = tclOS2DDEMLInstanceListHead;
          instance != NULL && instance->idInst != idInst;
          instance = instance->nextPtr) {
        /* Nothing */
    }
    if (instance == NULL) {
        return FALSE;
    }

    /* Remove from list */
    Tcl_MutexLock(&tclOS2DDEMLMutex);
    if (instance == tclOS2DDEMLInstanceListHead) {
        tclOS2DDEMLInstanceListHead = instance->nextPtr;
    } else {
        instance->prevPtr->nextPtr = instance->nextPtr;
    }
    if (instance == tclOS2DDEMLInstanceListTail) {
        tclOS2DDEMLInstanceListTail = instance->prevPtr;
    } else {
        instance->nextPtr->prevPtr = instance->prevPtr;
    }
    Tcl_MutexUnlock(&tclOS2DDEMLMutex);

    /* Terminate all conversations open for the application */
    for ( conversation = instance->convList;
          conversation != NULL;
          /* No increment here */ ) {
        TclOS2DDEML_Conversation *curConversation = conversation;

        conversation = curConversation->nextPtr;
        /* Ignore errors */
        DdeDisconnect(curConversation->hConv);
        ckfree((char *)curConversation);
    }
    /* instance->convList = NULL; *//* Not necessary */

    /* Free memory */
    ckfree((char *)instance);
    return TRUE;
}

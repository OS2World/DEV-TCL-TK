/* 
 * tkOS2Clipboard.c --
 *
 *	This file contains functions for managing the clipboard.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-2000 by Scriptics Corporation.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkOS2Int.h"
#include "tkSelect.h"

static void     UpdateClipboard _ANSI_ARGS_((HWND hwnd));

/* Needed for signal-catching after we've become clipboard-owner */
#include <signal.h>
void sighandler(int sig);

/*
 *----------------------------------------------------------------------
 *
 * TkSelGetSelection --
 *
 *	Retrieve the specified selection from another process.  For
 *	now, only fetching XA_STRING from CLIPBOARD is supported.
 *	Eventually other types should be allowed.
 * 
 * Results:
 *	The return value is a standard Tcl return value.
 *	If an error occurs (such as no selection exists)
 *	then an error message is left in the interp's result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkSelGetSelection(interp, tkwin, selection, target, proc, clientData)
    Tcl_Interp *interp;		/* Interpreter to use for reporting
				 * errors. */
    Tk_Window tkwin;		/* Window on whose behalf to retrieve
				 * the selection (determines display
				 * from which to retrieve). */
    Atom selection;		/* Selection to retrieve. */
    Atom target;		/* Desired form in which selection
				 * is to be returned. */
    Tk_GetSelProc *proc;	/* Procedure to call to process the
				 * selection, once it has been retrieved. */
    ClientData clientData;	/* Arbitrary value to pass to proc. */
{
    char *data, *destPtr;
    Tcl_DString ds;
    Tcl_Encoding encoding;
    int result;
    ULONG formatInfo;
    APIRET rc;

#ifdef VERBOSE
    printf("TkSelGetSelection, selection %s, target %s (%s)\n",
           selection == Tk_InternAtom(tkwin, "CLIPBOARD") ? "CLIPBOARD"
                                                          : "!CLIPBOARD",
           target == XA_STRING ? "XA_STRING" : "!XA_STRING",
           Tk_GetAtomName(tkwin, target));
#endif

    if ((selection != Tk_InternAtom(tkwin, "CLIPBOARD"))
	    || (target != XA_STRING)
	    || !WinOpenClipbrd(TclOS2GetHAB())) {
        goto error;
    }

#ifdef VERBOSE
    printf("WinOpenClipbrd OK\n");
#endif
    result = TCL_ERROR;
    if (WinQueryClipbrdFmtInfo(TclOS2GetHAB(), CF_TEXT, &formatInfo)){
        ULONG currentCodePages[1], lenCodePages;
        char buf[4 + TCL_INTEGER_SPACE];

        /*
         * Determine the encoding to use to convert this text.
         */
        rc = DosQueryCp(sizeof(currentCodePages), currentCodePages,
                        &lenCodePages);
        sprintf(buf, "cp%ld", currentCodePages[0]);
        encoding = Tcl_GetEncoding(NULL, buf);

        /*
         * Fetch the text and convert it to UTF.
         */

        if (!(data= (char *)WinQueryClipbrdData(TclOS2GetHAB(), CF_TEXT))) {
#ifdef VERBOSE
            printf("WinQueryClipbrdData ERROR (or no such format, %x): %x\n",
                   data, WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
#endif
            if (encoding) {
                Tcl_FreeEncoding(encoding);
            }
            WinCloseClipbrd(TclOS2GetHAB());
            goto error;
        }
#ifdef VERBOSE
        printf("WinQueryClipbrdData OK: %s (%d)\n", data, strlen(data));
#endif
        Tcl_ExternalToUtfDString(encoding, data, -1, &ds);
        if (encoding) {
            Tcl_FreeEncoding(encoding);
        }
    } else {
#ifdef VERBOSE
        printf("WinQueryClipbrdFmtInfo ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
#endif
	WinCloseClipbrd(TclOS2GetHAB());
        goto error;
    }
    /*
     * Translate CR/LF to LF.
     */

    data = destPtr = Tcl_DStringValue(&ds);
    while (*data) {
        if (data[0] == '\r' && data[1] == '\n') {
            data++;
        } else {
            *destPtr++ = *data++;
        }
    }
    *destPtr = '\0';

    /*
     * Pass the data off to the selection procedure.
     */

    result = (*proc)(clientData, interp, Tcl_DStringValue(&ds));
    Tcl_DStringFree(&ds);
    WinCloseClipbrd(TclOS2GetHAB());
    return result;

error:
    Tcl_AppendResult(interp, Tk_GetAtomName(tkwin, selection),
	             " selection doesn't exist or form \"",
                     Tk_GetAtomName(tkwin, target),
	             "\" not defined", (char *) NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetSelectionOwner --
 *
 *	This function claims ownership of the specified selection.
 *	If the selection is CLIPBOARD, then we empty the system
 *	clipboard.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Empties the system clipboard, and claims ownership.
 *
 *----------------------------------------------------------------------
 */

void
XSetSelectionOwner(display, selection, owner, time)
    Display* display;
    Atom selection;
    Window owner;
    Time time;
{
    HWND hwnd = owner ? TkOS2GetHWND(owner) : NULLHANDLE;
    Tk_Window tkwin;

#ifdef VERBOSE
    printf("XSetSelectionOwner\n");
#endif

    /*
     * This is a gross hack because the Tk_InternAtom interface is broken.
     * It expects a Tk_Window, even though it only needs a Tk_Display.
     */

    tkwin = (Tk_Window) TkGetMainInfoList()->winPtr;

    if (selection == Tk_InternAtom(tkwin, "CLIPBOARD")) {

#ifdef VERBOSE
        printf("    selection CLIPBOARD\n");
#endif
	/*
	 * Only claim and empty the clipboard if we aren't already the
	 * owner of the clipboard.
	 */

	if (WinQueryClipbrdOwner(TclOS2GetHAB()) != hwnd) {
            UpdateClipboard(hwnd);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ClipboardRender --
 *
 *	This function supplies the contents of the clipboard in
 *	response to a WM_RENDERFMT message.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the contents of the clipboard.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2ClipboardRender(dispPtr, format)
    TkDisplay *dispPtr;
    ULONG format;
{
    TkClipboardTarget *targetPtr;
    TkClipboardBuffer *cbPtr;
    PVOID mem;
    char *buffer, *p, *rawText, *endPtr;
    int length;
    Tcl_DString ds;

/*
    if (format != CF_TEXT) {
#ifdef VERBOSE
        printf("TkOS2ClipboardRender != CF_TEXT (%x)\n", format);
#endif
        return;
    }
#ifdef VERBOSE
    printf("TkOS2ClipboardRender CF_TEXT\n");
#endif
*/

    for (targetPtr = dispPtr->clipTargetPtr; targetPtr != NULL;
	    targetPtr = targetPtr->nextPtr) {
	if (targetPtr->type == XA_STRING) {
#ifdef VERBOSE
            printf("Found target XA_STRING\n");
#endif
	    break;
        }
    }

    /*
     * Count the number of newlines so we can add space for them in
     * the resulting string.
     */

    length = 0;
    if (targetPtr != NULL) {
	for (cbPtr = targetPtr->firstBufferPtr; cbPtr != NULL;
		cbPtr = cbPtr->nextPtr) {
	    length += cbPtr->length;
            for (p = cbPtr->buffer, endPtr = p + cbPtr->length;
                    p < endPtr; p++) {
                if (*p == '\n') {
                    length++;
                }
            }
	}
    }

    /*
     * Copy the data and change EOL characters.
     */

    buffer = rawText = ckalloc(length + 1);
    if (targetPtr != NULL) {
	for (cbPtr = targetPtr->firstBufferPtr; cbPtr != NULL;
		cbPtr = cbPtr->nextPtr) {
            for (p = cbPtr->buffer, endPtr = p + cbPtr->length;
                    p < endPtr; p++) {
                if (*p == '\n') {
                    *buffer++ = '\r';
                }
                *buffer++ = *p;
            }
	}
    }
    *buffer = '\0';

    /*
     * Turn the data into the system encoding before placing it on the
     * clipboard.
     */

    Tcl_UtfToExternalDString(NULL, rawText, -1, &ds);
    ckfree(rawText);

    if ( (rc = DosAllocSharedMem(&mem, NULL, Tcl_DStringLength(&ds)+1,
              OBJ_GIVEABLE | PAG_COMMIT | PAG_READ | PAG_WRITE)) != 0) {
#ifdef VERBOSE
        printf("TkOS2ClipboardRender: DosAllocSharedMem ERROR %x\n", rc);
#endif
        Tcl_DStringFree(&ds);
	return;
    }
#ifdef VERBOSE
    printf("TkOS2ClipboardRender: DosAllocSharedMem %s (%d) OK\n", mem,
           length+1);
#endif
    buffer = (char *)mem;
    memcpy(buffer, Tcl_DStringValue(&ds), Tcl_DStringLength(&ds) + 1);
    Tcl_DStringFree(&ds);

    rc = WinSetClipbrdData(TclOS2GetHAB(), (ULONG)mem, CF_TEXT, CFI_POINTER);
#ifdef VERBOSE
    if (rc==TRUE) {
        printf("WinSetClipbrdData OK\n", mem);
    } else {
        printf("WinSetClipbrdData ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
    }
#endif

    return;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelUpdateClipboard --
 *
 *	This function is called to force the clipboard to be updated
 *	after new data is added.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears the current contents of the clipboard.
 *
 *----------------------------------------------------------------------
 */

void
TkSelUpdateClipboard(winPtr, targetPtr)
    TkWindow *winPtr;
    TkClipboardTarget *targetPtr;
{
    /*
    */
    HWND hwnd = TkOS2GetHWND(winPtr->window);
#ifdef VERBOSE
    printf("TkSelUpdateClipboard\n");
#endif
    UpdateClipboard(hwnd);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateClipboard --
 *
 *      Take ownership of the clipboard, clear it, and indicate to the
 *      system the supported formats.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateClipboard(hwnd)
    HWND hwnd;
{
    APIRET rc;

    TkOS2UpdatingClipboard(TRUE);
    rc = WinOpenClipbrd(TclOS2GetHAB());
#ifdef VERBOSE
    if (rc==TRUE) printf("WinOpenClipbrd OK\n");
    else printf("WinOpenClipBrd ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
#endif
    rc = WinEmptyClipbrd(TclOS2GetHAB());
#ifdef VERBOSE
    if (rc==TRUE) printf("WinEmptyClipbrd OK\n");
    else printf("WinEmptyClipBrd ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
#endif
    rc = WinSetClipbrdData(TclOS2GetHAB(), NULLHANDLE, CF_TEXT, CFI_POINTER);
#ifdef VERBOSE
    if (rc==TRUE) printf("WinSetClipbrdData OK\n");
    else printf("WinSetClipBrdData ERROR %x\n",WinGetLastError(TclOS2GetHAB()));
#endif
    rc = WinCloseClipbrd(TclOS2GetHAB());
#ifdef VERBOSE
    if (rc==TRUE) printf("WinCloseClipbrd OK\n");
    else printf("WinCloseClipBrd ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
#endif
    /*
     * If we've become owner of the clipboard but are terminated by
     * a signal, the whole system will be hanging waiting for it.
     */
    signal(SIGFPE, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGSEGV, sighandler);
    rc = WinSetClipbrdOwner(TclOS2GetHAB(), hwnd);
#ifdef VERBOSE
    if (rc==TRUE) printf("WinSetClipbrdOwner OK\n");
    else printf("WinSetClipBrdOwner ERROR %x\n",
                WinGetLastError(TclOS2GetHAB()));
#endif
    TkOS2UpdatingClipboard(FALSE);
}

/*
 *----------------------------------------------------------------------
 *
 * sighandler --
 *
 *	This function is invoked upon a terminating signal, so we can
 *	release the clipboard to the system (no owner).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Relinquishises ownership of the clipboard and exits.
 *
 *----------------------------------------------------------------------
 */

void sighandler(int sig)
{
    rc = WinSetClipbrdOwner(TclOS2GetHAB(), NULLHANDLE);
#ifdef VERBOSE
    if (rc==TRUE) printf("\nSIGNAL %d: WinSetClipbrdOwner OK\n", sig);
    else printf("\nSIGNAL %d: WinSetClipBrdOwner ERROR %x\n", sig,
                WinGetLastError(TclOS2GetHAB()));
#endif
    exit(1);
}

/*
 *--------------------------------------------------------------
 *
 * TkSelEventProc --
 *
 *	This procedure is invoked whenever a selection-related
 *	event occurs. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots:  depends on the type of event.
 *
 *--------------------------------------------------------------
 */

void
TkSelEventProc(tkwin, eventPtr)
    Tk_Window tkwin;		/* Window for which event was
				 * targeted. */
    register XEvent *eventPtr;	/* X event:  either SelectionClear,
				 * SelectionRequest, or
				 * SelectionNotify. */
{
#ifdef VERBOSE
printf("TkSelEventProc\n");
#endif

    if (eventPtr->type == SelectionClear) {
	TkSelClearSelection(tkwin, eventPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelPropProc --
 *
 *	This procedure is invoked when property-change events
 *	occur on windows not known to the toolkit.  This is a stub
 *	function under OS/2 Presentation Manager.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkSelPropProc(eventPtr)
    register XEvent *eventPtr;		/* X PropertyChange event. */
{
}

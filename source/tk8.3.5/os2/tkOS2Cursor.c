/* 
 * tkOS2Cursor.c --
 *
 *	This file contains OS/2 PM specific cursor related routines.
 *	Note: cursors for the mouse are called "POINTER" in Presentation
 *	Manager, those in text are "CURSOR".
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"

/*
 * The following data structure contains the system specific data
 * necessary to control OS/2 PM pointers.
 */

typedef struct {
    TkCursor info;		/* Generic cursor info used by tkCursor.c */
    HPOINTER os2Pointer;	/* OS/2 PM pointer handle */
    int system;			/* 1 if cursor is a system cursor, else 0. */
} TkOS2Cursor;

/*
 * The table below is used to map from the name of a predefined cursor
 * to its resource identifier.
 */

static struct CursorName {
    char *name;
    LONG id;
} cursorNames[] = {
/*    {"X_cursor",		SPTR_ARROW}, */
    {"arrow",			SPTR_ARROW},
/*    {"based_arrow_down",	SPTR_SIZENS}, */
/*    {"based_arrow_up",		SPTR_SIZENS}, */
/*    {"bottom_left_corner",	SPTR_SIZENESW}, */
/*    {"bottom_right_corner",	SPTR_SIZENWSE}, */
/*    {"bottom_side",		SPTR_SIZENS}, */
/*    {"bottom_tee",		SPTR_SIZENS}, */
/*    {"center_ptr",		SPTR_MOVE}, */
/*    {"clock",			SPTR_WAIT}, */
/*    {"cross",			SPTR_ARROW}, */
/*    {"cross_reverse",		SPTR_ARROW}, */
/*    {"crosshair",		SPTR_TEXT}, */
/*    {"diamond_cross",		SPTR_ARROW}, */
    {"double_arrow",		SPTR_SIZEWE},
    {"fleur",			SPTR_MOVE},
    {"ibeam",			SPTR_TEXT},
    {"left_ptr",		SPTR_ARROW},
    {"left_side",		SPTR_SIZEWE},
    {"left_tee",		SPTR_SIZEWE},
/*    {"mouse",			SPTR_ARROW}, */
    {"no",			SPTR_ILLEGAL},
/*    {"plus",			SPTR_ARROW}, */
/*    {"question_arrow",		SPTR_QUESICON}, */
/*    {"right_ptr",		SPTR_SIZEWE}, */
/*    {"right_side",		SPTR_SIZEWE}, */
/*    {"right_tee",		SPTR_SIZEWE}, */
/*    {"sb_down_arrow",		SPTR_SIZENS}, */
    {"sb_h_double_arrow",	SPTR_SIZEWE},
/*    {"sb_left_arrow",		SPTR_SIZEWE}, */
/*    {"sb_right_arrow",		SPTR_SIZEWE}, */
/*    {"sb_up_arrow",		SPTR_SIZENS}, */
    {"sb_v_double_arrow",	SPTR_SIZENS},
    {"size_nw_se",		SPTR_SIZENWSE},
    {"size_ne_sw",		SPTR_SIZENESW},
    {"size",			SPTR_MOVE},
    {"starting",		SPTR_WAIT},
/*    {"target",			SPTR_SIZE}, */
/*    {"tcross",			SPTR_ARROW}, */
/*    {"top_left_arrow",		SPTR_ARROW}, */
/*    {"top_left_corner",		SPTR_SIZENWSE}, */
/*    {"top_right_corner",	SPTR_SIZENESW}, */
/*    {"top_side",		SPTR_SIZENS}, */
/*    {"top_tee",			SPTR_SIZENS}, */
/*    {"ul_angle",		SPTR_SIZENWSE}, */
    {"uparrow",			SPTR_SIZENS},
/*    {"ur_angle",		SPTR_SIZENESW}, */
    {"watch",			SPTR_WAIT},
    {"wait",			SPTR_WAIT},
    {"xterm",			SPTR_TEXT},
    /* cursors without moderately reasonable equivalents: question mark icon */
/*
    {"boat",			SPTR_QUESICON},
    {"bogosity",		SPTR_QUESICON},
    {"box_spiral",		SPTR_QUESICON},
    {"circle",			SPTR_QUESICON},
    {"coffee_mug",		SPTR_QUESICON},
    {"dot",			SPTR_QUESICON},
    {"dotbox",			SPTR_QUESICON},
    {"draft_large",		SPTR_QUESICON},
    {"draft_small",		SPTR_QUESICON},
    {"draped_box",		SPTR_QUESICON},
    {"exchange",		SPTR_QUESICON},
    {"gobbler",			SPTR_QUESICON},
    {"gumby",			SPTR_QUESICON},
    {"hand1",			SPTR_QUESICON},
    {"hand2",			SPTR_QUESICON},
    {"heart",			SPTR_QUESICON},
    {"icon",			SPTR_QUESICON},
    {"iron_cross",		SPTR_QUESICON},
    {"leftbutton",		SPTR_QUESICON},
    {"ll_angle",		SPTR_QUESICON},
    {"lr_angle",		SPTR_QUESICON},
    {"man",			SPTR_QUESICON},
    {"middlebutton",		SPTR_QUESICON},
    {"pencil",			SPTR_QUESICON},
    {"pirate",			SPTR_QUESICON},
    {"rightbutton",		SPTR_QUESICON},
    {"rtl_logo",		SPTR_QUESICON},
    {"sailboat",		SPTR_QUESICON},
    {"shuttle",			SPTR_QUESICON},
    {"spider",			SPTR_QUESICON},
    {"spraycan",		SPTR_QUESICON},
    {"star",			SPTR_QUESICON},
    {"trek",			SPTR_QUESICON},
    {"umbrella",		SPTR_QUESICON},
*/
    {NULL,			0}
};
/* Include cursors; done by Ilya Zakharevich */
#include "rc/cursors.h"

/*
 * The default cursor is used whenever no other cursor has been specified.
 */

#define TK_DEFAULT_CURSOR	SPTR_ARROW
static HPOINTER defCursor = NULLHANDLE;


/*
 *----------------------------------------------------------------------
 *
 * TkGetCursorByName --
 *
 *	Retrieve a system cursor by name.  
 *
 * Results:
 *	Returns a new cursor, or NULL on errors.  
 *
 * Side effects:
 *	Allocates a new cursor.
 *
 *----------------------------------------------------------------------
 */

TkCursor *
TkGetCursorByName(interp, tkwin, string)
    Tcl_Interp *interp;		/* Interpreter to use for error reporting. */
    Tk_Window tkwin;		/* Window in which cursor will be used. */
    Tk_Uid string;		/* Description of cursor.  See manual entry
				 * for details on legal syntax. */
{
    struct CursorName *namePtr;
    TkOS2Cursor *cursorPtr;

#ifdef VERBOSE
    printf("TkGetCursorByName %s\n", string);
#endif

    /*
     * Check for the cursor in the system cursor set.
     */

    for (namePtr = cursorNames; namePtr->name != NULL; namePtr++) {
	if (strcmp(namePtr->name, string) == 0) {
	    break;
	}
    }

    cursorPtr = (TkOS2Cursor *) ckalloc(sizeof(TkOS2Cursor));
    if (!cursorPtr) {
	return NULL;
    }

    cursorPtr->info.cursor = (Tk_Cursor) cursorPtr;
    cursorPtr->os2Pointer = NULLHANDLE;

    if (namePtr->name != NULL) {
	/* Found a system cursor, make a reference (not a copy) */
	cursorPtr->os2Pointer = WinQuerySysPointer(HWND_DESKTOP, namePtr->id,
	                                           FALSE);
	cursorPtr->system = 1;
    }
    if (cursorPtr->os2Pointer == NULLHANDLE) {
	/* Variable cursors comes from rc/cursors.h */
	myCursor *curPtr = cursors;

	/* Added X-derived cursors by Ilya Zakharevich */
	cursorPtr->system = 0;
	while (curPtr->name) {
	    if (strcmp(curPtr->name, string) == 0) {
	        break;
	    }
	    curPtr++;
	}
	if (curPtr->name &&
	    (cursorPtr->os2Pointer = WinLoadPointer(HWND_DESKTOP,
	                                            Tk_GetHMODULE(),
	                                            curPtr->id))
            != NULLHANDLE) {
            cursorPtr->system = 0;
        } else {
	    /* Not a known cursor */
	    cursorPtr->os2Pointer = NULLHANDLE;
	    cursorPtr->system = 1;
        }
    }
#ifdef VERBOSE
    printf("    cursorPtr->os2Pointer %x\n", cursorPtr->os2Pointer);
#endif
    if (cursorPtr->os2Pointer == NULLHANDLE) {
        badCursorSpec:
	ckfree((char *)cursorPtr);
	Tcl_AppendResult(interp, "bad cursor spec \"", string, "\"",
		(char *) NULL);
	return NULL;
    } else {
	return (TkCursor *) cursorPtr;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkCreateCursorFromData --
 *
 *	Creates a cursor from the source and mask bits.
 *
 * Results:
 *	Returns a new cursor, or NULL on errors.
 *
 * Side effects:
 *	Allocates a new cursor.
 *
 *----------------------------------------------------------------------
 */

TkCursor *
TkCreateCursorFromData(tkwin, source, mask, width, height, xHot, yHot,
	fgColor, bgColor)
    Tk_Window tkwin;		/* Window in which cursor will be used. */
    char *source;		/* Bitmap data for cursor shape. */
    char *mask;			/* Bitmap data for cursor mask. */
    int width, height;		/* Dimensions of cursor. */
    int xHot, yHot;		/* Location of hot-spot in cursor. */
    XColor fgColor;		/* Foreground color for cursor. */
    XColor bgColor;		/* Background color for cursor. */
{
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeCursor --
 *
 *	This procedure is called to release a cursor allocated by
 *	TkGetCursorByName.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Non-system pointers are destroyed. Deallocating the cursor data
 *      structure is done by the calling function when the reference count
 *      has reached 0.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeCursor(cursorPtr)
    TkCursor *cursorPtr;
{
    TkOS2Cursor *os2PointerPtr = (TkOS2Cursor *) cursorPtr;
    if (os2PointerPtr->system != 1) {
        rc = WinDestroyPointer(os2PointerPtr->os2Pointer);
#ifdef VERBOSE
        printf("TkFreeCursor %x, WinDestroyPointer %x returns %d\n", cursorPtr,
               os2PointerPtr->os2Pointer, rc);
    } else {
        printf("TkFreeCursor %x, system pointer %x\n", cursorPtr,
               os2PointerPtr->os2Pointer);
#endif
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCursor --
 *
 *	Set the global cursor. If the cursor is None, then use the
 *	default Tk cursor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the mouse cursor.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetCursor(cursor)
    TkpCursor cursor;
{
    HPOINTER hcursor = NULLHANDLE;
    TkOS2Cursor *os2Cursor = (TkOS2Cursor *) cursor;

#ifdef VERBOSE
    printf("TkOS2UpdateCursor os2Cursor %x, os2Pointer %x\n", os2Cursor,
           os2Cursor == NULL ? 0 : os2Cursor->os2Pointer);
#endif

    if (os2Cursor == NULL || os2Cursor->os2Pointer == NULLHANDLE) {
        if (defCursor == NULLHANDLE) {
            defCursor = WinQuerySysPointer(HWND_DESKTOP, TK_DEFAULT_CURSOR,
                                           FALSE);
        }
        hcursor = defCursor;
    } else {
        hcursor = os2Cursor->os2Pointer;
    }
#ifdef VERBOSE
    printf("    hcursor %x, old pointer %x\n", hcursor,
           WinQueryPointer(HWND_DESKTOP));
#endif
    if (hcursor != NULLHANDLE && hcursor != WinQueryPointer(HWND_DESKTOP)) {
	rc = WinSetPointer(HWND_DESKTOP, hcursor);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("    WinSetPointer(%x) ERROR\n", hcursor,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("    WinSetPointer(%x) OK\n", hcursor);
        }
#endif
    }
}

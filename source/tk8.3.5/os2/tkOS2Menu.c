/*
 * tkOS2Menu.c --
 *
 *	This module implements the OS/2 platform-specific features of menus.
 *
 * Copyright (c) 1996-1998 by Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 by Scriptics Corporation.
 * Copyright (c) 1999-2003 by Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tkOS2Int.h"
#include "tkMenu.h"

/* Get to thread local data of tkOS2Color.c */
#include "tkOS2Color.h"

#include <string.h>

/*
 * The class of the window for popup menus.
 */

#define MENU_CLASS_NAME "MenuWindowClass"

/*
 * Used to align an OS/2 bitmap inside a rectangle
 */

#define ALIGN_BITMAP_LEFT   0x00000001
#define ALIGN_BITMAP_RIGHT  0x00000002
#define ALIGN_BITMAP_TOP    0x00000004
#define ALIGN_BITMAP_BOTTOM 0x00000008

/*
 * Platform-specific menu flags:
 *
 * MENU_SYSTEM_MENU	Non-zero means that the OS/2 menu handle
 *			was retrieved with GetSystemMenu and needs
 *			to be disposed of specially.
 * MENU_RECONFIGURE_PENDING
 *			Non-zero means that an idle handler has
 *			been set up to reconfigure the OS/2 menu
 *			handle for this menu.
 */

#define MENU_SYSTEM_MENU	    MENU_PLATFORM_FLAG1
#define MENU_RECONFIGURE_PENDING    MENU_PLATFORM_FLAG2

static PFNWP oldMenuProc = WinDefWindowProc; /* window proc of MENU controls */

static int indicatorDimensions[2] = { 0, 0};
				/* The dimensions of the indicator space
				 * in a menu entry. Calculated at init
				 * time to save time. */
typedef struct MenuThreadSpecificData {
    Tcl_HashTable commandTable; /* A map of command ids to menu entries */
    int inPostMenu;		/* We cannot be re-entrant like X Windows. */
    USHORT lastCommandID;	/* The last command ID we allocated. */
    HWND menuHWND;		/* A window to service popup-menu messages
				 * in. */
    int oldServiceMode;         /* Used while processing a menu; we need
				 * to set the event mode specially when we
				 * enter the menu processing modal loop
				 * and reset it when menus go away. */
    TkMenu *modalMenuPtr;	/* The menu we are processing inside the modal
				 * loop. We need this to reset all of the
				 * active items when menus go away since
				 * OS/2 does not see fit to give this
				 * to us when it sends its WM_MENUSELECT. */
    Tcl_HashTable os2MenuTable; /* Need this to map HWNDs back to menuPtrs */
} MenuThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * The following are default menu value strings.
 */

static int defaultBorderWidth;  /* The OS/2 default border width. */
static Tcl_DString menuFontDString;
				/* A buffer to store the default menu font
				 * string. */
/*
 * Forward declarations for procedures defined later in this file:
 * y coordinates are X11 not PM.
 */

static void		DrawMenuEntryAccelerator _ANSI_ARGS_((
			    TkMenu *menuPtr, TkMenuEntry *mePtr,
			    Drawable d, GC gc, Tk_Font tkfont,
			    CONST Tk_FontMetrics *fmPtr,
			    Tk_3DBorder activeBorder, int x, int y,
			    int width, int height, int drawArrow));
static void		DrawMenuEntryBackground _ANSI_ARGS_((
			    TkMenu *menuPtr, TkMenuEntry *mePtr,
			    Drawable d, Tk_3DBorder activeBorder,
			    Tk_3DBorder bgBorder, int x, int y,
			    int width, int heigth));
static void		DrawMenuEntryIndicator _ANSI_ARGS_((
			    TkMenu *menuPtr, TkMenuEntry *mePtr,
			    Drawable d, GC gc, GC indicatorGC,
			    Tk_Font tkfont,
			    CONST Tk_FontMetrics *fmPtr, int x, int y,
			    int width, int height));
static void		DrawMenuEntryLabel _ANSI_ARGS_((
			    TkMenu * menuPtr, TkMenuEntry *mePtr, Drawable d,
			    GC gc, Tk_Font tkfont,
			    CONST Tk_FontMetrics *fmPtr, int x, int y,
			    int width, int height));
static void		DrawMenuSeparator _ANSI_ARGS_((TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d, GC gc,
			    Tk_Font tkfont, CONST Tk_FontMetrics *fmPtr,
			    int x, int y, int width, int height));
static void		DrawTearoffEntry _ANSI_ARGS_((TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d, GC gc,
			    Tk_Font tkfont, CONST Tk_FontMetrics *fmPtr,
			    int x, int y, int width, int height));
static void		DrawMenuUnderline _ANSI_ARGS_((TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d, GC gc,
			    Tk_Font tkfont, CONST Tk_FontMetrics *fmPtr, int x,
			    int y, int width, int height));
/*
static void		DrawOS2SystemBitmap _ANSI_ARGS_((
			    Display *display, Drawable drawable,
			    GC gc, CONST RECTL *rectPtr, int bitmapID,
			    int alignFlags));
*/
static void		FreeID _ANSI_ARGS_((int commandID));
static char *		GetEntryText _ANSI_ARGS_((TkMenuEntry *mePtr));
static void		GetMenuAccelGeometry _ANSI_ARGS_((TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Tk_Font tkfont,
			    CONST Tk_FontMetrics *fmPtr, int *widthPtr,
			    int *heightPtr));
static void		GetMenuLabelGeometry _ANSI_ARGS_((TkMenuEntry *mePtr,
			    Tk_Font tkfont, CONST Tk_FontMetrics *fmPtr,
			    int *widthPtr, int *heightPtr));
static void		GetMenuIndicatorGeometry _ANSI_ARGS_((
			    TkMenu *menuPtr, TkMenuEntry *mePtr,
			    Tk_Font tkfont, CONST Tk_FontMetrics *fmPtr,
			    int *widthPtr, int *heightPtr));
static void		GetMenuSeparatorGeometry _ANSI_ARGS_((
			    TkMenu *menuPtr, TkMenuEntry *mePtr,
			    Tk_Font tkfont, CONST Tk_FontMetrics *fmPtr,
			    int *widthPtr, int *heightPtr));
static void		GetTearoffEntryGeometry _ANSI_ARGS_((TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Tk_Font tkfont,
			    CONST Tk_FontMetrics *fmPtr, int *widthPtr,
			    int *heightPtr));
static int		GetNewID _ANSI_ARGS_((TkMenuEntry *mePtr,
			    int *menuIDPtr));
static void		MenuSelectEvent _ANSI_ARGS_((TkMenu *menuPtr));
static void		ReconfigureOS2Menu _ANSI_ARGS_((
			    ClientData clientData));
static void		RecursivelyClearActiveMenu _ANSI_ARGS_((
			    TkMenu *menuPtr));
static void             TkOS2MenuSetDefaults _ANSI_ARGS_((int firstTime));
static MRESULT EXPENTRY	TkOS2MenuProc _ANSI_ARGS_((HWND hwnd,
			    ULONG message, MPARAM param1,
			    MPARAM param2));



/*
 *----------------------------------------------------------------------
 *
 * GetNewID --
 *
 *	Allocates a new menu id and marks it in use.
 *
 * Results:
 *	Returns TCL_OK if succesful; TCL_ERROR if there are no more
 *	ids of the appropriate type to allocate. menuIDPtr contains
 *	the new id if succesful.
 *
 * Side effects:
 *	An entry is created for the menu in the command hash table,
 *	and the hash entry is stored in the appropriate field in the
 *	menu data structure.
 *
 *----------------------------------------------------------------------
 */

static int
GetNewID(mePtr, menuIDPtr)
    TkMenuEntry *mePtr;		/* The menu we are working with */
    int *menuIDPtr;		/* The resulting id */
{
    int found = 0;
    int newEntry;
    Tcl_HashEntry *commandEntryPtr = (Tcl_HashEntry *) NULL;
    USHORT returnID = 0;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

    USHORT curID = tsdPtr->lastCommandID + 1;

#ifdef VERBOSE
    printf("GetNewID\n");
    fflush(stdout);
#endif

    /*
     * The following code relies on USHORT wrapping when the highest value is
     * incremented.
     */

    while (curID != tsdPtr->lastCommandID) {
	/*
	 * Extra cast to ULONG to prevent warning by GCC about casting from
	 * integer of different size.
	 */
    	commandEntryPtr = Tcl_CreateHashEntry(&tsdPtr->commandTable,
		(char *)(ULONG) curID, &newEntry);
    	if (newEntry == 1) {
    	    found = 1;
    	    returnID = curID;
    	    break;
    	}
    	curID++;
    }

    if (found) {
    	Tcl_SetHashValue(commandEntryPtr, (char *) mePtr);
    	*menuIDPtr = (int) returnID;
    	tsdPtr->lastCommandID = returnID;
#ifdef VERBOSE
        printf("GetNewID TCL_OK returning id %d (mePtr %x, hwnd %x)\n",
               returnID, mePtr, mePtr->menuPtr->platformData);
        fflush(stdout);
#endif
    	return TCL_OK;
    } else {
#ifdef VERBOSE
        printf("GetNewID TCL_ERROR\n");
        fflush(stdout);
#endif
    	return TCL_ERROR;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FreeID --
 *
 *	Marks the itemID as free.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The hash table entry for the ID is cleared.
 *
 *----------------------------------------------------------------------
 */

static void
FreeID(commandID)
    int commandID;
{
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

    Tcl_HashEntry *entryPtr = Tcl_FindHashEntry(&tsdPtr->commandTable,
	    (char *) commandID);

#ifdef VERBOSE
    printf("FreeID %d\n", commandID);
    fflush(stdout);
#endif
    if (entryPtr != NULL) {
    	 Tcl_DeleteHashEntry(entryPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpNewMenu --
 *
 *	Gets a new blank menu. Only the platform specific options are filled
 *	in.
 *
 * Results:
 *	Standard TCL error.
 *
 * Side effects:
 *	Allocates an OS/2 menu handle and places it in the platformData
 *	field of the menuPtr.
 *
 *----------------------------------------------------------------------
 */

int
TkpNewMenu(menuPtr)
    TkMenu *menuPtr;	/* The common structure we are making the
			 * platform structure for. */
{
    HWND os2MenuHdl;
    Tcl_HashEntry *hashEntryPtr;
    int newEntry;
    FRAMECDATA fcdata;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

#ifdef VERBOSE
    printf("TkpNewMenu\n");
    fflush(stdout);
#endif
    fcdata.cb = sizeof(FRAMECDATA);
    fcdata.flCreateFlags = 0L;
    fcdata.hmodResources = 0L;
    fcdata.idResources = 0;
    os2MenuHdl = WinCreateWindow(HWND_DESKTOP, WC_MENU, NULL,
                                 WS_CLIPSIBLINGS | WS_SAVEBITS, 0, 0, 0, 0,
                                 HWND_DESKTOP, HWND_BOTTOM, FID_MENU, &fcdata,
                                 NULL);
/*
    os2MenuHdl = WinCreateWindow(HWND_OBJECT, WC_MENU, NULL,
                                 WS_CLIPSIBLINGS | WS_SAVEBITS, 0, 0, 0, 0,
                                 HWND_OBJECT, HWND_BOTTOM, FID_MENU, &fcdata,
                                 NULL);
*/

    if (os2MenuHdl == NULLHANDLE) {
#ifdef VERBOSE
        printf("WinCreateWindow menu ERROR %x\n",
	       WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
#endif
    	Tcl_AppendResult(menuPtr->interp, "No more menus can be allocated.",
    		(char *) NULL);
    	return TCL_ERROR;
    }
#ifdef VERBOSE
    printf("WinCreateWindow menu OK: %x\n", os2MenuHdl);
            fflush(stdout);
#endif
    oldMenuProc = WinSubclassWindow(os2MenuHdl, TkOS2MenuProc);
#ifdef VERBOSE
    printf("WinSubclassWindow os2MenuHdl %x, oldMenuProc %x\n", os2MenuHdl,
           oldMenuProc);
    fflush(stdout);
#endif
/*
*/

    /*
     * We hash all of the HWND's so that we can get their menu ptrs
     * back when dispatch messages.
     */

    hashEntryPtr = Tcl_CreateHashEntry(&tsdPtr->os2MenuTable,
                                       (char *) os2MenuHdl, &newEntry);
#ifdef VERBOSE
    printf("Tcl_CreateHashEntry hwnd %x for menuPtr %x: hashEntryPtr %x\n",
               os2MenuHdl, menuPtr, hashEntryPtr);
#endif
    Tcl_SetHashValue(hashEntryPtr, (char *) menuPtr);

    menuPtr->platformData = (TkMenuPlatformData) os2MenuHdl;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyMenu --
 *
 *	Destroys platform-specific menu structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All platform-specific allocations are freed up.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyMenu(menuPtr)
    TkMenu *menuPtr;	    /* The common menu structure */
{
    HWND os2MenuHdl = (HWND) menuPtr->platformData;
    char *searchName;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));
#ifdef VERBOSE
    printf("TkpDestroyMenu %x\n", os2MenuHdl);
            fflush(stdout);
#endif

    if (menuPtr->menuFlags & MENU_RECONFIGURE_PENDING) {
	Tcl_CancelIdleCall(ReconfigureOS2Menu, (ClientData) menuPtr);
    }

    if (os2MenuHdl == NULLHANDLE) {
	return;
    }

    if (menuPtr->menuFlags & MENU_SYSTEM_MENU) {
	TkMenuEntry *searchEntryPtr;
	Tcl_HashTable *tablePtr = TkGetMenuHashTable(menuPtr->interp);
	char *menuName = Tcl_GetHashKey(tablePtr,
		menuPtr->menuRefPtr->hashEntryPtr);

	/*
	 * Search for the menu in the menubar, if it is present, get the
	 * wrapper window associated with the toplevel and reset its
	 * system menu to the default menu.
	 */

	for (searchEntryPtr = menuPtr->menuRefPtr->parentEntryPtr;
	     searchEntryPtr != NULL;
	     searchEntryPtr = searchEntryPtr->nextCascadePtr) {
            searchName = Tcl_GetStringFromObj(searchEntryPtr->namePtr, NULL);
	    if (strcmp(searchName, menuName) == 0) {
		Tk_Window parentTopLevelPtr = 
		    searchEntryPtr->menuPtr->parentTopLevelPtr;

		if (parentTopLevelPtr != NULL) {
		    /*
		     * According to SMART, there is no documented way to
		     * reset the system menu to the default state...
		     * Windows uses:
		    GetSystemMenu(TkOS2GetWrapperWindow(parentTopLevelPtr),
			    TRUE);
		     */
		}
		break;
	    }
	}
    } else {
	Tcl_HashEntry *hashEntryPtr;

	/*
	 * Remove the menu from the menu hash table, then destroy the handle.
	 */

	hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->os2MenuTable,
                                         (char *) os2MenuHdl);
	if (hashEntryPtr != NULL) {
	    Tcl_DeleteHashEntry(hashEntryPtr);
	}
	WinDestroyWindow(os2MenuHdl);
    }
    menuPtr->platformData = NULL;

    if (menuPtr == tsdPtr->modalMenuPtr) {
        tsdPtr->modalMenuPtr = NULL;
    }
    /* Reset lastWinPtr etc.????
    TkPointerDeadWindow((TkWindow *)menuPtr->tkwin);
    */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyMenuEntry --
 *
 *	Cleans up platform-specific menu entry items.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	All platform-specific allocations are freed up.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyMenuEntry(mePtr)
    TkMenuEntry *mePtr;		    /* The entry to destroy */
{
    TkMenu *menuPtr = mePtr->menuPtr;
    HWND os2MenuHdl = (HWND) menuPtr->platformData;
#ifdef VERBOSE
    printf("TkpDestroyMenuEntry %x (mePtr %x, menuPtr %x)\n", os2MenuHdl, mePtr,
           menuPtr);
            fflush(stdout);
#endif

    if (NULLHANDLE != os2MenuHdl) {
        if (!(menuPtr->menuFlags & MENU_RECONFIGURE_PENDING)) {
	    menuPtr->menuFlags |= MENU_RECONFIGURE_PENDING;
	    Tcl_DoWhenIdle(ReconfigureOS2Menu, (ClientData) menuPtr);
	}
    }
    FreeID((int) mePtr->platformEntryData);
    mePtr->platformEntryData = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetEntryText --
 *
 *	Given a menu entry, gives back the text that should go in it.
 *	Separators should be done by the caller, as they have to be
 *	handled specially. Allocates the memory with alloc. The caller
 *	should free the memory.
 *
 * Results:
 *	itemText points to the new text for the item.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
GetEntryText(mePtr)
    TkMenuEntry *mePtr;		/* A pointer to the menu entry. */
{
    char *itemText;
#ifdef VERBOSE
    printf("GetEntryText mePtr %x\n", mePtr);
            fflush(stdout);
#endif

    if (mePtr->type == TEAROFF_ENTRY) {
	itemText = ckalloc(sizeof("(Tear-off)"));
	strcpy(itemText, "(Tear-off)");
    } else if (mePtr->imagePtr != NULL) {
	itemText = ckalloc(sizeof("(Image)"));
	strcpy(itemText, "(Image)");
    } else if (mePtr->bitmapPtr != None) {
	itemText = ckalloc(sizeof("(Pixmap)"));
	strcpy(itemText, "(Pixmap)");
    } else if (mePtr->labelPtr == NULL || mePtr->labelLength == 0) {
	itemText = ckalloc(sizeof("( )"));
	strcpy(itemText, "( )");
    } else {
        int i;
        char *label = (mePtr->labelPtr == NULL) ? ""
                : Tcl_GetStringFromObj(mePtr->labelPtr, NULL);
        char *accel = (mePtr->accelPtr == NULL) ? ""
                : Tcl_GetStringFromObj(mePtr->accelPtr, NULL);
        char *p, *next;
        Tcl_DString itemString;
        BOOL beforeTilde = TRUE;

        /*
         * We have to construct the string with a tilde (~)
         * preceeding the underline character (to get OS/2 PM to underline it),
         * and a tab seperating the text and the accel text. We have to be
         * careful with tildes in the string.  Escaping the tilde is done by
         * another tilde, but ONLY if the literal tilde comes for any
         * 'underline' / 'mnemonic' tilde.  Every tilde after that is taken
         * literally by the menu control.
         */

        Tcl_DStringInit(&itemString);

        for (p = label, i = 0; *p != '\0'; i++, p = next) {
            if (i == mePtr->underline) {
                Tcl_DStringAppend(&itemString, "~", 1);
            }
            if (*p == '~') {
                Tcl_DStringAppend(&itemString, "~", 1);
                beforeTilde = FALSE;
            }
            next = Tcl_UtfNext(p);
            Tcl_DStringAppend(&itemString, p, next - p);
        }
        if (mePtr->accelLength > 0) {
            Tcl_DStringAppend(&itemString, "\t", 1);
            for (p = accel, i = 0; *p != '\0'; i++, p = next) {
                if (*p == '~') {
                    Tcl_DStringAppend(&itemString, "~", 1);
                }
                next = Tcl_UtfNext(p);
                Tcl_DStringAppend(&itemString, p, next - p);
            }
        }

        itemText = ckalloc(Tcl_DStringLength(&itemString) + 1);
        strcpy(itemText, Tcl_DStringValue(&itemString));
        Tcl_DStringFree(&itemString);
    }
#ifdef VERBOSE
    printf("GetEntryText returning [%s]\n", itemText);
            fflush(stdout);
#endif
    return itemText;
}

/*
 *----------------------------------------------------------------------
 *
 * ReconfigureOS2Menu --
 *
 *	Tears down and rebuilds the platform-specific part of this menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Configuration information get set for mePtr; old resources
 *	get freed, if any need it.
 *
 *----------------------------------------------------------------------
 */

static void
ReconfigureOS2Menu(
    ClientData clientData)	    /* The menu we are rebuilding */
{
    TkMenu *menuPtr = (TkMenu *) clientData;
    TkMenuEntry *mePtr;
    HWND os2MenuHdl = (HWND) menuPtr->platformData;
    char *itemText = NULL;
    PSZ pszNewItem;
    USHORT flags, attrs;
    USHORT itemID, base;
    int i, count, systemMenu = 0;
    int width = 0, height = 0;
    Tcl_DString translatedText;
    MENUITEM menuItem;


#ifdef VERBOSE
    printf("ReconfigureOS2Menu %x (menuPtr %x)\n", os2MenuHdl, menuPtr);
            fflush(stdout);
#endif
    if (os2MenuHdl == NULLHANDLE) {
    	return;
    }

    /*
     * Reconstruct the entire menu. Takes care of nasty system menu and index
     * problem.
     *
     */

    if ((menuPtr->menuType == MENUBAR)
	    && (menuPtr->parentTopLevelPtr != NULL)) {
	width = Tk_Width(menuPtr->parentTopLevelPtr);
	height = Tk_Height(menuPtr->parentTopLevelPtr);
    }

    base = (menuPtr->menuFlags & MENU_SYSTEM_MENU) ? 8 : 0;
    count = (int) WinSendMsg(os2MenuHdl, MM_QUERYITEMCOUNT,
                             MPFROMLONG(0), MPFROMLONG(0));
#ifdef VERBOSE
    printf("ReconfigureOS2Menu %x: base %d, count %d\n", os2MenuHdl,base,count);
            fflush(stdout);
#endif
    for (i = base; i < count; i++) {
        /*
         * RemoveMenu MF_BYPOSITION
         * Extra cast to ULONG to prevent warning by GCC about casting
         * from integer of different size.
         */
	SHORT id = (SHORT)(ULONG) WinSendMsg(os2MenuHdl, MM_ITEMIDFROMPOSITION,
	                                     MPFROMSHORT(base), MPFROMLONG(0));
	rc = (LONG) WinSendMsg(os2MenuHdl, MM_REMOVEITEM,
                               MPFROM2SHORT(id, FALSE), MPVOID);
#ifdef VERBOSE
        printf("ReconfigureOS2Menu %x MM_REMOVEITEM item %d id %d returns %d\n",
               os2MenuHdl, i, id, rc);
        fflush(stdout);
#endif
    }

    count = menuPtr->numEntries;
#ifdef VERBOSE
    printf("ReconfigureOS2Menu %x: count now %d\n", os2MenuHdl, count);
            fflush(stdout);
#endif
    for (i = 0; i < count; i++) {
	mePtr = menuPtr->entries[i];
	pszNewItem = NULL;
	flags = 0;
	attrs = 0;
	itemID = 0;
	menuItem.hwndSubMenu = NULLHANDLE;
        Tcl_DStringInit(&translatedText);
#ifdef VERBOSE
        printf("ReconfigureOS2Menu %x ID %d menuPtr %x mePtr %x %s %s\n",
               os2MenuHdl, i, menuPtr, mePtr,
               menuPtr->menuType == MENUBAR ? "MENUBAR" :
               (menuPtr->menuType == TEAROFF_MENU ? "TEAROFF_MENU" :
               (menuPtr->menuType == MASTER_MENU ? "MASTER_MENU": "OTHER")),
               mePtr->type == COMMAND_ENTRY ? "COMMAND_ENTRY" :
               (mePtr->type == SEPARATOR_ENTRY ? "SEPARATOR_ENTRY" :
               (mePtr->type == CHECK_BUTTON_ENTRY ? "CHECK_BUTTON_ENTRY" :
               (mePtr->type == RADIO_BUTTON_ENTRY ? "RADIO_BUTTON_ENTRY" :
               (mePtr->type == CASCADE_ENTRY ? "CASCADE_ENTRY" :
               (mePtr->type == TEAROFF_ENTRY ? "TEAROFF_ENTRY" : "TEXT"))))));
            fflush(stdout);
#endif

	if ((menuPtr->menuType == MENUBAR) && (mePtr->type == TEAROFF_ENTRY)) {
	    continue;
	}

        itemText = GetEntryText(mePtr);
        if ((menuPtr->menuType == MENUBAR)
                || (menuPtr->menuFlags & MENU_SYSTEM_MENU)) {
            Tcl_UtfToExternalDString(NULL, itemText, -1, &translatedText);
            pszNewItem = Tcl_DStringValue(&translatedText);
	} else {
            pszNewItem = (PSZ) mePtr;
            flags |= MIS_OWNERDRAW;
        }

        /*
    	 * Set enabling and disabling correctly.
    	 */

	if (mePtr->state == ENTRY_DISABLED) {
#ifdef VERBOSE
            printf("MIA_DISABLED\n");
            fflush(stdout);
#endif
	    attrs |= MIA_DISABLED;
	}
    	
    	/*
    	 * Set the check mark for check entries and radio entries.
    	 */
	
	if (((mePtr->type == CHECK_BUTTON_ENTRY)
	        || (mePtr->type == RADIO_BUTTON_ENTRY))
	        && (mePtr->entryFlags & ENTRY_SELECTED)) {
#ifdef VERBOSE
            printf("MIA_CHECKED\n");
            fflush(stdout);
#endif
	    attrs |= MIA_CHECKED;
	}

        /*
         * Set the SEPARATOR bit for separator entries.  This bit is not
         * used by our internal drawing functions, but it is used by the
         * system when drawing the system menu (we do not draw the system menu
         * ourselves).  If this bit is not set, separator entries on the system
         * menu will not be drawn correctly.
         */

        if (mePtr->type == SEPARATOR_ENTRY) {
#ifdef VERBOSE
            printf("MIS_SEPARATOR\n");
            fflush(stdout);
#endif
            flags |= MIS_SEPARATOR;
        }

	if (mePtr->columnBreak) {
#ifdef VERBOSE
            printf("MIS_BREAK\n");
            fflush(stdout);
#endif
	    flags |= MIS_BREAK;
	}

        /*
         * Extra cast to ULONG to prevent warning by GCC about casting
         * from integer of different size.
         */
	itemID = (USHORT)(ULONG) mePtr->platformEntryData;
#ifdef VERBOSE
        printf("itemID %x\n", itemID);
        fflush(stdout);
#endif
	if ((mePtr->type == CASCADE_ENTRY)
	       && (mePtr->childMenuRefPtr != NULL)
	       && (mePtr->childMenuRefPtr->menuPtr != NULL)) {
	    HWND childMenuHdl =
                           (HWND) mePtr->childMenuRefPtr->menuPtr->platformData;
#ifdef VERBOSE
            printf("CASCADE_ENTRY %x\n", childMenuHdl);
            fflush(stdout);
#endif
	    menuItem.hwndSubMenu = NULLHANDLE;
	    if (childMenuHdl != NULLHANDLE) {
/*
		itemID = (USHORT) childMenuHdl;
*/
	        menuItem.hwndSubMenu = childMenuHdl;
#ifdef VERBOSE
                printf("MIS_SUBMENU itemID %x childMenuHdl %x\n", itemID,
                       childMenuHdl);
                fflush(stdout);
#endif
		flags |= MIS_SUBMENU;
	    }

	    if ((menuPtr->menuType == MENUBAR)
		    && !(mePtr->childMenuRefPtr->menuPtr->menuFlags
			    & MENU_SYSTEM_MENU)) {
                Tcl_DString ds;
		TkMenuReferences *menuRefPtr;
		TkMenu *systemMenuPtr = mePtr->childMenuRefPtr->menuPtr;

                Tcl_DStringInit(&ds);
                Tcl_DStringAppend(&ds,
                        Tk_PathName(menuPtr->masterMenuPtr->tkwin), -1);
                Tcl_DStringAppend(&ds, ".system", 7);

		menuRefPtr = TkFindMenuReferences(menuPtr->interp,
			Tcl_DStringValue(&ds));

                Tcl_DStringFree(&ds);

		if ((menuRefPtr != NULL)
			&& (menuRefPtr->menuPtr != NULL)
			&& (menuPtr->parentTopLevelPtr != NULL)
			&& (systemMenuPtr->masterMenuPtr
				    == menuRefPtr->menuPtr)) {
		    HWND systemMenuHdl = (HWND) systemMenuPtr->platformData;
		    HWND wrapper =
                              TkOS2GetWrapperWindow(menuPtr->parentTopLevelPtr);
		    if (wrapper != NULLHANDLE) {
			MENUITEM sysMenu;

			WinDestroyWindow(systemMenuHdl);
                        systemMenuHdl = WinWindowFromID(wrapper, FID_SYSMENU);
			/* systemMenuHdl now handle of bitmap button item */
                        rc = (LONG) WinSendMsg(systemMenuHdl, MM_QUERYITEM,
                                               MPFROM2SHORT(SC_SYSMENU, TRUE),
                                               MPFROMP(&sysMenu));
		        if (rc != TRUE) {
#ifdef VERBOSE
                            printf("WinSendMsg(%x, MM_QI SYSMENU failed\n",
                                   systemMenuHdl);
                            fflush(stdout);
#endif
			    return;
			}
			/* sysMenu.hwndSubMenu is the real SM handle */

			systemMenuPtr->menuFlags |= MENU_SYSTEM_MENU;
			systemMenuPtr->platformData =
			    (TkMenuPlatformData) sysMenu.hwndSubMenu;
			if (!(systemMenuPtr->menuFlags
				& MENU_RECONFIGURE_PENDING)) {
			    systemMenuPtr->menuFlags
			        |= MENU_RECONFIGURE_PENDING;
			    Tcl_DoWhenIdle(ReconfigureOS2Menu,
			    	(ClientData) systemMenuPtr);
			}
		    }
		}
	    }
	    if (mePtr->childMenuRefPtr->menuPtr->menuFlags & MENU_SYSTEM_MENU) {
		systemMenu++;
	    }
	}
	if (!systemMenu) {
	    menuItem.iPosition = MIT_END;
	    menuItem.afStyle = flags;
	    menuItem.afAttribute = attrs;
	    menuItem.id = itemID;
/*
	    menuItem.hItem = (ULONG)pszNewItem;
*/
	    menuItem.hItem = (ULONG)mePtr;
#ifdef VERBOSE
            printf("Inserting [%s] (%x) itemID %x, flags %x, attrs %x\n",
                   pszNewItem, pszNewItem, itemID, flags, attrs);
            fflush(stdout);
#endif
	    rc = (LONG) WinSendMsg(os2MenuHdl, MM_INSERTITEM,
                                   MPFROMP(&menuItem), MPFROMP(pszNewItem));
#ifdef VERBOSE
            printf("MM_INSERTITEM returns %d [%s]\n", rc,
                   rc == MIT_MEMERROR ? "MIT_MEMERROR" :
                   rc == MIT_ERROR ? "MIT_ERROR" : "zero-based index");
            fflush(stdout);
#endif
	}
        Tcl_DStringFree(&translatedText);
	if (itemText != NULL) {
	    ckfree(itemText);
	    itemText = NULL;
	}
    }

    if ((menuPtr->menuType == MENUBAR)
	    && (menuPtr->parentTopLevelPtr != NULL)) {
	WinSendMsg(TkOS2GetWrapperWindow(menuPtr->parentTopLevelPtr),
	           WM_UPDATEFRAME, MPFROMLONG(FCF_MENU), MPVOID);
	Tk_GeometryRequest(menuPtr->parentTopLevelPtr, width, height);
    }

    menuPtr->menuFlags &= ~(MENU_RECONFIGURE_PENDING);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpPostMenu --
 *
 *	Posts a menu on the screen
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The menu is posted and handled.
 *
 *----------------------------------------------------------------------
 */

int
TkpPostMenu(interp, menuPtr, x, y)
    Tcl_Interp *interp;
    TkMenu *menuPtr;
    int x;
    int y;
{
    HWND os2MenuHdl = (HWND) menuPtr->platformData;
    int result, flags;
    RECTL rectl;
/*
    Tk_Window parentWindow = Tk_Parent(menuPtr->tkwin);
*/
    int oldServiceMode = Tcl_GetServiceMode();
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

#ifdef VERBOSE
    printf("TkpPostMenu (%d,%d) => (%d,%d)\n", x, y, x,
           yScreen - menuPtr->totalHeight - y);
    fflush(stdout);
#endif

    tsdPtr->inPostMenu++;

    if (menuPtr->menuFlags & MENU_RECONFIGURE_PENDING) {
	Tcl_CancelIdleCall(ReconfigureOS2Menu, (ClientData) menuPtr);
	ReconfigureOS2Menu((ClientData) menuPtr);
    }

    result = TkPreprocessMenu(menuPtr);
    if (result != TCL_OK) {
	tsdPtr->inPostMenu--;
	return result;
    }

    /*
     * The post commands could have deleted the menu, which means
     * we are dead and should go away.
     */

    if (menuPtr->tkwin == NULL) {
	tsdPtr->inPostMenu--;
    	return TCL_OK;
    }

    Tcl_SetServiceMode(TCL_SERVICE_NONE);

    /*
     * Make an assumption here. If the right button is down,
     * then we want to track it. Otherwise, track the left mouse button.
     */

    flags = PU_KEYBOARD;
    if (WinQuerySysValue(HWND_DESKTOP, SV_SWAPBUTTON)) {
	if (WinGetPhysKeyState(HWND_DESKTOP, VK_BUTTON1) == 0x8000) {
	    flags |= PU_MOUSEBUTTON2;
#ifdef VERBOSE
            printf("Popup: swapped, PU_MOUSEBUTTON2\n");
#endif
	} else {
	    flags |= PU_MOUSEBUTTON1;
#ifdef VERBOSE
            printf("Popup: swapped, PU_MOUSEBUTTON1\n");
#endif
	}
    } else {
	if (WinGetPhysKeyState(HWND_DESKTOP, VK_BUTTON2) == 0x8000) {
	    flags |= PU_MOUSEBUTTON2;
#ifdef VERBOSE
            printf("Popup: not swapped, PU_MOUSEBUTTON2\n");
#endif
	} else {
	    flags |= PU_MOUSEBUTTON1;
#ifdef VERBOSE
            printf("Popup: not swapped, PU_MOUSEBUTTON1\n");
#endif
	}
    }

    /*
     * WinPopupMenu returns immediately and doesn't wait for the popup menu
     * to be dismissed as in Windows. When it is dismissed, the window gets
     * a WM_MENUEND message => postpone "after dismissed" handling to the
     * receipt of that message.
     * It turns out that menuPtr->totalHeight is not quite correct (must be
     * something font-related), causing the translation of the X coordinate
     * tot the PM coordinate to give a wrong result. OTOH, the first time
     * the menu is being posted, WinQueryWindowRect doesn't give a correct
     * value either (always 3 for the height).
     * So for now resort to a hack: WinPopupMenu above the screen (don't
     * forget to remove the PU_HCONSTRAIN and PU_VCONSTRAIN), then query the
     * height (now this give correct output) and reposition the window.
     */
/*
    rc = WinPopupMenu(HWND_DESKTOP, tsdPtr->menuHWND, os2MenuHdl, x,
                      yScreen - menuPtr->totalHeight - y, 0L, flags);
*/
    rc = WinPopupMenu(HWND_DESKTOP, tsdPtr->menuHWND, os2MenuHdl, x,
                      yScreen, 0L, flags);
#ifdef VERBOSE
    if (rc != TRUE) {
        printf("WinPopupMenu %x %x %x (%d,%d) fl %x ERROR %x\n", HWND_DESKTOP,
               tsdPtr->menuHWND, os2MenuHdl, x, yScreen, flags,
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("WinPopupMenu %x %x %x (%d,%d) fl %x OK\n", HWND_DESKTOP,
               tsdPtr->menuHWND, os2MenuHdl, x, yScreen, flags);
    }
#endif
    rc = WinQueryWindowRect(os2MenuHdl, &rectl);
#ifdef VERBOSE
    if (rc != TRUE) {
        printf("WinQueryWindowRect %x ERROR %x\n", os2MenuHdl,
               WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("WinQueryWindowRect %x OK (%d,%d)(%d,%d)\n", os2MenuHdl,
               rectl.xLeft, rectl.yBottom, rectl.xRight, rectl.yTop);
    }
#endif
    rc = WinSetWindowPos(os2MenuHdl, HWND_TOP, x, yScreen - rectl.yTop - y, 0,
                         0, SWP_MOVE | SWP_NOADJUST);
#ifdef VERBOSE
    if (rc != TRUE) {
        printf("WinSetWindowPos %x -> (%d,%d) ERROR %x\n", os2MenuHdl, x,
               yScreen - rectl.yTop - y, WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("WinSetWindowPos %x -> (%d,%d) OK\n", os2MenuHdl, x,
               yScreen - rectl.yTop - y);
    }
#endif
    Tcl_SetServiceMode(oldServiceMode);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuNewEntry --
 *
 *	Adds a pointer to a new menu entry structure with the platform-
 *	specific fields filled in.
 *
 * Results:
 *	Standard TCL error.
 *
 * Side effects:
 *	A new command ID is allocated and stored in the platformEntryData
 *	field of mePtr.
 *
 *----------------------------------------------------------------------
 */

int
TkpMenuNewEntry(mePtr)
    TkMenuEntry *mePtr;
{
    int commandID;
    TkMenu *menuPtr = mePtr->menuPtr;
#ifdef VERBOSE
    printf("TkpMenuNewEntry\n");
            fflush(stdout);
#endif

    if (GetNewID(mePtr, &commandID) != TCL_OK) {
    	return TCL_ERROR;
    }

    if (!(menuPtr->menuFlags & MENU_RECONFIGURE_PENDING)) {
    	menuPtr->menuFlags |= MENU_RECONFIGURE_PENDING;
    	Tcl_DoWhenIdle(ReconfigureOS2Menu, (ClientData) menuPtr);
    }

    mePtr->platformEntryData = (TkMenuPlatformEntryData) commandID;
#ifdef VERBOSE
    printf("TkpMenuNewEntry returning %d for mePtr %x\n", commandID, mePtr);
    fflush(stdout);
#endif

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2MenuProc --
 *
 *	The window proc for the dummy window we put popups in. This allows
 *	us to post a popup whether or not we know what the parent window
 *	is.
 *
 * Results:
 *	Returns whatever is appropriate for the message in question.
 *
 * Side effects:
 *	Normal side-effect for OS/2 messages.
 *
 *----------------------------------------------------------------------
 */

static MRESULT EXPENTRY
TkOS2MenuProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    MRESULT mResult;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));
#ifdef VERBOSE
    printf("TkOS2MenuProc msg %x\n", message);
    fflush(stdout);
#endif

    /*
     * WinPopupMenu returns immediately and doesn't wait for the popup menu
     * to be dismissed as in Windows. When it is dismissed, the window gets
     * a WM_MENUEND message.
     */
    if (message == WM_MENUEND) {
#ifdef VERBOSE
	    USHORT usMenuId = SHORT1FROMMP(param1);
	    HWND hwndMenu = HWNDFROMMP(param2);
            printf("MenuProc WM_MENUEND, usMenuId %d, hwnd %x\n", usMenuId,
                   hwndMenu);
            fflush(stdout);
#endif
        if (tsdPtr->inPostMenu) {
	    tsdPtr->inPostMenu = 0;
        }
    }
    if (!TkOS2HandleMenuEvent(&hwnd, &message, &param1, &param2, &mResult)) {
	mResult = oldMenuProc(hwnd, message, param1, param2);
#ifdef VERBOSE
        printf("TkOS2MenuProc called oldMenuProc %x for msg %x: %x\n",
               oldMenuProc, message, mResult);
        fflush(stdout);
#endif
    }
    return mResult;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2HandleMenuEvent --
 *
 *	Filters out menu messages from messages passed to a top-level.
 *	Will respond appropriately to WM_COMMAND, WM_MENUSELECT,
 *	WM_MEASUREITEM, WM_DRAWITEM
 *
 * Result:
 *	Returns 1 if this handled the message; 0 if it did not.
 *
 * Side effects:
 *	All of the parameters may be modified so that the caller can
 *	think it is getting a different message. pmResult points to
 *	the result that should be returned to OS/2 from this message.
 *
 *----------------------------------------------------------------------
 */

int
TkOS2HandleMenuEvent(pHwnd, pMessage, pParam1, pParam2, pmResult)
    HWND *pHwnd;
    ULONG *pMessage;
    MPARAM *pParam1;
    MPARAM *pParam2;
    MRESULT *pmResult;
{
    Tcl_HashEntry *hashEntryPtr;
    int returnResult = 0;
    TkMenu *menuPtr;
    TkMenuEntry *mePtr;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));
    ThreadSpecificData *colorTsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&tkOS2ColorDataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("TkOS2HandleMenuEvent msg %x, p1 %x, p2 %x\n", *pMessage, *pParam1,
           *pParam2);
    fflush(stdout);
#endif

    switch (*pMessage) {
#ifdef VERBOSE
	case WM_QUERYWINDOWPARAMS: {
            printf("WM_QUERYWINDOWPARAMS p1 %x p2 %x\n", *pParam1, *pParam2);
            fflush(stdout);
            break;
        }
	case MM_QUERYITEMATTR: {
            printf("MM_QUERYITEMATTR p1 %x p2 %x\n", *pParam1, *pParam2);
            fflush(stdout);
            break;
        }
	case MM_SETITEMATTR: {
            printf("MM_SETITEMATTR p1 %x p2 %x\n", *pParam1, *pParam2);
            fflush(stdout);
            break;
        }
	case WM_CHAR: {
            printf("WM_CHAR menu p1 %x p2 %x\n", *pParam1, *pParam2);
            fflush(stdout);
            break;
        }
	case WM_TRANSLATEACCEL: {
            PQMSG pqmsg = PVOIDFROMMP(*pParam1);
            USHORT flags = (USHORT) SHORT1FROMMP(pqmsg->mp1);
            UCHAR krepeat = (UCHAR) SHORT2FROMMP(pqmsg->mp1);
            USHORT charcode = (USHORT) SHORT1FROMMP(pqmsg->mp2);
            USHORT vkeycode = (USHORT) SHORT2FROMMP(pqmsg->mp2);
            printf("WM_TRANSLATEACCEL menu p1 %x p2 %x\n", *pParam1, *pParam2);
            printf("WM_TRANSLATEACCEL menu h %x m %x mp1 %x mp2 %x\n",
                   ((PQMSG)(*pParam1))->hwnd, ((PQMSG)(*pParam1))->msg,
                   ((PQMSG)(*pParam1))->mp1, ((PQMSG)(*pParam1))->mp2);
            printf("WM_TRANSLATEACCEL menu vk %x repeat %x char %x fl %x\n",
                   vkeycode, krepeat, charcode, flags);
            fflush(stdout);
            break;
        }
#endif

	case WM_INITMENU:
#ifdef VERBOSE
            printf("WM_INITMENU id %x hwnd %x\n", SHORT1FROMMP(*pParam1),
                   HWNDFROMMP(*pParam2));
            fflush(stdout);
#endif
            *pmResult = 0;
	    /* Sent for every menu, even popup menus */
	    TkMenuInit();
	    hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->os2MenuTable,
                                             (char *) *pParam2);
	    if (hashEntryPtr != NULL) {
		tsdPtr->oldServiceMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
		menuPtr = (TkMenu *) Tcl_GetHashValue(hashEntryPtr);
		tsdPtr->modalMenuPtr = menuPtr;
		if (menuPtr->menuFlags & MENU_RECONFIGURE_PENDING) {
		    Tcl_CancelIdleCall(ReconfigureOS2Menu,
			    (ClientData) menuPtr);
		    ReconfigureOS2Menu((ClientData) menuPtr);
		}
		if (!tsdPtr->inPostMenu) {
		    Tcl_Interp *interp;
		    int code;

		    interp = menuPtr->interp;
		    Tcl_Preserve((ClientData)interp);
		    code = TkPreprocessMenu(menuPtr);
		    if ((code != TCL_OK) && (code != TCL_CONTINUE)
			    && (code != TCL_BREAK)) {
			Tcl_AddErrorInfo(interp, "\n    (menu preprocess)");
			Tcl_BackgroundError(interp);
		    }
		    Tcl_Release((ClientData)interp);
		}
		TkActivateMenuEntry(menuPtr, -1);
		returnResult = 1;
	    } else {
		tsdPtr->modalMenuPtr = NULL;
	    }
	    break;

	case WM_COMMAND: {
	    /* Windows: item from menu chosen, control passes a message to its
	       parent window or accelerator key translated */
	    /* param1 menu item/control ID/accelerator ID; param2 HIWORD 0 =>
	       from menu, HIWORD 1=> accel., from control => HIWORD notification
	       code, LOWORD hwnd of sending control */
	    /*
	     * OS/2: param1 LOWORD control specific
	     * param2 HIWORD source              LOWORD param1
	     *               CMDSRC_PUSHBUTTON   windowID PB
	     *               CMDSRC_MENU         windowID menu
	     *               CMDSRC_ACCELERATOR  accel.command value
	     *               CMDSRC_FONTDLG      font dialog identity
	     *               CMDSRC_FILEDLG      file dialog identity
	     *               CMDSRC_OTHER        control specific
	     *        LOWORD TRUE => result of pointing-device operation
	     *               FALSE => result of keyboard operation
	     * Sent to owner of control.
	     */
	    USHORT usSource = (USHORT)SHORT1FROMMP(*pParam2);
	    USHORT usCmd = (USHORT)SHORT1FROMMP(*pParam1);
#ifdef VERBOSE
            USHORT pointer = (USHORT)SHORT2FROMMP(*pParam2);
            printf("WM_COMMAND cmd %x, usSource %s, pointer %d\n", usCmd,
                   usSource == CMDSRC_OTHER ? "CMDSRC_OTHER" :
                   (usSource == CMDSRC_PUSHBUTTON ? "CMDSRC_PUSHBUTTON" :
                   (usSource == CMDSRC_ACCELERATOR ? "CMDSRC_ACCELERATOR" :
                   (usSource == CMDSRC_FONTDLG ? "CMDSRC_FONTDLG" :
                   (usSource == CMDSRC_FILEDLG ? "CMDSRC_FILEDLG" :
                   (usSource == CMDSRC_PRINTDLG ? "CMDSRC_PRINTDLG" :
                   (usSource == CMDSRC_COLORDLG ? "CMDSRC_COLORDLG" :
                   (usSource == CMDSRC_MENU ? "CMDSRC_MENU" : "UNKNOWN"))))))),
                   pointer);
            fflush(stdout);
#endif
            *pmResult = 0;

	    switch (usSource) {
            case CMDSRC_MENU:
#ifdef VERBOSE
                printf("CMDSRC_MENU\n");
                fflush(stdout);
#endif
                TkMenuInit();
                /*
                 * Extra cast to ULONG to prevent warning by GCC about casting
                 * from integer of different size.
                 */
                hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->commandTable,
                        (char *)(LONG)SHORT1FROMMP(*pParam1));
                if (hashEntryPtr == NULL) {
                    break;
                }
                mePtr = (TkMenuEntry *) Tcl_GetHashValue(hashEntryPtr);
                if (mePtr != NULL) {
                    TkMenuReferences *menuRefPtr;
                    TkMenuEntry *parentEntryPtr;
                    Tcl_Interp *interp;
                    int code;

                    /*
                     * We have to set the parent of this menu to be active
                     * if this is a submenu so that tearoffs will get the
                     * correct title.
                     */

                    menuPtr = mePtr->menuPtr;
                    menuRefPtr = TkFindMenuReferences(menuPtr->interp,
                            Tk_PathName(menuPtr->tkwin));
                    if ((menuRefPtr != NULL)
                            && (menuRefPtr->parentEntryPtr != NULL)) {
                        char *name;

                        for (parentEntryPtr = menuRefPtr->parentEntryPtr;
                             ;
                             parentEntryPtr =
                                 parentEntryPtr->nextCascadePtr) {
                            name = Tcl_GetStringFromObj(
                                parentEntryPtr->namePtr, NULL);
                            if (strcmp(name, Tk_PathName(menuPtr->tkwin))
                                    == 0) {
                                break;
                            }
                        }
                        if (parentEntryPtr->menuPtr
                                ->entries[parentEntryPtr->index]->state
                                != ENTRY_DISABLED) {
                            TkActivateMenuEntry(parentEntryPtr->menuPtr,
                                    parentEntryPtr->index);
                        }
                    }

                    interp = menuPtr->interp;
                    Tcl_Preserve((ClientData)interp);
                    code = TkInvokeMenu(interp, menuPtr, mePtr->index);
                    if (code != TCL_OK && code != TCL_CONTINUE
                            && code != TCL_BREAK) {
                        Tcl_AddErrorInfo(interp, "\n    (menu invoke)");
                        Tcl_BackgroundError(interp);
                    }
                    Tcl_Release((ClientData)interp);
                }
                *pmResult = 0;
                returnResult = 1;
                break;

	    case CMDSRC_ACCELERATOR: {
	        unsigned char menuChar = (unsigned char) SHORT1FROMMP(*pParam1);
                TkWindow *winPtr = (TkWindow *) WinQueryWindowULong(*pHwnd,
                                                                    QWL_USER);
#ifdef VERBOSE
                printf("CMDSRC_ACCELERATOR, winPtr %x\n", winPtr);
                printf("display %x (screens %x), screenNum %d, screen %x\n",
                       winPtr ? winPtr->display : 0,
                       winPtr ? winPtr->display->screens : 0,
                       winPtr ? winPtr->screenNum : 0,
                       winPtr && winPtr->display->screens != NULL ?
                       RootWindow(winPtr->display, winPtr->screenNum) : 0L);
                fflush(stdout);
#endif
                if (!winPtr || !winPtr->wmInfoPtr) {
                    break;
                }
		/* Message comes from frame window */
/*
	        hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->os2MenuTable,
		                           (char *) winPtr->wmInfoPtr->wrapper);
*/
	        hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->os2MenuTable,
		                           (char *) *pHwnd);
	        if (hashEntryPtr != NULL) {
		    int i;

		    *pmResult = 0;
		    menuPtr = (TkMenu *) Tcl_GetHashValue(hashEntryPtr);
		    for (i = 0; i < menuPtr->numEntries; i++) {
		        int underline;
                        char *label;

                        underline = menuPtr->entries[i]->underline;
                        if (menuPtr->entries[i]->labelPtr != NULL) {
                            label = Tcl_GetStringFromObj(
                                    menuPtr->entries[i]->labelPtr, NULL);
                        }
		        if ((-1 != underline)
			        && (NULL != menuPtr->entries[i]->labelPtr)
			        && (toupper( menuChar)
			        == toupper( (unsigned char)label[underline]))) {
			    *pmResult = (MRESULT) ((2 << 16) | i);
			    returnResult = 1;
			    break;
		        }
		    }
	        }
	        break;
	    }

	    case CMDSRC_PUSHBUTTON: {
                HWND buttonHwnd = WinWindowFromID(*pHwnd, usCmd);
                POINTL msgPos;
                WinQueryMsgPos(TclOS2GetHAB(), &msgPos);
                buttonHwnd = WinWindowFromPoint(HWND_DESKTOP, &msgPos, TRUE);
#ifdef VERBOSE
                printf("TkOS2HandleMenuEvent: CMDSRC_PUSHBUTTON id %x hwnd %x\n", usCmd, buttonHwnd);
                fflush(stdout);
#endif
                if (buttonHwnd != NULLHANDLE && buttonHwnd != *pHwnd) {
                    *pmResult = WinSendMsg(buttonHwnd, *pMessage, *pParam1,
                                           *pParam2);
                    returnResult = 1;
                }
	        break;
            }
#ifdef VERBOSE
	    case CMDSRC_FONTDLG:
                printf("TkOS2HandleMenuEvent: CMDSRC_FONTDLG\n");
                fflush(stdout);
	        break;
	    case CMDSRC_FILEDLG:
                printf("TkOS2HandleMenuEvent: CMDSRC_FILEDLG\n");
                fflush(stdout);
	        break;
	    case CMDSRC_OTHER:
                printf("TkOS2HandleMenuEvent: CMDSRC_OTHER\n");
                fflush(stdout);
	        break;
#endif
	    } /* switch */
	    break;
	}

	case WM_MEASUREITEM: {
	    POWNERITEM itemPtr = (POWNERITEM) *pParam2;
            *pmResult = (MRESULT) 0;

	    if (itemPtr != NULL) {
		mePtr = (TkMenuEntry *) itemPtr->hItem;
#ifdef VERBOSE
            printf("WM_MEASUREITEM h %x id %d hI %x hps %x mePtr: (%d,%d) %dx%d\n",
                   *pHwnd, itemPtr->idItem, itemPtr->hItem, itemPtr->hps,
                   mePtr->x, mePtr->y, mePtr->width, mePtr->height);
            fflush(stdout);
#endif
		menuPtr = mePtr->menuPtr;

		TkRecomputeMenu(menuPtr);
#ifdef VERBOSE
                printf("WM_MEASUREITEM, after TkRecomputeMenu: (%d,%d) %dx%d\n",
                       mePtr->x, mePtr->y, mePtr->width, mePtr->height);
                fflush(stdout);
#endif
		itemPtr->rclItem.xLeft = 0;
		itemPtr->rclItem.yBottom = 0;
		itemPtr->rclItem.xRight = mePtr->width;
		itemPtr->rclItem.yTop = mePtr->height;
		if (mePtr->hideMargin) {
		    itemPtr->rclItem.xRight += 2 - indicatorDimensions[1];
		} else {
                    int activeBorderWidth;

                    Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
                            menuPtr->activeBorderWidthPtr,
                            &activeBorderWidth);
		    itemPtr->rclItem.xRight += 2 * activeBorderWidth;
		}
#ifdef VERBOSE
                printf("WM_MEASUREITEM, returning %d-%d (rectl %d,%d-%d,%d)\n",
                       itemPtr->rclItem.yTop, itemPtr->rclItem.yBottom,
                       itemPtr->rclItem.xLeft, itemPtr->rclItem.yBottom,
                       itemPtr->rclItem.xRight, itemPtr->rclItem.yTop);
                fflush(stdout);
#endif
                *pmResult = (MRESULT) itemPtr->rclItem.yTop
                                      - itemPtr->rclItem.yBottom;
		returnResult = 1;
	    }
	    break;
	}
	
	case WM_DRAWITEM: {
	    TkOS2Drawable *todPtr;
	    POWNERITEM itemPtr = (POWNERITEM) *pParam2;
	    Tk_FontMetrics fontMetrics;
#ifdef VERBOSE
            printf("WM_DRAWITEM hwnd %x, idItem %d, hItem %x, hps %x\n",
                   *pHwnd, itemPtr->idItem, itemPtr->hItem, itemPtr->hps);
            fflush(stdout);
#endif
	    *pmResult = (MRESULT) FALSE;

	    if (itemPtr != NULL) {
                Tk_Font tkfont;
                SWP pos;
		mePtr = (TkMenuEntry *) itemPtr->hItem;
		menuPtr = mePtr->menuPtr;
#ifdef VERBOSE
                printf("WM_DRAWITEM menuPtr %x mePtr %x (%d,%d) %dx%d [%s]\n",
                       menuPtr, mePtr, mePtr->x, mePtr->y, mePtr->width,
                       mePtr->height,
                       Tcl_GetStringFromObj(mePtr->labelPtr, NULL));
                fflush(stdout);
#endif
		todPtr = (TkOS2Drawable *) ckalloc(sizeof(TkOS2Drawable));
#ifdef VERBOSE
                printf("    new todPtr (drawable) %x\n", todPtr);
                printf("Attr %x AttrOld %x State %x StateOld %x (%d,%d)-(%d,%d)\n",
                       itemPtr->fsAttribute, itemPtr->fsAttributeOld,
                       itemPtr->fsState, itemPtr->fsStateOld,
                       itemPtr->rclItem.xLeft, itemPtr->rclItem.yBottom,
                       itemPtr->rclItem.xRight, itemPtr->rclItem.yTop);
                fflush(stdout);
#endif
		todPtr->type = TOD_OS2PS;
		todPtr->os2PS.hwnd = itemPtr->hwnd;
		todPtr->os2PS.hps = itemPtr->hps;
                if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
                    TkOS2SelectPalette(itemPtr->hps, itemPtr->hwnd,
                                    Tk_Colormap(menuPtr->masterMenuPtr->tkwin));
                } else {
                    /* Recreate the color table in RGB mode */
                    rc = GpiCreateLogColorTable(todPtr->os2PS.hps,0L, LCOLF_RGB,
                                                0L, nextColor,
                                                colorTsdPtr->logColorTable);
#ifdef VERBOSE
                    if (rc==FALSE) {
                        printf("  GpiCreateLogColorTable ERROR %x\n",
                               WinGetLastError(TclOS2GetHAB()));
                    } else {
                        printf("  GpiCreateLogColorTable OK (%d elements)\n",
                               nextColor);
                    }
#endif
                }

		/*
		 * There are two possibilities when getting this message:
		 * 1. The item must be redrawn completely.
		 *    This is the case when fsState and fsStateOld fields are
		 *    the same. Drawing needs to be done considering the
		 *    attributes it has (MIA_CHECKED, MIA_FRAMED, MIA_DISABLED).
		 *    Attributes you draw yourself need to be cleared in both
		 *    the fsAttribute and fsAttributeOld fields to prevent the
		 *    system from drawing them.
                 * 2. An attribute has changed.
                 * Since Tk handles this all itself, we just determine if the
		 * item must be highlighted or unhighlighted.
		 * This is the case when the fsAttribute and fsAttributeOld
		 * fields are not the same (an attribute has changed, case 2)
                 * and the MIA_HILITED bit has changed.
                 * If the MIA_HILITED bit of the fsAttribute field is set the
                 * item needs to be highlighted, if it's not set it needs to
                 * be unhighlighted.
		 */

		if (mePtr->state != ENTRY_DISABLED) {
                    if (itemPtr->fsAttribute & MIA_HILITED) {
                        /* Activate this entry */
                        TkActivateMenuEntry(menuPtr, mePtr->index);
                    } else {
                        /* Deactivate this entry */
                        TkActivateMenuEntry(menuPtr, -1);
                    }
                }
if (itemPtr->fsAttribute == itemPtr->fsAttributeOld) {
                /* Redraw the entire item. */
                tkfont = Tk_GetFontFromObj(menuPtr->tkwin, menuPtr->fontPtr);
                Tk_GetFontMetrics(tkfont, &fontMetrics);
                rc = WinQueryWindowPos(itemPtr->hwnd, &pos);
                TkpDrawMenuEntry(mePtr, (Drawable) todPtr, tkfont,
                                 &fontMetrics, itemPtr->rclItem.xLeft,
                                 pos.cy - itemPtr->rclItem.yTop,
                                 itemPtr->rclItem.xRight-itemPtr->rclItem.xLeft,
                                 itemPtr->rclItem.yTop-itemPtr->rclItem.yBottom,
                                 0, 0);
/*
#ifdef VERBOSE
                WinMessageBox(HWND_DESKTOP, HWND_DESKTOP,
                              "After TkpDrawMenuEntry", "DEBUG", 1L, MB_OK);
                printf("After WinMessageBox\n");
                fflush(stdout);
#endif
                itemPtr->fsAttributeOld = itemPtr->fsAttribute = 0;
if (itemPtr->fsAttribute & MIA_CHECKED) {
                itemPtr->fsAttributeOld = itemPtr->fsAttribute = MIA_CHECKED;
} else {
                itemPtr->fsAttributeOld = itemPtr->fsAttribute = 0;
}
*/

                *pmResult = (MRESULT) TRUE; /* TRUE -> Item has been drawn */
} else {
                *pmResult = (MRESULT) FALSE; /* TRUE -> Item has been drawn */
}
#ifdef VERBOSE
                printf("Now Attr %x AttrOld %x State %x StateOld %x (%d,%d)-(%d,%d)\n",
                       itemPtr->fsAttribute, itemPtr->fsAttributeOld,
                       itemPtr->fsState, itemPtr->fsStateOld,
                       itemPtr->rclItem.xLeft, itemPtr->rclItem.yBottom,
                       itemPtr->rclItem.xRight, itemPtr->rclItem.yTop);
                fflush(stdout);
#endif
		 /*
		 */
                ckfree((char *) todPtr);
                returnResult = 1;
            }
            break;
        }

	case WM_MENUSELECT: {
	    /*
             * Win32 -> OS/2:
             * Replace with WM_MENUSELECT and MM_QUERYITEM messages.
             * The menu window handle is in the second parameter.  The
             * menu identifier is in SHORT1FROMMP(mp1).  Send MM_QUERYITEM
             * to query the menu flags for the menu item identified in
             * SHORT2FROMMP(mp1).  A menu identifier of -1 in OS/2
             * indicates that the submenu is being closed, but does not
             * necessarily indicate that menu processing is complete.
Windows:
flags = HIWORD(wParam)  MIA_CHECKED etc.
item = LOWORD(wParam)
hMenu = lParam
process this message => should return 0
flags 0xFFFF and hMenu NULL => closed menu
	     */
	    USHORT usItemId = SHORT1FROMMP(*pParam1);
	    USHORT usPostCmd = SHORT2FROMMP(*pParam1);
	    HWND hwndMenu = HWNDFROMMP(*pParam2);
#ifdef VERBOSE
            printf("WM_MENUSELECT usItem %x usPostC %x hwnd %x modalMenu %x\n",
	           usItemId, usPostCmd, hwndMenu, tsdPtr->modalMenuPtr);
            fflush(stdout);
#endif
	    *pmResult = (MRESULT) FALSE;

	    TkMenuInit();

	    if (usItemId == 0xFFFF) {
                /* Menu was closed */
#ifdef VERBOSE
                printf("WM_MENUSELECT closing\n");
                fflush(stdout);
#endif
		Tcl_SetServiceMode(tsdPtr->oldServiceMode);
		if (tsdPtr->modalMenuPtr != NULL) {
		    RecursivelyClearActiveMenu(tsdPtr->modalMenuPtr);
		}
	    } else {
		menuPtr = NULL;
		if (hwndMenu != NULLHANDLE) {
		    hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->os2MenuTable,
			    (char *) hwndMenu);
#ifdef VERBOSE
                    printf("hashEntryPtr %x for hwnd %x\n", hashEntryPtr,
                           hwndMenu);
                    fflush(stdout);
#endif
		    if (hashEntryPtr != NULL) {
			menuPtr = (TkMenu *) Tcl_GetHashValue(hashEntryPtr);
#ifdef VERBOSE
                        printf("menuPtr %x\n", menuPtr);
                        fflush(stdout);
#endif
		    }
		}


                if (menuPtr != NULL) {
                    HWND os2MenuHdl = (HWND) menuPtr->platformData;
                    MENUITEM menuItem;
#ifdef VERBOSE
                    printf("menuPtr %x os2MenuHdl %x\n",menuPtr, os2MenuHdl);
                    fflush(stdout);
#endif
                    mePtr = NULL;
                    if (usPostCmd != 0) {
                        rc = (LONG) WinSendMsg(os2MenuHdl, MM_QUERYITEM,
                                               MPFROM2SHORT(usItemId,TRUE),
                                               MPFROMP(&menuItem));
                        if (rc != TRUE) {
#ifdef VERBOSE
                           printf("WinSendMsg MM_QI %x, %d returned ERROR %x\n",
                                   os2MenuHdl, usItemId,
                                   WinGetLastError(TclOS2GetHAB()));
                            fflush(stdout);
#endif
                            return returnResult;
                        }
#ifdef VERBOSE
        printf("WinSendMsg MM_QI h %x id %d style %x pos %d hSub %x hItem %x\n",
                           os2MenuHdl, usItemId, menuItem.afStyle,
                           menuItem.iPosition, menuItem.hwndSubMenu,
                           menuItem.hItem);
                    fflush(stdout);
#endif
/*
                        if (menuItem.afStyle & MIS_SUBMENU ||
                            menuItem.afStyle & MIS_MULTMENU) {
                            mePtr = menuPtr->entries[menuItem.iPosition];
#ifdef VERBOSE
                            printf("menuItem %d afStyle (%x) & MIS_SUBMENU: %x\n",
                                   usItemId, menuItem.afStyle, mePtr);
                            fflush(stdout);
#endif
                        } else {
*/
                            /*
                             * Extra cast to ULONG to prevent warning by GCC
                             * about casting from integer of different size.
                            hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->commandTable,
                                    (char *)(ULONG) usItemId);
                            if (hashEntryPtr != NULL) {
                                mePtr = (TkMenuEntry *)
                                        Tcl_GetHashValue(hashEntryPtr);
                            }
                         */
                            mePtr = menuPtr->entries[menuItem.iPosition];
/*
                        }
*/
#ifdef VERBOSE
                        printf("mePtr %x, index %d\n", mePtr, mePtr->index);
                        fflush(stdout);
#endif
                    }
/*
*/

		    if ((mePtr == NULL) || (mePtr->state == ENTRY_DISABLED)) {
#ifdef VERBOSE
                        printf("calling TkActivateMenuEntry(%x, -1)\n", mePtr);
#endif
			TkActivateMenuEntry(menuPtr, -1);
		    } else {
			TkActivateMenuEntry(menuPtr, mePtr->index);
#ifdef VERBOSE
                        printf("calling TkActivateMenuEntry(%x, %d)\n", mePtr,
                               mePtr->index);
#endif
		    }
		    MenuSelectEvent(menuPtr);
		    Tcl_ServiceAll();
		}
	    }
            *pmResult = (MRESULT)1;
            returnResult = 1;
            break;
	}

        case WM_HELP: {
#ifdef VERBOSE
            printf("menu WM_HELP\n");
            fflush(stdout);
#endif
            break;
        }

        case WM_MENUEND: {
	    /*
             * The menu window handle is in the second parameter.  The
             * menu identifier is in SHORT1FROMMP(mp1).  Send MM_QUERYITEM
             * to query the menu flags for the menu item identified in
             * SHORT2FROMMP(mp1).  A menu identifier of -1 in OS/2
             * indicates that the submenu is being closed, but does not
             * necessarily indicate that menu processing is complete.
	     */
#ifdef VERBOSE
	    USHORT usMenuId = SHORT1FROMMP(*pParam1);
	    HWND hwndMenu = HWNDFROMMP(*pParam2);
            printf("WM_MENUEND, usMenuId %d, hwnd %x\n", usMenuId, hwndMenu);
            fflush(stdout);
#endif
	    *pmResult = (MRESULT) FALSE;

	    TkMenuInit();

            /* Menu was closed */
            Tcl_SetServiceMode(tsdPtr->oldServiceMode);
            if (tsdPtr->modalMenuPtr != NULL) {
                RecursivelyClearActiveMenu(tsdPtr->modalMenuPtr);
            }
            break;
        }

        case WM_NEXTMENU: {
#ifdef VERBOSE
            printf("menu WM_NEXTMENU\n");
            fflush(stdout);
#endif
            *pmResult = oldMenuProc(*pHwnd, *pMessage, *pParam1, *pParam2);
            break;
        }

    }
    return returnResult;
}

/*
 *----------------------------------------------------------------------
 *
 * RecursivelyClearActiveMenu --
 *
 *	Recursively clears the active entry in the menu's cascade hierarchy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Generates <<MenuSelect>> virtual events.
 *
 *----------------------------------------------------------------------
 */

void
RecursivelyClearActiveMenu(
    TkMenu *menuPtr)		/* The menu to reset. */
{
    int i;
    TkMenuEntry *mePtr;
#ifdef VERBOSE
    printf("RecursivelyClearActiveMenu\n");
            fflush(stdout);
#endif

    TkActivateMenuEntry(menuPtr, -1);
    MenuSelectEvent(menuPtr);
    for (i = 0; i < menuPtr->numEntries; i++) {
    	mePtr = menuPtr->entries[i];
    	if (mePtr->type == CASCADE_ENTRY) {
    	    if ((mePtr->childMenuRefPtr != NULL)
    	    	    && (mePtr->childMenuRefPtr->menuPtr != NULL)) {
    	    	RecursivelyClearActiveMenu(mePtr->childMenuRefPtr->menuPtr);
    	    }
    	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetWindowMenuBar --
 *
 *	Associates a given menu with a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	On OS/2, Windows and UNIX, associates the platform menu with the
 *	platform window.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetWindowMenuBar(tkwin, menuPtr)
    Tk_Window tkwin;	    /* The window we are putting the menubar into.*/
    TkMenu *menuPtr;	    /* The menu we are inserting */
{
    HWND os2MenuHdl;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

#ifdef VERBOSE
    printf("TkpSetWindowMenuBar tkwin %x\n", tkwin);
            fflush(stdout);
#endif

    if (menuPtr != NULL) {
	Tcl_HashEntry *hashEntryPtr;
	int newEntry;

	os2MenuHdl = (HWND) menuPtr->platformData;
	hashEntryPtr = Tcl_FindHashEntry(&tsdPtr->os2MenuTable,
                                         (char *) os2MenuHdl);
	Tcl_DeleteHashEntry(hashEntryPtr);
	WinDestroyWindow(os2MenuHdl);
	os2MenuHdl = WinCreateMenu(HWND_DESKTOP, NULL);
#ifdef VERBOSE
        if (os2MenuHdl == NULLHANDLE) {
            printf("TkpSetWindowMenuBar WinCreateMenu NULL ERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("TkpSetWindowMenuBar WinCreateMenu NULL OK %x\n",os2MenuHdl);
        }
        fflush(stdout);
#endif
	hashEntryPtr = Tcl_CreateHashEntry(&tsdPtr->os2MenuTable,
                                           (char *) os2MenuHdl, &newEntry);
#ifdef VERBOSE
        printf("Tcl_CreateHashEntry hwnd %x for menuPtr %x: hashEntryPtr %x\n",
               os2MenuHdl, menuPtr, hashEntryPtr);
#endif
	Tcl_SetHashValue(hashEntryPtr, (char *) menuPtr);
	menuPtr->platformData = (TkMenuPlatformData) os2MenuHdl;
	TkOS2SetMenu(tkwin, os2MenuHdl);
	if (menuPtr->menuFlags & MENU_RECONFIGURE_PENDING) {
	    Tcl_DoWhenIdle(ReconfigureOS2Menu, (ClientData) menuPtr);
	    menuPtr->menuFlags |= MENU_RECONFIGURE_PENDING;
	}
    } else {
	TkOS2SetMenu(tkwin, NULLHANDLE);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TkpSetMainMenubar --
 *
 *	Puts the menu associated with a window into the menubar. Should
 *	only be called when the window is in front.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The menubar is changed.
 *
 *----------------------------------------------------------------------
 */
void
TkpSetMainMenubar(
    Tcl_Interp *interp,		/* The interpreter of the application */
    Tk_Window tkwin,		/* The frame we are setting up */
    char *menuName)		/* The name of the menu to put in front.
    				 * If NULL, use the default menu bar.
    				 */
{
#ifdef VERBOSE
    printf("superfluous TkpSetMainMenuBar tkwin %x, [%s]\n", tkwin, menuName);
            fflush(stdout);
#endif
    /*
     * Nothing to do.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuIndicatorGeometry --
 *
 *	Gets the width and height of the indicator area of a menu.
 *
 * Results:
 *	widthPtr and heightPtr are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
GetMenuIndicatorGeometry (
    TkMenu *menuPtr,			/* The menu we are measuring */
    TkMenuEntry *mePtr,			/* The entry we are measuring */
    Tk_Font tkfont,			/* Precalculated font */
    CONST Tk_FontMetrics *fmPtr,	/* Precalculated font metrics */
    int *widthPtr,			/* The resulting width */
    int *heightPtr)			/* The resulting height */
{
#ifdef VERBOSE
    printf("GetMenuIndicatorGeometry\n");
            fflush(stdout);
#endif
    *heightPtr = indicatorDimensions[0];
    if (mePtr->hideMargin) {
	*widthPtr = 0;
    } else {
        int borderWidth;

        Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
                menuPtr->borderWidthPtr, &borderWidth);
        *widthPtr = indicatorDimensions[1] - borderWidth;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuAccelGeometry --
 *
 *	Gets the width and height of the indicator area of a menu.
 *
 * Results:
 *	widthPtr and heightPtr are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
GetMenuAccelGeometry (
    TkMenu *menuPtr,			/* The menu we are measuring */
    TkMenuEntry *mePtr,			/* The entry we are measuring */
    Tk_Font tkfont,			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr,	/* The precalculated font metrics */
    int *widthPtr,			/* The resulting width */
    int *heightPtr)			/* The resulting height */
{
#ifdef VERBOSE
    printf("GetMenuAccelGeometry\n");
            fflush(stdout);
#endif
    *heightPtr = fmPtr->linespace;
    if (mePtr->type == CASCADE_ENTRY) {
	*widthPtr = 0;
    } else if (mePtr->accelPtr == NULL) {
	*widthPtr = 0;
    } else {
        char *accel = Tcl_GetStringFromObj(mePtr->accelPtr, NULL);
	*widthPtr = Tk_TextWidth(tkfont, accel, mePtr->accelLength);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetTearoffEntryGeometry --
 *
 *	Gets the width and height of the indicator area of a menu.
 *
 * Results:
 *	widthPtr and heightPtr are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
GetTearoffEntryGeometry (
    TkMenu *menuPtr,			/* The menu we are measuring */
    TkMenuEntry *mePtr,			/* The entry we are measuring */
    Tk_Font tkfont,			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr,	/* The precalculated font metrics */
    int *widthPtr,			/* The resulting width */
    int *heightPtr)			/* The resulting height */
{
#ifdef VERBOSE
    printf("GetTearoffEntryGeometry\n");
            fflush(stdout);
#endif
    if (menuPtr->menuType != MASTER_MENU) {
	*heightPtr = 0;
    } else {
	*heightPtr = fmPtr->linespace;
    }
    *widthPtr = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuSeparatorGeometry --
 *
 *	Gets the width and height of the indicator area of a menu.
 *
 * Results:
 *	widthPtr and heightPtr are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
GetMenuSeparatorGeometry (
    TkMenu *menuPtr,			/* The menu we are measuring */
    TkMenuEntry *mePtr,			/* The entry we are measuring */
    Tk_Font tkfont,			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr,	/* The precalcualted font metrics */
    int *widthPtr,			/* The resulting width */
    int *heightPtr)			/* The resulting height */
{
#ifdef VERBOSE
    printf("GetMenuSeparatorGeometry\n");
            fflush(stdout);
#endif
    *widthPtr = 0;
    *heightPtr = fmPtr->linespace - (2 * fmPtr->descent);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawOS2SystemBitmap -- NOT NEEDED
 *
 *	Draws the OS/2 system bitmap given by bitmapID into the rect
 *	given by rectPtr in the drawable. The bitmap is centered in the
 *	rectangle. It is not clipped, so if the bitmap is bigger than
 *	the rect it will bleed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Drawing occurs. Some storage is allocated and released.
 *
 *----------------------------------------------------------------------
 */

static void
DrawOS2SystemBitmap(display, drawable, gc, rectPtr, bitmapID, alignFlags)
    Display *display;			/* The display we are drawing into */
    Drawable drawable;			/* The drawable we are working with */
    GC gc;				/* The GC to draw with */
    CONST RECTL *rectPtr;		/* The rectangle to draw into
                                         * in PM coordinates */			
    int bitmapID;			/* The OS/2 index value of the system
					 * bitmap to draw. */
    int alignFlags;			/* How to align the bitmap inside the
					 * rectangle. */
{
    TkOS2PSState state;
    HPS hps = TkOS2GetDrawablePS(display, drawable, &state);
    HBITMAP bitmap;
    CHARBUNDLE cBundle;
    POINTL aPoints[4] = {
        {0,0},
        {0,0},
        {0,0},
        {0,0}
    }; /* Lower-left dst, upper-right dst, lower-left src */
    int botOffset, leftOffset;
    BITMAPINFOHEADER2 info;
return;

#ifdef VERBOSE
    printf("DrawOS2SystemBitmap d %x bitmapID %x (%d,%d)->(%d,%d), align %x\n",
           drawable, bitmapID, rectPtr->xLeft, rectPtr->yBottom,
           rectPtr->xRight, rectPtr->yTop, alignFlags);
            fflush(stdout);
#endif

    GpiSetBackColor(hps, gc->background);
    cBundle.lColor = gc->foreground;
    rc = GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);

    bitmap = WinGetSysBitmap(HWND_DESKTOP, bitmapID);
    info.cbFix = sizeof(BITMAPINFOHEADER2);
    rc = GpiQueryBitmapInfoHeader(bitmap, &info);
    aPoints[3].x = info.cx;
    aPoints[3].y = info.cy;
#ifdef VERBOSE
    if (rc == FALSE) {
        printf("    GpiQueryBitmapInfoHeader ERROR %x bitmapdimensions %dx%d\n",
               WinGetLastError(TclOS2GetHAB()), aPoints[3].x, aPoints[3].y);
    } else {
        printf("    GpiQueryBitmapInfoHeader OK bitmapdimensions %dx%d\n",
               aPoints[3].x, aPoints[3].y);
    }
#endif

    if (alignFlags & ALIGN_BITMAP_TOP) {
	botOffset = (rectPtr->yTop - rectPtr->yBottom) - aPoints[3].y;
    } else if (alignFlags & ALIGN_BITMAP_BOTTOM) {
	botOffset = 0;
    } else {
	botOffset = (rectPtr->yTop - rectPtr->yBottom) / 2 - (aPoints[3].y / 2);
    }

    if (alignFlags & ALIGN_BITMAP_LEFT) {
	leftOffset = 0;
    } else if (alignFlags & ALIGN_BITMAP_RIGHT) {
	leftOffset = (rectPtr->xRight - rectPtr->xLeft) - aPoints[3].x;
    } else {
	leftOffset = (rectPtr->xRight - rectPtr->xLeft) / 2 - (aPoints[3].x/2);
#ifdef VERBOSE
        printf("    leftOffset = (%d-%d) / 2 (=%d) - %d/2 (=%d)  = %d\n",
               rectPtr->xRight, rectPtr->xLeft,
               (rectPtr->xRight - rectPtr->xLeft) / 2, aPoints[3].x,
               aPoints[3].x/2, leftOffset);
#endif
    }
#ifdef VERBOSE
    printf("    leftOffset %d botOffset %d\n", leftOffset, botOffset);
#endif

    aPoints[0].x = rectPtr->xLeft + leftOffset;
    aPoints[0].y = rectPtr->yBottom + botOffset;
    aPoints[1].x = aPoints[3].x + aPoints[0].x;
    aPoints[1].y = aPoints[3].y + aPoints[0].y;
    rc = GpiWCBitBlt(hps, bitmap, 4, aPoints, ROP_SRCCOPY, BBO_OR);
#ifdef VERBOSE
    if (rc == TRUE) {
        printf("GpiWCBitBlt hps %x bmp %x (%d,%d)(%d,%d)-> (%d,%d)(%d,%d) OK\n",
               hps, bitmap, aPoints[2].x, aPoints[2].y, aPoints[3].x,
               aPoints[3].y, aPoints[0].x, aPoints[0].y, aPoints[1].x,
               aPoints[1].y);
    } else {
        printf("GpiWCBitBlt hps %x bmp %x (%d,%d) -> (%d,%d)(%d,%d) ERROR %x\n",
               hps, bitmap, aPoints[2].x, aPoints[2].y, aPoints[0].x,
               aPoints[0].y, aPoints[1].x, aPoints[1].y,
               WinGetLastError(TclOS2GetHAB()));
    }
#endif
    GpiDeleteBitmap(bitmap);

    TkOS2ReleaseDrawablePS(drawable, hps, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryIndicator --
 *
 *	This procedure draws the indicator part of a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the menu in its
 *	current mode.
 *
 *----------------------------------------------------------------------
 */
void
DrawMenuEntryIndicator(menuPtr, mePtr, d, gc, indicatorGC, tkfont, fmPtr, x,
	y, width, height)
    TkMenu *menuPtr;		    /* The menu we are drawing */
    TkMenuEntry *mePtr;		    /* The entry we are drawing */
    Drawable d;			    /* What we are drawing into */
    GC gc;			    /* The gc we are drawing with */
    GC indicatorGC;		    /* The gc for indicator objects */
    Tk_Font tkfont;		    /* The precalculated font */
    CONST Tk_FontMetrics *fmPtr;    /* The precalculated font metrics */
    int x;			    /* Left edge */
    int y;			    /* Top edge */
    int width;
    int height;
{
#ifdef VERBOSE
    printf("DrawMenuEntryIndicator d %x, (%d,%d) %dx%d\n", d, x, y, width,
           height);
            fflush(stdout);
#endif
    if ((mePtr->type == CHECK_BUTTON_ENTRY)
            || (mePtr->type == RADIO_BUTTON_ENTRY)) {
        if (mePtr->indicatorOn && (mePtr->entryFlags & ENTRY_SELECTED)) {
	    RECTL rect;
	    GC whichGC;
            int borderWidth, activeBorderWidth;

            if (mePtr->state != ENTRY_NORMAL) {
	        whichGC = gc;
	    } else {
	        whichGC = indicatorGC;
	    }

            /* PM coordinates reversed */
	    rect.yTop = TkOS2WindowHeight((TkOS2Drawable *)d) - y;
	    rect.yBottom = rect.yTop - mePtr->height;
            Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
                    menuPtr->borderWidthPtr, &borderWidth);
            Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
                    menuPtr->activeBorderWidthPtr, &activeBorderWidth);
	    rect.xLeft = borderWidth + activeBorderWidth + x;
	    rect.xRight = mePtr->indicatorSpace + x;

/**** Win95/NT 4.0 part */
            if ((mePtr->state == ENTRY_DISABLED)
                    && (menuPtr->disabledFgPtr != NULL)) {
                RECTL hilightRect;
                LONG oldFgColor = whichGC->foreground;

                whichGC->foreground = WinQuerySysColor(HWND_DESKTOP,
                                                SYSCLR_ACTIVETITLETEXTBGND, 0L);
                hilightRect.yTop = rect.yTop + 1;
                hilightRect.yBottom = rect.yBottom + 1;
                hilightRect.xLeft = rect.xLeft + 1;
                hilightRect.xRight = rect.xRight + 1;
#ifdef VERBOSE
                printf("Drawing hilight MENUCHECK (Indicator) %d,%d -> %d,%d\n",
                       hilightRect.xLeft, hilightRect.yBottom,
                       hilightRect.xRight, hilightRect.yTop);
                fflush(stdout);
#endif
	        DrawOS2SystemBitmap(menuPtr->display, d, whichGC, &hilightRect,
		                    SBMP_MENUCHECK, 0);	/* PM coordinates */
                whichGC->foreground = oldFgColor;
            }
/**** End of Win95/NT 4.0 part */
#ifdef VERBOSE
            printf("Drawing MENUCHECK (Indicator) %d,%d -> %d,%d\n",
                   rect.xLeft, rect.yBottom, rect.xRight, rect.yTop);
            fflush(stdout);
#endif
	    DrawOS2SystemBitmap(menuPtr->display, d, whichGC, &rect,
		                SBMP_MENUCHECK, 0);	/* PM coordinates */

	    if ((mePtr->state == ENTRY_DISABLED)
		    && (menuPtr->disabledImageGC != None)) {
	        XFillRectangle(menuPtr->display, d, menuPtr->disabledImageGC,
		        rect.xLeft, y, rect.xRight, y + mePtr->height);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryAccelerator --
 *
 *	This procedure draws the accelerator part of a menu. We
 *	need to decide what to draw here. Should we replace strings
 *	like "Control", "Command", etc?
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the menu in its
 *	current mode.
 *
 *----------------------------------------------------------------------
 */

void
DrawMenuEntryAccelerator(menuPtr, mePtr, d, gc, tkfont, fmPtr,
	activeBorder, x, y, width, height, drawArrow)
    TkMenu *menuPtr;			/* The menu we are drawing */
    TkMenuEntry *mePtr;			/* The entry we are drawing */
    Drawable d;				/* What we are drawing into */
    GC gc;				/* The gc we are drawing with */
    Tk_Font tkfont;			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr;	/* The precalculated font metrics */
    Tk_3DBorder activeBorder;		/* The border when an item is active */
    int x;				/* left edge */
    int y;				/* top edge */
    int width;				/* Width of menu entry */
    int height;				/* Height of menu entry */
    int drawArrow;			/* For cascade menus, whether of not
					 * to draw the arraw. I cannot figure
					 * out Windows' algorithm for where
					 * to draw this. */
{
    int baseline;
    int leftEdge = x + mePtr->indicatorSpace + mePtr->labelWidth;
    char *accel = NULL;

    /* Translate Y coordinate to PM */
    LONG pmy =  TkOS2WindowHeight((TkOS2Drawable *)d) - y - height;

    if (mePtr->accelPtr != NULL) {
        accel = Tcl_GetStringFromObj(mePtr->accelPtr, NULL);
    }

    /* Use X Window System coordinate for baseline! */
    baseline = y + (height + fmPtr->ascent - fmPtr->descent) / 2;
#ifdef VERBOSE
    printf("DrawMenuEntryAccelerator d %x (%d,%d) %dx%d baseline %d t %s\n",
           d, x, y, width, height, baseline,
           mePtr->type == SEPARATOR_ENTRY ? "SEPARATOR_ENTRY" :
           (mePtr->type == TEAROFF_ENTRY ? "TEAROFF_ENTRY" :
           (mePtr->type == COMMAND_ENTRY ? "COMMAND_ENTRY" :
           (mePtr->type == CHECK_BUTTON_ENTRY ? "CHECK_BUTTON_ENTRY" :
           (mePtr->type == RADIO_BUTTON_ENTRY ? "RADIO_BUTTON_ENTRY" :
           (mePtr->type == CASCADE_ENTRY ? "CASCADE_ENTRY" : "OTHER"))))));
    printf("mePtr->state == ENTRY_DISABLED %s\nmenuPtr->disabledFgPtr != NULL %s
mePtr->accel != NULL %s\nmePtr->type == CASCADE_ENTRY %s\ndrawArrow %s\n",
           (mePtr->state == ENTRY_DISABLED) ? "TRUE" : "FALSE",
           (menuPtr->disabledFgPtr != NULL) ? "TRUE" : "FALSE",
           (accel != NULL) ? "TRUE" : "FALSE",
           (mePtr->type == CASCADE_ENTRY) ? "TRUE" : "FALSE",
           drawArrow ? "TRUE" : "FALSE");
    fflush(stdout);
#endif
    if ((mePtr->state == ENTRY_DISABLED) && (menuPtr->disabledFgPtr != NULL)
        &&
        ((mePtr->accelPtr != NULL) || ((mePtr->type == CASCADE_ENTRY)))) {
        LONG oldFgColor = gc->foreground;

        gc->foreground = WinQuerySysColor(HWND_DESKTOP,
                                          SYSCLR_ACTIVETITLETEXTBGND, 0L);
#ifdef VERBOSE
        printf("DrawMenuEntryAccelerator Windows95-part, fore %x\n",
               gc->foreground);
        fflush(stdout);
#endif
        if (mePtr->accelPtr != NULL) {
            Tk_DrawChars(menuPtr->display, d, gc, tkfont, accel,
                         mePtr->accelLength, leftEdge + 1, baseline + 1);
        }

/* OS/2 menu will draw cascade arrow
        if (mePtr->type == CASCADE_ENTRY) {
            RECTL rect;

            rect.yBottom = pmy + yBorder;
            rect.yTop = pmy + height - yBorder + 1;
            rect.xLeft = leftEdge;
            rect.xRight = x + width - 1;
            DrawOS2SystemBitmap(menuPtr->display, d, gc, &rect,
                    SBMP_MENUATTACHED, ALIGN_BITMAP_RIGHT);
        }
*/
        gc->foreground = oldFgColor;
    }

    if (mePtr->accelPtr != NULL) {
	Tk_DrawChars(menuPtr->display, d, gc, tkfont, accel,
		mePtr->accelLength, leftEdge, baseline);
    }

#ifdef VERBOSE
    printf("mePtr->type == CASCADE_ENTRY %d, drawArrow %d\n",
           (mePtr->type == CASCADE_ENTRY), drawArrow);
    fflush(stdout);
#endif
    if ((mePtr->type == CASCADE_ENTRY) && drawArrow) {
	RECTL rect;
        LONG yBorder = WinQuerySysValue(HWND_DESKTOP, SV_CYBORDER);

	rect.yBottom = pmy + yBorder;
	rect.yTop = pmy + height - yBorder;
	rect.xLeft = x + mePtr->indicatorSpace + mePtr->labelWidth;
	rect.xRight = x + width - 1;
#ifdef VERBOSE
        printf("Drawing CASCADE %d,%d -> %d,%d\n", rect.xLeft, rect.yBottom,
               rect.xRight, rect.yTop);
        fflush(stdout);
#endif
/* OS/2 menu will draw cascade arrow
	DrawOS2SystemBitmap(menuPtr->display, d, gc, &rect, SBMP_MENUATTACHED,
		ALIGN_BITMAP_RIGHT);	/* PM coordinates */
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuSeparator --
 *
 *	The menu separator is drawn.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the menu in its
 *	current mode.
 *
 *----------------------------------------------------------------------
 */
void
DrawMenuSeparator(menuPtr, mePtr, d, gc, tkfont, fmPtr, x, y, width, height)
    TkMenu *menuPtr;			/* The menu we are drawing */
    TkMenuEntry *mePtr;			/* The entry we are drawing */
    Drawable d;				/* What we are drawing into */
    GC gc;				/* The gc we are drawing with */
    Tk_Font tkfont;			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr;	/* The precalculated font metrics */
    int x;				/* left edge */
    int y;				/* top edge */
    int width;				/* width of item */
    int height;				/* height of item */
{
    XPoint points[2];
    Tk_3DBorder border;
#ifdef VERBOSE
    printf("DrawMenuSeparator d %x, (%d,%d) %dx%d\n", d, x, y, width, height);
            fflush(stdout);
#endif

    points[0].x = x;
    points[0].y = y + height / 2;
    points[1].x = x + width - 1;
    points[1].y = points[0].y;
    border = Tk_Get3DBorderFromObj(menuPtr->tkwin, menuPtr->borderPtr);
    Tk_Draw3DPolygon(menuPtr->tkwin, d, border, points, 2, 1, TK_RELIEF_RAISED);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuUnderline --
 *
 *	On appropriate platforms, draw the underline character for the
 *	menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the menu in its
 *	current mode.
 *
 *----------------------------------------------------------------------
 */
static void
DrawMenuUnderline(
    TkMenu *menuPtr,			/* The menu to draw into */
    TkMenuEntry *mePtr,			/* The entry we are drawing */
    Drawable d,				/* What we are drawing into */
    GC gc,				/* The gc to draw into */
    Tk_Font tkfont,			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr,	/* The precalculated font metrics */
    int x,				/* Left Edge */
    int y,				/* Top Edge */
    int width,				/* Width of entry */
    int height)				/* Height of entry */
{
#ifdef VERBOSE
    printf("DrawMenuUnderline d %x, (%d,%d) %dx%d\n", d, x, y, width, height);
            fflush(stdout);
#endif
    if (mePtr->underline >= 0) {
        char *label = Tcl_GetStringFromObj(mePtr->labelPtr, NULL);
        char *start = Tcl_UtfAtIndex(label, mePtr->underline);
        char *end = Tcl_UtfNext(start);

    	Tk_UnderlineChars(menuPtr->display, d,
    		gc, tkfont, label, x + mePtr->indicatorSpace,
    		y + (height + fmPtr->ascent - fmPtr->descent) / 2,
		start - label, end - label);
    }		
}

/*
 *--------------------------------------------------------------
 *
 * TkpInitializeMenuBindings --
 *
 *	For every interp, initializes the bindings for OS/2
 *	menus. Does nothing on Mac or XWindows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	C-level bindings are setup for the interp which will
 *	handle Alt-key sequences for menus without beeping
 *	or interfering with user-defined Alt-key bindings.
 *
 *--------------------------------------------------------------
 */

void
TkpInitializeMenuBindings(interp, bindingTable)
    Tcl_Interp *interp;		    /* The interpreter to set. */
    Tk_BindingTable bindingTable;   /* The table to add to. */
{
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryLabel --
 *
 *	This procedure draws the label part of a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the menu in its
 *	current mode.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuEntryLabel(
    TkMenu *menuPtr,			/* The menu we are drawing */
    TkMenuEntry *mePtr,			/* The entry we are drawing */
    Drawable d,				/* What we are drawing into */
    GC gc,				/* The gc we are drawing into */
    Tk_Font tkfont,			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr,	/* The precalculated font metrics */
    int x,				/* left edge */
    int y,				/* right edge */
    int width,				/* width of entry */
    int height)				/* height of entry */
{
    int baseline;
    int indicatorSpace =  mePtr->indicatorSpace;
    int activeBorderWidth;
    int leftEdge;
    int imageHeight, imageWidth;

    Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
            menuPtr->activeBorderWidthPtr, &activeBorderWidth);
    leftEdge = x + indicatorSpace + activeBorderWidth;

    /*
     * Draw label or bitmap or image for entry.
     */

    baseline = y + (height + fmPtr->ascent - fmPtr->descent) / 2;
#ifdef VERBOSE
    printf("DrawMenuEntryLabel d %x, (%d,%d) %dx%d, baseline %d\n", d, x, y,
           width, height, baseline);
            fflush(stdout);
#endif
    if (mePtr->image != NULL) {
    	Tk_SizeOfImage(mePtr->image, &imageWidth, &imageHeight);
    	if ((mePtr->selectImage != NULL)
	    	&& (mePtr->entryFlags & ENTRY_SELECTED)) {
	    Tk_RedrawImage(mePtr->selectImage, 0, 0,
		    imageWidth, imageHeight, d, leftEdge,
	            (int) (y + (mePtr->height - imageHeight)/2));
    	} else {
	    Tk_RedrawImage(mePtr->image, 0, 0, imageWidth,
		    imageHeight, d, leftEdge,
		    (int) (y + (mePtr->height - imageHeight)/2));
    	}
    } else if (mePtr->bitmapPtr != NULL) {
    	int width, height;
        Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr);

        Tk_SizeOfBitmap(menuPtr->display, bitmap, &width, &height);
    	XCopyPlane(menuPtr->display, bitmap, d, gc, 0, 0, (unsigned) width,
                   (unsigned) height, leftEdge,
	    	   (int) (y + (mePtr->height - height)/2), 1);
    } else {
    	if (mePtr->labelLength > 0) {
            char *label = Tcl_GetStringFromObj(mePtr->labelPtr, NULL);

	    Tk_DrawChars(menuPtr->display, d, gc, tkfont, label,
                         mePtr->labelLength, leftEdge, baseline);
	    DrawMenuUnderline(menuPtr, mePtr, d, gc, tkfont, fmPtr, x, y,
		              width, height);
    	}
    }

    if (mePtr->state == ENTRY_DISABLED) {
	if (menuPtr->disabledFgPtr == NULL) {
	    XFillRectangle(menuPtr->display, d, menuPtr->disabledGC, x, y,
		    (unsigned) width, (unsigned) height);
	} else if ((mePtr->image != NULL)
		&& (menuPtr->disabledImageGC != None)) {
	    XFillRectangle(menuPtr->display, d, menuPtr->disabledImageGC,
		    leftEdge,
		    (int) (y + (mePtr->height - imageHeight)/2),
		    (unsigned) imageWidth, (unsigned) imageHeight);
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkpComputeMenubarGeometry --
 *
 *	This procedure is invoked to recompute the size and
 *	layout of a menu that is a menubar clone.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fields of menu entries are changed to reflect their
 *	current positions, and the size of the menu window
 *	itself may be changed.
 *
 *--------------------------------------------------------------
 */

void
TkpComputeMenubarGeometry(menuPtr)
    TkMenu *menuPtr;		/* Structure describing menu. */
{
#ifdef VERBOSE
    printf("TkpComputeMenubarGeometry\n");
            fflush(stdout);
#endif
    TkpComputeStandardMenuGeometry(menuPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawTearoffEntry --
 *
 *	This procedure draws the background part of a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the menu in its
 *	current mode.
 *
 *----------------------------------------------------------------------
 */

void
DrawTearoffEntry(menuPtr, mePtr, d, gc, tkfont, fmPtr, x, y, width, height)
    TkMenu *menuPtr;			/* The menu we are drawing */
    TkMenuEntry *mePtr;			/* The entry we are drawing */
    Drawable d;				/* The drawable we are drawing into */
    GC gc;				/* The gc we are drawing with */
    Tk_Font tkfont;			/* The font we are drawing with */
    CONST Tk_FontMetrics *fmPtr;	/* The metrics we are drawing with */
    int x;
    int y;
    int width;
    int height;
{
    XPoint points[2];
    int segmentWidth, maxX;
    Tk_3DBorder border;
#ifdef VERBOSE
    printf("DrawTearoffEntry d %x, (%d,%d) %dx%d\n", d, x, y, width, height);
            fflush(stdout);
#endif

    if (menuPtr->menuType != MASTER_MENU) {
	return;
    }

    points[0].x = x;
    points[0].y = y + height/2;
    points[1].y = points[0].y;
    segmentWidth = 6;
    maxX  = width - 1;
    border = Tk_Get3DBorderFromObj(menuPtr->tkwin, menuPtr->borderPtr);

    while (points[0].x < maxX) {
	points[1].x = points[0].x + segmentWidth;
	if (points[1].x > maxX) {
	    points[1].x = maxX;
	}
	Tk_Draw3DPolygon(menuPtr->tkwin, d, border, points, 2, 1,
		         TK_RELIEF_RAISED);
	points[0].x += 2*segmentWidth;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpConfigureMenuEntry --
 *
 *	Processes configurations for menu entries.
 *
 * Results:
 *	Returns standard TCL result. If TCL_ERROR is returned, then
 *	the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information get set for mePtr; old resources
 *	get freed, if any need it.
 *
 *----------------------------------------------------------------------
 */

int
TkpConfigureMenuEntry(mePtr)
    register TkMenuEntry *mePtr;	/* Information about menu entry;  may
					 * or may not already have values for
					 * some fields. */
{
    TkMenu *menuPtr = mePtr->menuPtr;
#ifdef VERBOSE
    printf("TkpConfigureMenuEntry\n");
            fflush(stdout);
#endif

    if (!(menuPtr->menuFlags & MENU_RECONFIGURE_PENDING)) {
	menuPtr->menuFlags |= MENU_RECONFIGURE_PENDING;
	Tcl_DoWhenIdle(ReconfigureOS2Menu, (ClientData) menuPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawMenuEntry --
 *
 *	Draws the given menu entry at the given coordinates with the
 *	given attributes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	X Server commands are executed to display the menu entry.
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawMenuEntry(mePtr, d, tkfont, menuMetricsPtr, x, y, width, height,
	strictMotif, drawArrow)
    TkMenuEntry *mePtr;		    /* The entry to draw */
    Drawable d;			    /* What to draw into */
    Tk_Font tkfont;		    /* Precalculated font for menu */
    CONST Tk_FontMetrics *menuMetricsPtr;
				    /* Precalculated metrics for menu */
    int x;			    /* X-coordinate of topleft of entry */
    int y;			    /* Y-coordinate of topleft of entry */
    int width;			    /* Width of the entry rectangle */
    int height;			    /* Height of the current rectangle */
    int strictMotif;		    /* Boolean flag */
    int drawArrow;		    /* Whether or not to draw the cascade
				     * arrow for cascade items. Only applies
				     * to Windows and OS/2. */
{
    GC gc, indicatorGC;
    TkMenu *menuPtr = mePtr->menuPtr;
    Tk_3DBorder bgBorder, activeBorder;
    CONST Tk_FontMetrics *fmPtr;
    Tk_FontMetrics entryMetrics;
    int padY = (menuPtr->menuType == MENUBAR) ? 3 : 0;
    int adjustedY = y + padY;
    int adjustedHeight = height - 2 * padY;
#ifdef VERBOSE
    printf("TkpDrawMenuEntry d %x (%d,%d) %dx%d aY %d aH %d pY %d dA %d %s a%d\n",
           d, x, y, width, height, adjustedY, adjustedHeight, padY, drawArrow,
           mePtr->type == SEPARATOR_ENTRY ? "SEPARATOR_ENTRY" :
           (mePtr->type == TEAROFF_ENTRY ? "TEAROFF_ENTRY" :
           (mePtr->type == COMMAND_ENTRY ? "COMMAND_ENTRY" :
           (mePtr->type == CHECK_BUTTON_ENTRY ? "CHECK_BUTTON_ENTRY" :
           (mePtr->type == RADIO_BUTTON_ENTRY ? "RADIO_BUTTON_ENTRY" :
           (mePtr->type == CASCADE_ENTRY ? "CASCADE_ENTRY" : "OTHER"))))),
           (mePtr->state == ENTRY_ACTIVE));
            fflush(stdout);
#endif

    /*
     * Choose the gc for drawing the foreground part of the entry.
     */

    if ((mePtr->state == ENTRY_ACTIVE) && !strictMotif) {
	gc = mePtr->activeGC;
	if (gc == NULL) {
	    gc = menuPtr->activeGC;
	}
    } else {
    	TkMenuEntry *cascadeEntryPtr;
    	int parentDisabled = 0;
        char *name;
    	
    	for (cascadeEntryPtr = menuPtr->menuRefPtr->parentEntryPtr;
    		cascadeEntryPtr != NULL;
    		cascadeEntryPtr = cascadeEntryPtr->nextCascadePtr) {
            name = Tcl_GetStringFromObj(cascadeEntryPtr->namePtr, NULL);
            if (strcmp(name, Tk_PathName(menuPtr->tkwin)) == 0) {
                if (mePtr->state == ENTRY_DISABLED) {
    	    	    parentDisabled = 1;
    	    	}
    	    	break;
    	    }
    	}

	if (((parentDisabled || (mePtr->state == ENTRY_DISABLED)))
		&& (menuPtr->disabledFgPtr != NULL)) {
	    gc = mePtr->disabledGC;
	    if (gc == NULL) {
		gc = menuPtr->disabledGC;
	    }
	} else {
	    gc = mePtr->textGC;
	    if (gc == NULL) {
		gc = menuPtr->textGC;
	    }
	}
    }
    indicatorGC = mePtr->indicatorGC;
    if (indicatorGC == NULL) {
	indicatorGC = menuPtr->indicatorGC;
    }
	
    bgBorder = Tk_Get3DBorderFromObj(menuPtr->tkwin,
            (mePtr->borderPtr == NULL) ? menuPtr->borderPtr
            : mePtr->borderPtr);
    if (strictMotif) {
	activeBorder = bgBorder;
    } else {
        activeBorder = Tk_Get3DBorderFromObj(menuPtr->tkwin,
            (mePtr->activeBorderPtr == NULL) ? menuPtr->activeBorderPtr
            : mePtr->activeBorderPtr);
    }

    if (mePtr->fontPtr == NULL) {
	fmPtr = menuMetricsPtr;
    } else {
	tkfont = Tk_GetFontFromObj(menuPtr->tkwin, mePtr->fontPtr);
	Tk_GetFontMetrics(tkfont, &entryMetrics);
	fmPtr = &entryMetrics;
    }

    /*
     * Need to draw the entire background, including padding. On Unix,
     * for menubars, we have to draw the rest of the entry taking
     * into account the padding.
     */

    DrawMenuEntryBackground(menuPtr, mePtr, d, activeBorder,
	    bgBorder, x, y, width, height);

    if (mePtr->type == SEPARATOR_ENTRY) {
	DrawMenuSeparator(menuPtr, mePtr, d, gc, tkfont,
		fmPtr, x, adjustedY, width, adjustedHeight);
    } else if (mePtr->type == TEAROFF_ENTRY) {
	DrawTearoffEntry(menuPtr, mePtr, d, gc, tkfont, fmPtr, x, adjustedY,
		width, adjustedHeight);
    } else {
	DrawMenuEntryLabel(menuPtr, mePtr, d, gc, tkfont, fmPtr, x, adjustedY,
		width, adjustedHeight);
	DrawMenuEntryAccelerator(menuPtr, mePtr, d, gc, tkfont, fmPtr,
		activeBorder, x, adjustedY, width, adjustedHeight, drawArrow);
	if (!mePtr->hideMargin) {
	    DrawMenuEntryIndicator(menuPtr, mePtr, d, gc, indicatorGC, tkfont,
		    fmPtr, x, adjustedY, width, adjustedHeight);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuLabelGeometry --
 *
 *	Figures out the size of the label portion of a menu item.
 *
 * Results:
 *	widthPtr and heightPtr are filled in with the correct geometry
 *	information.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMenuLabelGeometry(mePtr, tkfont, fmPtr, widthPtr, heightPtr)
    TkMenuEntry *mePtr;			/* The entry we are computing */
    Tk_Font tkfont;			/* The precalculated font */
    CONST Tk_FontMetrics *fmPtr;	/* The precalculated metrics */
    int *widthPtr;			/* The resulting width of the label
					 * portion */
    int *heightPtr;			/* The resulting height of the label
					 * portion */
{
    TkMenu *menuPtr = mePtr->menuPtr;

    if (mePtr->image != NULL) {
#ifdef VERBOSE
        printf("GetMenuLabelGeometry: TkSizeOfImage\n");
            fflush(stdout);
#endif
    	Tk_SizeOfImage(mePtr->image, widthPtr, heightPtr);
    } else if (mePtr->bitmapPtr != NULL) {
        Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr);
#ifdef VERBOSE
        printf("GetMenuLabelGeometry: TkSizeOfBitmap\n");
        fflush(stdout);
#endif
    	Tk_SizeOfBitmap(menuPtr->display, bitmap, widthPtr, heightPtr);
    } else {
    	*heightPtr = fmPtr->linespace;
    	
    	if (mePtr->labelPtr != NULL) {
            char *label = Tcl_GetStringFromObj(mePtr->labelPtr, NULL);

    	    *widthPtr = Tk_TextWidth(tkfont, label, mePtr->labelLength);
    	} else {
    	    *widthPtr = 0;
    	}
#ifdef VERBOSE
        printf("GetMenuLabelGeometry: width %d, height %d (+1)\n", *widthPtr,
               *heightPtr);
        fflush(stdout);
#endif
    }
    *heightPtr += 1;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryBackground --
 *
 *	This procedure draws the background part of a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the menu in its
 *	current mode.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuEntryBackground(
    TkMenu *menuPtr,			/* The menu we are drawing. */
    TkMenuEntry *mePtr,			/* The entry we are drawing. */
    Drawable d,				/* What we are drawing into */
    Tk_3DBorder activeBorder,		/* Border for active items */
    Tk_3DBorder bgBorder,		/* Border for the background */
    int x,				/* left edge */
    int y,				/* top edge */
    int width,				/* width of rectangle to draw */
    int height)				/* height of rectangle to draw */
{
#ifdef VERBOSE
#include "tk3d.h"
    TkBorder *activePtr = (TkBorder *) activeBorder;
    TkBorder *bgPtr = (TkBorder *) bgBorder;
    printf("DrawMenuEntryBackground d %x actbg %x bg %x act %d => %d (%d,%d) %dx%d\n",
           d, activePtr->bgGC->foreground, bgPtr->bgGC->foreground,
           mePtr->state == ENTRY_ACTIVE,
           mePtr->state == ENTRY_ACTIVE ? activePtr->bgGC->foreground :
           bgPtr->bgGC->foreground, x, y, width, height);
           fflush(stdout);
#endif
    if (mePtr->state == ENTRY_ACTIVE) {
	bgBorder = activeBorder;
    }
    Tk_Fill3DRectangle(menuPtr->tkwin, d, bgBorder,
    	    x, y, width, height, 0, TK_RELIEF_FLAT);
}

/*
 *--------------------------------------------------------------
 *
 * TkpComputeStandardMenuGeometry --
 *
 *	This procedure is invoked to recompute the size and
 *	layout of a menu that is not a menubar clone.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fields of menu entries are changed to reflect their
 *	current positions, and the size of the menu window
 *	itself may be changed.
 *
 *--------------------------------------------------------------
 */

void
TkpComputeStandardMenuGeometry(
    TkMenu *menuPtr)		/* Structure describing menu. */
{
    Tk_Font menuFont, tkfont;
    Tk_FontMetrics menuMetrics, entryMetrics, *fmPtr;
    int x, y, height, width, indicatorSpace, labelWidth, accelWidth;
    int windowWidth, windowHeight, accelSpace;
    int i, j, lastColumnBreak = 0;
    int activeBorderWidth, borderWidth;
#ifdef VERBOSE
    printf("TkpComputeStandardMenuGeometry\n");
            fflush(stdout);
#endif

    if (menuPtr->tkwin == NULL) {
	return;
    }

    Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
            menuPtr->borderWidthPtr, &borderWidth);
    x = y = borderWidth;
    indicatorSpace = labelWidth = accelWidth = 0;
    windowHeight = 0;
#ifdef VERBOSE
    printf("windowHeight 0\n");
            fflush(stdout);
#endif

    /*
     * On the Mac especially, getting font metrics can be quite slow,
     * so we want to do it intelligently. We are going to precalculate
     * them and pass them down to all of the measuring and drawing
     * routines. We will measure the font metrics of the menu once.
     * If an entry does not have its own font set, then we give
     * the geometry/drawing routines the menu's font and metrics.
     * If an entry has its own font, we will measure that font and
     * give all of the geometry/drawing the entry's font and metrics.
     */

    menuFont = Tk_GetFontFromObj(menuPtr->tkwin, menuPtr->fontPtr);
    Tk_GetFontMetrics(menuFont, &menuMetrics);
    accelSpace = Tk_TextWidth(menuFont, "M", 1);
    Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
            menuPtr->activeBorderWidthPtr, &activeBorderWidth);

    for (i = 0; i < menuPtr->numEntries; i++) {
    	if (menuPtr->entries[i]->fontPtr == NULL) {
    	    tkfont = menuFont;
    	    fmPtr = &menuMetrics;
    	} else {
            tkfont = Tk_GetFontFromObj(menuPtr->tkwin,
                                       menuPtr->entries[i]->fontPtr);
    	    Tk_GetFontMetrics(tkfont, &entryMetrics);
    	    fmPtr = &entryMetrics;
    	}
	if ((i > 0) && menuPtr->entries[i]->columnBreak) {
	    if (accelWidth != 0) {
		labelWidth += accelSpace;
	    }
	    for (j = lastColumnBreak; j < i; j++) {
		menuPtr->entries[j]->indicatorSpace = indicatorSpace;
		menuPtr->entries[j]->labelWidth = labelWidth;
		menuPtr->entries[j]->width = indicatorSpace + labelWidth
			+ accelWidth + 2 * activeBorderWidth;
		menuPtr->entries[j]->x = x;
		menuPtr->entries[j]->entryFlags &= ~ENTRY_LAST_COLUMN;
	    }
	    x += indicatorSpace + labelWidth + accelWidth
		    + 2 * borderWidth;
	    indicatorSpace = labelWidth = accelWidth = 0;
	    lastColumnBreak = i;
	    y = borderWidth;
#ifdef VERBOSE
            printf("y = borderWidth (%d)\n", y);
            fflush(stdout);
#endif
	}

	if (menuPtr->entries[i]->type == SEPARATOR_ENTRY) {
	    GetMenuSeparatorGeometry(menuPtr, menuPtr->entries[i], tkfont,
	    	    fmPtr, &width, &height);
	    menuPtr->entries[i]->height = height;
	} else if (menuPtr->entries[i]->type == TEAROFF_ENTRY) {
	    GetTearoffEntryGeometry(menuPtr, menuPtr->entries[i], tkfont,
	    	    fmPtr, &width, &height);
	    menuPtr->entries[i]->height = height;
	} else {
	
	    /*
	     * For each entry, compute the height required by that
	     * particular entry, plus three widths:  the width of the
	     * label, the width to allow for an indicator to be displayed
	     * to the left of the label (if any), and the width of the
	     * accelerator to be displayed to the right of the label
	     * (if any).  These sizes depend, of course, on the type
	     * of the entry.
	     */
	
	    GetMenuLabelGeometry(menuPtr->entries[i], tkfont, fmPtr, &width,
	    	    &height);
	    menuPtr->entries[i]->height = height;
	    if (width > labelWidth) {
	    	labelWidth = width;
	    }
	
	    GetMenuAccelGeometry(menuPtr, menuPtr->entries[i], tkfont,
		    fmPtr, &width, &height);
	    if (height > menuPtr->entries[i]->height) {
	    	menuPtr->entries[i]->height = height;
	    }
	    if (width > accelWidth) {
	    	accelWidth = width;
	    }

	    GetMenuIndicatorGeometry(menuPtr, menuPtr->entries[i], tkfont,
	    	    fmPtr, &width, &height);
	    if (height > menuPtr->entries[i]->height) {
	    	menuPtr->entries[i]->height = height;
	    }
	    if (width > indicatorSpace) {
	    	indicatorSpace = width;
	    }

	    menuPtr->entries[i]->height += 2 * activeBorderWidth + 1;
    	}
        menuPtr->entries[i]->y = y;
	y += menuPtr->entries[i]->height;
	if (y > windowHeight) {
	    windowHeight = y;
#ifdef VERBOSE
            printf("menuPtr->entries[%d]->y = %d, y += %d, windowHeight = %d\n",
                   i, menuPtr->entries[i]->y, menuPtr->entries[i]->height, y);
            fflush(stdout);
        } else {
            printf("menuPtr->entries[%d]->y = %d, y += %d\n",
                   i, menuPtr->entries[i]->y, menuPtr->entries[i]->height);
            fflush(stdout);
#endif
	}
    }

    if (accelWidth != 0) {
	labelWidth += accelSpace;
    }
    for (j = lastColumnBreak; j < menuPtr->numEntries; j++) {
	menuPtr->entries[j]->indicatorSpace = indicatorSpace;
	menuPtr->entries[j]->labelWidth = labelWidth;
	menuPtr->entries[j]->width = indicatorSpace + labelWidth
		+ accelWidth + 2 * activeBorderWidth;
	menuPtr->entries[j]->x = x;
	menuPtr->entries[j]->entryFlags |= ENTRY_LAST_COLUMN;
    }
    windowWidth = x + indicatorSpace + labelWidth + accelWidth + accelSpace
	    + 2 * activeBorderWidth + 2 * borderWidth;

    windowHeight += borderWidth;
#ifdef VERBOSE
    printf("windowHeight += borderWidth (%d)\n", borderWidth);
            fflush(stdout);
#endif

    /*
     * The X server doesn't like zero dimensions, so round up to at least
     * 1 (a zero-sized menu should never really occur, anyway).
     */

    if (windowWidth <= 0) {
	windowWidth = 1;
    }
    if (windowHeight <= 0) {
#ifdef VERBOSE
        printf("windowHeight <= 0 (%d) => 1\n", windowHeight);
            fflush(stdout);
#endif
	windowHeight = 1;
    }
    menuPtr->totalWidth = windowWidth;
#ifdef VERBOSE
    printf("totalHeight %d\n", windowHeight);
            fflush(stdout);
#endif
    menuPtr->totalHeight = windowHeight;
}

/*
 *----------------------------------------------------------------------
 *
 * MenuSelectEvent --
 *
 *	Generates a "MenuSelect" virtual event. This can be used to
 *	do context-sensitive menu help.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Places a virtual event on the event queue.
 *
 *----------------------------------------------------------------------
 */

static void
MenuSelectEvent(
    TkMenu *menuPtr)		/* the menu we have selected. */
{
    XVirtualEvent event;
    POINTL rootPoint;
#ifdef VERBOSE
    printf("MenuSelectEvent\n");
            fflush(stdout);
#endif

    if (menuPtr->tkwin == NULL) {
        return;
    }
    event.type = VirtualEvent;
    event.serial = menuPtr->display->request;
    event.send_event = 0;
    event.display = menuPtr->display;
    Tk_MakeWindowExist(menuPtr->tkwin);
    event.event = Tk_WindowId(menuPtr->tkwin);
    event.root = XRootWindow(menuPtr->display, 0);
    event.subwindow = None;
    event.time = TkpGetMS();

    WinQueryMsgPos(TclOS2GetHAB(), &rootPoint);
    event.x_root = rootPoint.x;
    /* Translate y coordinate */
    event.y_root = yScreen - rootPoint.y;
    event.state = TkOS2GetModifierState();
    event.same_screen = 1;
    event.name = Tk_GetUid("MenuSelect");
    Tk_QueueWindowEvent((XEvent *) &event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuNotifyToplevelCreate --
 *
 *	This routine reconfigures the menu and the clones indicated by
 *	menuName becuase a toplevel has been created and any system
 *	menus need to be created.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An idle handler is set up to do the reconfiguration.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuNotifyToplevelCreate(
    Tcl_Interp *interp,			/* The interp the menu lives in. */
    char *menuName)			/* The name of the menu to
					 * reconfigure. */
{
    TkMenuReferences *menuRefPtr;
    TkMenu *menuPtr;
#ifdef VERBOSE
    printf("TkpMenuNotifyToplevelCreate [%s]\n", menuName);
            fflush(stdout);
#endif

    if ((menuName != NULL) && (menuName[0] != '\0')) {
	menuRefPtr = TkFindMenuReferences(interp, menuName);
	if ((menuRefPtr != NULL) && (menuRefPtr->menuPtr != NULL)) {
	    for (menuPtr = menuRefPtr->menuPtr->masterMenuPtr; menuPtr != NULL;
		    menuPtr = menuPtr->nextInstancePtr) {
		if ((menuPtr->menuType == MENUBAR)
			&& !(menuPtr->menuFlags & MENU_RECONFIGURE_PENDING)) {
		    menuPtr->menuFlags |= MENU_RECONFIGURE_PENDING;
		    Tcl_DoWhenIdle(ReconfigureOS2Menu,
			    (ClientData) menuPtr);
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MenuExitHandler --
 *
 *	Throws away the utility window needed for menus and unregisters
 *	the class.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Menus have to be reinitialized next time.
 *
 *----------------------------------------------------------------------
 */

static void
MenuExitHandler(
    ClientData clientData)	    /* Not used */
{
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

#ifdef VERBOSE
    printf("MenuExitHandler tsdPtr->menuHWND %x\n", tsdPtr->menuHWND);
            fflush(stdout);
#endif
    WinDestroyWindow(tsdPtr->menuHWND);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetMenuSystemDefault --
 *
 *      Gets the OS/2 specific default value for a given X resource
 *      database name.
 *
 * Results:
 *      Returns a Tcl_Obj * with the default value. If there is no
 *      Windows-specific default for this attribute, returns NULL.
 *      This object has a ref count of 0.
 *
 * Side effects:
 *      Storage is allocated.
 *
 *----------------------------------------------------------------------
 */
Tcl_Obj *
TkOS2GetMenuSystemDefault(
    Tk_Window tkwin,            /* A window to use. */
    char *dbName,               /* The option database name. */
    char *className)            /* The name of the option class. */
{
    Tcl_Obj *valuePtr = NULL;

    if ((strcmp(dbName, "activeBorderWidth") == 0) ||
            (strcmp(dbName, "borderWidth") == 0)) {
        valuePtr = Tcl_NewIntObj(defaultBorderWidth);
    } else if (strcmp(dbName, "font") == 0) {
        valuePtr = Tcl_NewStringObj(Tcl_DStringValue(&menuFontDString),
                -1);
    }

    return valuePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2MenuSetDefaults --
 *
 *      Sets up the hash tables and the variables used by the menu package.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      lastMenuID gets initialized, and the parent hash and the command hash
 *      are allocated.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2MenuSetDefaults(
    int firstTime)                  /* Is this the first time this
                                     * has been called? */
{
    char sizeString[TCL_INTEGER_SPACE];
    char faceName[FACESIZE];
    HPS hps;
    Tcl_DString boldItalicDString;
    int bold = 0;
    int italic = 0;
    FONTMETRICS fm;
    int pointSize;
    HBITMAP checkMarkBitmap;
    BITMAPINFOHEADER info;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

    /*
     * Set all of the default options.
     */

    defaultBorderWidth = WinQuerySysValue(HWND_DESKTOP, SV_CXBORDER);
    if (WinQuerySysValue(HWND_DESKTOP, SV_CYBORDER) > defaultBorderWidth) {
	defaultBorderWidth = WinQuerySysValue(HWND_DESKTOP, SV_CYBORDER);
    }

    /*
     * System font is in OS2.INI, application "PM_SystemFonts",
     * key "Menus" (eg. on Warp 4 "9.WarpSans Bold").
     * We don't need to query that though, since the menu control
     * handles this for us, so just query the font of its hps.
     */

    hps = WinGetPS(tsdPtr->menuHWND);
#ifdef VERBOSE
    printf("WinGetPS menuHWND %x returns %x\n", tsdPtr->menuHWND, hps);
    fflush(stdout);
#endif

    if (!firstTime) {
        Tcl_DStringFree(&menuFontDString);
    }
    Tcl_DStringInit(&menuFontDString);

    rc = GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm);
#ifdef VERBOSE
    if (rc == TRUE) {
        printf("GpiQueryFontMetrics %x OK: [%s] %dpt wt %d it %d l%d\n",
               hps, fm.szFacename, fm.lEmHeight, fm.usWeightClass > 5,
               fm.fsSelection & FM_SEL_ITALIC, fm.lMaxBaselineExt);
    } else {
        printf("GpiQueryFontMetrics %x ERROR %x\n", hps,
               WinGetLastError(TclOS2GetHAB()));
    }
    fflush(stdout);
#endif
    WinReleasePS(hps);

    /*
     * For a bitmap font, lEmHeight contains the height in pixels
     * For an outline font it contains the intended point size in
     * decipoints.
     */
    if (fm.fsType & FM_TYPE_FIXED) {
        pointSize = -fm.sNominalPointSize;
#ifdef VERBOSE
        printf("FM_TYPE_FIXED, pointSize %d\n", pointSize);
        fflush(stdout);
#endif
    } else {
	        pointSize = fm.sNominalPointSize / 10;
#ifdef VERBOSE
        printf("not FM_TYPE_FIXED, pointSize %d\n", pointSize);
        fflush(stdout);
#endif
    }
    if (fm.usWeightClass > 5) {
	bold = 1;
    }
    if (fm.fsSelection & FM_SEL_ITALIC) {
	italic = 1;
    }

    Tcl_DStringAppendElement(&menuFontDString, fm.szFacename);
    sprintf(sizeString, "%d", pointSize);
    Tcl_DStringAppendElement(&menuFontDString, sizeString);

    if (bold == 1 || italic == 1) {
	Tcl_DStringInit(&boldItalicDString);
	if (bold == 1) {
	    Tcl_DStringAppendElement(&boldItalicDString, "bold");
	}
	if (italic == 1) {
	    Tcl_DStringAppendElement(&boldItalicDString, "italic");
	}
	Tcl_DStringAppendElement(&menuFontDString,
		Tcl_DStringValue(&boldItalicDString));
    }

    /*
     * Now we go ahead and get the dimensions of the check mark and the
     * appropriate margins. Since this is fairly hairy, we do it here
     * to save time when traversing large sets of menu items.
     */

    checkMarkBitmap = WinGetSysBitmap(HWND_DESKTOP, SBMP_MENUCHECK);
    if (checkMarkBitmap == NULLHANDLE) {
	return;
    }
    info.cbFix = sizeof(BITMAPINFOHEADER);	/* Must be set first */
    rc = GpiQueryBitmapParameters(checkMarkBitmap, &info);
    GpiDeleteBitmap(checkMarkBitmap);
    if (rc != TRUE) {
	return;
    }
    indicatorDimensions[0] = info.cy;		/* height */
    indicatorDimensions[1] = info.cx;		/* width */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuInit --
 *
 *	Sets up the hash tables and the variables used by the menu package.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	lastMenuID gets initialized, and the parent hash and the command hash
 *	are allocated.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuInit()
{
    int i;
    FRAMECDATA fcdata;
    HBITMAP checkMarkBitmap;
    BITMAPINFOHEADER info;
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));
#ifdef VERBOSE
    printf("TkpMenuInit\n");
    fflush(stdout);
#endif

    Tcl_InitHashTable(&tsdPtr->os2MenuTable, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&tsdPtr->commandTable, TCL_ONE_WORD_KEYS);

    /*
    WinRegisterClass(TclOS2GetHAB(), MENU_CLASS_NAME, TkOS2MenuProc, 0L, 0L);
    */

    fcdata.cb = sizeof(FRAMECDATA);
    fcdata.flCreateFlags = 0L;
    fcdata.hmodResources = 0L;
    fcdata.idResources = 0;
    tsdPtr->menuHWND = WinCreateWindow(HWND_DESKTOP, WC_MENU, "MenuWindow",
                               0L, 0, 0, 10, 10, NULLHANDLE, HWND_BOTTOM,
			       FID_MENU, &fcdata, NULL);
/*
    tsdPtr->menuHWND = WinCreateWindow(HWND_DESKTOP, WC_MENU, "MenuWindow",
                               WS_CLIPSIBLINGS | WS_SAVEBITS, 0, 0, 0, 0,
                               HWND_DESKTOP, HWND_TOP, FID_MENU, NULL, NULL);
*/
    oldMenuProc = WinSubclassWindow(tsdPtr->menuHWND, TkOS2MenuProc);
#ifdef VERBOSE
    printf("WinCreateWindow menuHWND %x, oldMenuProc %x\n", tsdPtr->menuHWND,
           oldMenuProc);
    fflush(stdout);
#endif

    Tcl_CreateExitHandler(MenuExitHandler, (ClientData) NULL);
    TkOS2MenuSetDefaults(1);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuThreadInit --
 *
 *      Sets up the thread-local hash tables used by the menu module.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Hash tables os2MenuTable and commandTable are initialized.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuThreadInit()
{
    MenuThreadSpecificData *tsdPtr = (MenuThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(MenuThreadSpecificData));

    Tcl_InitHashTable(&tsdPtr->os2MenuTable, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&tsdPtr->commandTable, TCL_ONE_WORD_KEYS);
}

/* 
 * tkOS2Wm.c --
 *
 *	This module takes care of the interactions between a Tk-based
 *	application and the window manager.  Among other things, it
 *	implements the "wm" command and passes geometry information
 *	to the window manager.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-2000 by Scriptics Corporation.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkOS2Int.h"

/*
 * Event structure for synthetic activation events.  These events are
 * placed on the event queue whenever a toplevel gets a WM_MOUSEACTIVATE
 * message.
 */

typedef struct ActivateEvent {
    Tcl_Event ev;
    TkWindow *winPtr;
} ActivateEvent;

/*
 * A data structure of the following type holds information for
 * each window manager protocol (such as WM_DELETE_WINDOW) for
 * which a handler (i.e. a Tcl command) has been defined for a
 * particular top-level window.
 */

typedef struct ProtocolHandler {
    Atom protocol;              /* Identifies the protocol. */
    struct ProtocolHandler *nextPtr;
                                /* Next in list of protocol handlers for
                                 * the same top-level window, or NULL for
                                 * end of list. */
    Tcl_Interp *interp;         /* Interpreter in which to invoke command. */
    char command[4];            /* Tcl command to invoke when a client
                                 * message for this protocol arrives.
                                 * The actual size of the structure varies
                                 * to accommodate the needs of the actual
                                 * command. THIS MUST BE THE LAST FIELD OF
                                 * THE STRUCTURE. */
} ProtocolHandler;

#define HANDLER_SIZE(cmdLength) \
    ((unsigned) (sizeof(ProtocolHandler) - 3 + cmdLength))

/*
 * This structure represents the contents of a icon, in terms of its
 * image. The HPOINTER is an internal OS/2 handle.  Most of these
 * icon-specific-structures originated with the Winico extension.
 * We stripped out unused parts of that code, and integrated the
 * code more naturally with Tcl.
 */
/* temp*/
#if 0
typedef struct {
        ULONG         width, height, colors; /*  Width, Height and bpp */
        PBYTE         bitsPtr;               /*  ptr to bits */
        ULONG         numBytes;              /*  how many bytes? */
        PBITMAPINFO2  bitmapPtr;             /*  ptr to header */
        PBYTE         xorPtr;                /*  ptr to XOR image bits */
        PBYTE         andPtr;                /*  ptr to AND image bits */
        HPOINTER      hIcon;                 /*  the icon */
} ICONIMAGE, *PICONIMAGE;

/*
 * This structure is how we represent a block of the above
 * items.  We will reallocate these structures according to
 * how many images they need to contain.
 */
typedef struct {
        int           nNumImages;     /*  How many images? */
        ICONIMAGE     IconImages[1];  /*  Image entries */
} BlockOfIconImages, *BlockOfIconImagesPtr;

/*
 * These two structures are used to read in icons from an
 * 'icon directory' (i.e. the contents of a .icr file, say).
 * We only use these structures temporarily, since we copy
 * the information we want into a BlockOfIconImages.
 */
typedef struct {
        BYTE    bWidth;         /*  Width of the image */
        BYTE    bHeight;        /*  Height of the image (times 2) */
        BYTE    bColorCount;    /*  Number of colors in image (0 if >=8bpp) */
        BYTE    bReserved;      /*  Reserved */
        WORD    wPlanes;        /*  Color Planes */
        WORD    wBitCount;      /*  Bits per pixel */
        ULONG   dwBytesInRes;   /*  how many bytes in this resource? */
        ULONG   dwImageOffset;  /*  where in the file is this image */
} ICONDIRENTRY, *PICONDIRENTRY;
typedef struct {
        WORD          idReserved;    /*  Reserved */
        WORD          idType;        /*  resource type (1 for icons) */
        WORD          idCount;       /*  how many images? */
        ICONDIRENTRY  idEntries[1];  /*  the entries for each image */
} ICONDIR, *PICONDIR;

/*
 * A pointer to one of these structures is associated with each
 * toplevel.  This allows us to free up all memory associated with icon
 * resources when a window is deleted or if the window's icon is
 * changed.  They are simply reference counted according to:
 *
 * (i) how many WmInfo structures point to this object
 * (ii) whether the ThreadSpecificData defined in this file contains
 * a pointer to this object.
 *
 * The former count is for windows whose icons are individually
 * set, and the latter is for the global default icon choice.
 *
 * Icons loaded from .icr/.icr use the iconBlock field, icons
 * loaded from .exe/.dll use the hIcon field.
 */
typedef struct OS2IconInstance {
    int refCount;                /* Number of instances that share this
                                  * data structure. */
    BlockOfIconImagesPtr iconBlock;
                                 /* Pointer to icon resource data for
                                  * image. */
} OS2IconInstance;

typedef struct OS2IconInstance *OS2IconPtr;
#endif

/*
 * A data structure of the following type holds window-manager-related
 * information for each top-level window in an application.
 */

typedef struct TkWmInfo {
    TkWindow *winPtr;           /* Pointer to main Tk information for
                                 * this window. */
    HWND wrapper;               /* This is the decorative frame window
                                 * created by the window manager to wrap
                                 * a toplevel window.  This window is
                                 * a direct child of the root window. */
    Tk_Uid titleUid;            /* Title to display in window caption.  If
                                 * NULL, use name of widget. */
    Tk_Uid iconName;            /* Name to display in icon. */
    TkWindow *masterPtr;        /* Master window for TRANSIENT_FOR property,
                                 * or NULL. */
    XWMHints hints;             /* Various pieces of information for
                                 * window manager. */
    char *leaderName;           /* Path name of leader of window group
                                 * (corresponds to hints.window_group).
                                 * Malloc-ed. Note:  this field doesn't
                                 * get updated if leader is destroyed. */
    Tk_Window icon;             /* Window to use as icon for this window,
                                 * or NULL. */
    Tk_Window iconFor;          /* Window for which this window is icon, or
                                 * NULL if this isn't an icon for anyone. */

    /*
     * Information used to construct an XSizeHints structure for
     * the window manager:
     */

    int defMinWidth, defMinHeight, defMaxWidth, defMaxHeight;
                                /* Default resize limits given by system. */
    int sizeHintsFlags;         /* Flags word for XSizeHints structure.
                                 * If the PBaseSize flag is set then the
                                 * window is gridded;  otherwise it isn't
                                 * gridded. */
    int minWidth, minHeight;    /* Minimum dimensions of window, in
                                 * grid units, not pixels. */
    int maxWidth, maxHeight;    /* Maximum dimensions of window, in
                                 * grid units, not pixels, or 0 to default. */
    Tk_Window gridWin;          /* Identifies the window that controls
                                 * gridding for this top-level, or NULL if
                                 * the top-level isn't currently gridded. */
    int widthInc, heightInc;    /* Increments for size changes (# pixels
                                 * per step). */
    struct {
        int x;  /* numerator */
        int y;  /* denominator */
    } minAspect, maxAspect;     /* Min/max aspect ratios for window. */
    int reqGridWidth, reqGridHeight;
                                /* The dimensions of the window (in
                                 * grid units) requested through
                                 * the geometry manager. */
    int gravity;                /* Desired window gravity. */

    /*
     * Information used to manage the size and location of a window.
     */

    int width, height;          /* Desired dimensions of window, specified
                                 * in grid units.  These values are
                                 * set by the "wm geometry" command and by
                                 * ConfigureNotify events (for when wm
                                 * resizes window).  -1 means user hasn't
                                 * requested dimensions. */
    int x, y;                   /* Desired X and Y coordinates for window.
                                 * These values are set by "wm geometry",
                                 * plus by ConfigureNotify events (when wm
                                 * moves window).  These numbers are
                                 * different than the numbers stored in
                                 * winPtr->changes because (a) they could be
                                 * measured from the right or bottom edge
                                 * of the screen (see WM_NEGATIVE_X and
                                 * WM_NEGATIVE_Y flags) and (b) if the window
                                 * has been reparented then they refer to the
                                 * parent rather than the window itself. */
    int borderWidth, borderHeight;
                                /* Width and height of window dressing, in
                                 * pixels for the current style/exStyle.  This
                                 * includes the border on both sides of the
                                 * window. */
    int configWidth, configHeight;
                                /* Dimensions passed to last request that we
                                 * issued to change geometry of window.  Used
                                 * to eliminate redundant resize operations. */
    HWND hMenu;                 /* the hMenu associated with this menu */
    ULONG style, exStyle;       /* Style flags for the wrapper window. */

    /*
     * List of children of the toplevel which have private colormaps.
     */

    TkWindow **cmapList;        /* Array of window with private colormaps. */
    int cmapCount;              /* Number of windows in array. */

    /*
     * Miscellaneous information.
     */

    ProtocolHandler *protPtr;   /* First in list of protocol handlers for
                                 * this window (NULL means none). */
    int cmdArgc;                /* Number of elements in cmdArgv below. */
    char **cmdArgv;             /* Array of strings to store in the
                                 * WM_COMMAND property.  NULL means nothing
                                 * available. */
    char *clientMachine;        /* String to store in WM_CLIENT_MACHINE
                                 * property, or NULL. */
    int flags;                  /* Miscellaneous flags, defined below. */
    int numTransients;          /* number of transients on this window */
#if 0
    OS2IconPtr iconPtr;         /* pointer to titlebar icon structure for
                                 * this window, or NULL. */
#endif
    struct TkWmInfo *nextPtr;   /* Next in list of all top-level windows. */
} WmInfo;

/*
 * Flag values for WmInfo structures:
 *
 * WM_NEVER_MAPPED -            non-zero means window has never been
 *                              mapped;  need to update all info when
 *                              window is first mapped.
 * WM_UPDATE_PENDING -          non-zero means a call to UpdateGeometryInfo
 *                              has already been scheduled for this
 *                              window;  no need to schedule another one.
 * WM_NEGATIVE_X -              non-zero means x-coordinate is measured in
 *                              pixels from right edge of screen, rather
 *                              than from left edge.
 * WM_NEGATIVE_Y -              non-zero means y-coordinate is measured in
 *                              pixels up from bottom of screen, rather than
 *                              down from top.
 * WM_UPDATE_SIZE_HINTS -       non-zero means that new size hints need to be
 *                              propagated to window manager. Not used on
 *                              Windows and OS/2.
 * WM_SYNC_PENDING -            set to non-zero while waiting for the window
 *                              manager to respond to some state change.
 * WM_MOVE_PENDING -            non-zero means the application has requested
 *                              a new position for the window, but it hasn't
 *                              been reflected through the window manager
 *                              yet.
 * WM_COLORAMPS_EXPLICIT -      non-zero means the colormap windows were
 *                              set explicitly via "wm colormapwindows".
 * WM_ADDED_TOPLEVEL_COLORMAP - non-zero means that when "wm colormapwindows"
 *                              was called the top-level itself wasn't
 *                              specified, so we added it implicitly at
 *                              the end of the list.
 */

#define WM_NEVER_MAPPED                 (1<<0)
#define WM_UPDATE_PENDING               (1<<1)
#define WM_NEGATIVE_X                   (1<<2)
#define WM_NEGATIVE_Y                   (1<<3)
#define WM_UPDATE_SIZE_HINTS            (1<<4)
#define WM_SYNC_PENDING                 (1<<5)
#define WM_CREATE_PENDING               (1<<6)
#define WM_MOVE_PENDING                 (1<<7)
#define WM_COLORMAPS_EXPLICIT           (1<<8)
#define WM_ADDED_TOPLEVEL_COLORMAP      (1<<9)
#define WM_WIDTH_NOT_RESIZABLE          (1<<10)
#define WM_HEIGHT_NOT_RESIZABLE         (1<<11)

/*
 * Window styles for various types of toplevel windows.
 * Placing should be on the pixels specified by Tk, not byte-aligned
 *  ==> FCF_NOBYTEALIGN
 * Override redirect windows get created as undecorated popups.
 *  ==> no FCF_*BORDER, no FCF_TITLEBAR, no FCF_MINMAX, FCF_SYSMENU
 * Transient windows get a modal dialog frame.
 *  ==> FCF_DLGBORDER, FCF_TITLEBAR
 * Neither override, nor transient windows appear in the tasklist.
 *  ==> no FCF_TASKLIST
 */


#define WM_TOPLEVEL_STYLE (WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
#define EX_TOPLEVEL_STYLE (FCF_NOBYTEALIGN | FCF_TITLEBAR | FCF_SIZEBORDER | \
                           FCF_MINMAX | FCF_SYSMENU | FCF_TASKLIST)
#define BORDERWIDTH_TOPLEVEL    (2*xSizeBorder)
#define BORDERHEIGHT_TOPLEVEL   (titleBar + 2*ySizeBorder)

#define WM_OVERRIDE_STYLE (WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
#define EX_OVERRIDE_STYLE (FCF_NOBYTEALIGN | FCF_NOMOVEWITHOWNER)
#define BORDERWIDTH_OVERRIDE    (0)
#define BORDERHEIGHT_OVERRIDE   (0)

#define WM_TRANSIENT_STYLE (WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
#define EX_TRANSIENT_STYLE (FCF_NOBYTEALIGN | FCF_DLGBORDER | FCF_TITLEBAR |\
                            FCF_NOMOVEWITHOWNER)
#define BORDERWIDTH_TRANSIENT   (2*xDlgBorder)
#define BORDERHEIGHT_TRANSIENT  (titleBar + 2*yDlgBorder)

/*
 * The following structure is the official type record for geometry
 * management of top-level windows.
 */

static void		TopLevelReqProc (ClientData dummy, Tk_Window tkwin);

static Tk_GeomMgr wmMgrType = {
    "wm",				/* name */
    TopLevelReqProc,			/* requestProc */
    (Tk_GeomLostSlaveProc *) NULL,	/* lostSlaveProc */
};

typedef struct ThreadSpecificData {
    HPAL systemPalette;      /* System palette; refers to the
                              * currently installed foreground logical
                              * palette. */
    int initialized;         /* Flag indicating whether thread-
                              * specific elements of module have
                              * been initialized. */
    int firstWindow;         /* Flag, cleared when the first window
                              * is mapped in a non-iconic state. */
#if 0
    OS2IconPtr iconPtr;      /* IconPtr being used as default for all
                              * toplevels, or NULL. */
#endif
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * The following variables cannot be placed in thread local storage
 * because they must be shared across threads.
 */

static int initialized;        /* Flag indicating whether module has
                                * been initialized. */
TCL_DECLARE_MUTEX(os2WmMutex)

/*
 * Forward declarations for procedures defined in this file:
 */

static void             ConfigureTopLevel _ANSI_ARGS_((SWP *pos));
static void             GenerateConfigureNotify _ANSI_ARGS_((
                            TkWindow *winPtr));
static void             GetMaxSize _ANSI_ARGS_((WmInfo *wmPtr, int *maxWidthPtr,
                            int *maxHeightPtr));
static void             GetMinSize _ANSI_ARGS_((WmInfo *wmPtr, int *minWidthPtr,
                            int *minHeightPtr));
static TkWindow *       GetTopLevel _ANSI_ARGS_((HWND hwnd));
static void             InitWm _ANSI_ARGS_((void));
static MRESULT          InstallColormaps _ANSI_ARGS_((HWND hwnd, ULONG message,
                            int isForemost));
static void             InvalidateSubTree _ANSI_ARGS_((TkWindow *winPtr,
                            Colormap colormap));
static int	        ParseGeometry _ANSI_ARGS_((Tcl_Interp *interp,
                            char *string, TkWindow *winPtr));
static void             RefreshColormap _ANSI_ARGS_((Colormap colormap,
                            TkDisplay *dispPtr));
static void             SetLimits _ANSI_ARGS_((HWND hwnd,
                            TRACKINFO *info));
static MRESULT EXPENTRY TopLevelProc _ANSI_ARGS_((HWND hwnd, ULONG message,
                            MPARAM param1, MPARAM param2));
static void	        TopLevelEventProc _ANSI_ARGS_((ClientData clientData,
                            XEvent *eventPtr));
static void	        TopLevelReqProc _ANSI_ARGS_((ClientData dummy,
                            Tk_Window tkwin));
static void	        UpdateGeometryInfo _ANSI_ARGS_((ClientData clientData));
static void             UpdateWrapper _ANSI_ARGS_((TkWindow *winPtr));
static MRESULT EXPENTRY WmProc _ANSI_ARGS_((HWND hwnd, ULONG message,
                            MPARAM param1, MPARAM param2));
static void             WmWaitVisibilityProc _ANSI_ARGS_((
                            ClientData clientData, XEvent *eventPtr));
#if 0
static BlockOfIconImagesPtr   ReadIconFromICOFile _ANSI_ARGS_((
                            Tcl_Interp *interp, char* fileName));
static OS2IconPtr       ReadIconFromFile _ANSI_ARGS_((
                            Tcl_Interp *interp, char *fileName));
static int              ReadICOHeader _ANSI_ARGS_((Tcl_Channel channel));
static BOOL             AdjustIconImagePointers _ANSI_ARGS_((PICONIMAGE imagePtr));
static HPOINTER            MakeIconFromResource _ANSI_ARGS_((PICONIMAGE iconPtr));
static HPOINTER            GetIcon _ANSI_ARGS_((OS2IconPtr titlebarIcon,
                            int icon_size));
static int              OS2SetIcon _ANSI_ARGS_((Tcl_Interp *interp,
                            OS2IconPtr titlebarIcon, Tk_Window tkw));
static void             FreeIconBlock _ANSI_ARGS_((BlockOfIconImagesPtr lpIR));
static void             DecrIconRefCount _ANSI_ARGS_((OS2IconPtr titlebarIcon));
#endif

/* Used in BytesPerLine */
#define WIDTHBYTES(bits)      ((((bits) + 31)>>5)<<2)

#if 0
/*
 *----------------------------------------------------------------------
 *
 * DIBNumColors --
 *
 *      Calculates the number of entries in the color table, given by
 *      LPSTR bitmapPtr - pointer to the CF_DIB memory block.  Used by
 *      titlebar icon code.
 *
 * Results:
 *
 *      WORD - Number of entries in the color table.
 *
 * Side effects: None.
 *
 *
 *----------------------------------------------------------------------
 */
static WORD DIBNumColors( LPSTR bitmapPtr )
{
    WORD wBitCount;
    ULONG dwClrUsed;

    dwClrUsed = ((PBITMAPINFOHEADER2) bitmapPtr)->biClrUsed;

    if (dwClrUsed)
        return (WORD) dwClrUsed;

    wBitCount = ((PBITMAPINFOHEADER2) bitmapPtr)->biBitCount;

    switch (wBitCount)
    {
        case 1: return 2;
        case 4: return 16;
        case 8: return 256;
        default:return 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PaletteSize --
 *
 *      Calculates the number of bytes in the color table, as given by
 *      LPSTR bitmapPtr - pointer to the CF_DIB memory block.  Used by
 *      titlebar icon code.
 *
 * Results:
 *      number of bytes in the color table
 *
 * Side effects: None.
 *
 *
 *----------------------------------------------------------------------
 */
static WORD PaletteSize( LPSTR bitmapPtr )
{
    return ((WORD)( DIBNumColors( bitmapPtr ) * sizeof( RGBQUAD )) );
}

/*
 *----------------------------------------------------------------------
 *
 * FindDIBits --
 *
 *      Locate the image bits in a CF_DIB format DIB, as given by
 *      LPSTR bitmapPtr - pointer to the CF_DIB memory block.  Used by
 *      titlebar icon code.
 *
 * Results:
 *      pointer to the image bits
 *
 * Side effects: None
 *
 *
 *----------------------------------------------------------------------
 */
static LPSTR FindDIBBits( LPSTR bitmapPtr )
{
   return ( bitmapPtr + *(LPULONG)bitmapPtr + PaletteSize( bitmapPtr ) );
}

/*
 *----------------------------------------------------------------------
 *
 * BytesPerLine --
 *
 *      Calculates the number of bytes in one scan line, as given by
 *      PBITMAPINFOHEADER2 lpBMIH - pointer to the BITMAPINFOHEADER
 *      that begins the CF_DIB block.  Used by titlebar icon code.
 *
 * Results:
 *      number of bytes in one scan line (ULONG aligned)
 *
 * Side effects: None
 *
 *
 *----------------------------------------------------------------------
 */
static ULONG BytesPerLine( PBITMAPINFOHEADER2 lpBMIH )
{
    return WIDTHBYTES(lpBMIH->biWidth * lpBMIH->biPlanes * lpBMIH->biBitCount);
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustIconImagePointers --
 *
 *      Adjusts internal pointers in icon resource struct, as given
 *      by LPICONIMAGE lpImage - the resource to handle.  Used by
 *      titlebar icon code.
 *
 * Results:
 *      BOOL - TRUE for success, FALSE for failure
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */
BOOL AdjustIconImagePointers( LPICONIMAGE lpImage )
{
    /*  Sanity check */
    if (lpImage==NULL)
        return FALSE;
    /*  BITMAPINFO is at beginning of bits */
    lpImage->bitmapPtr = (PBITMAPINFO2)lpImage->lpBits;
    /*  width - simple enough */
    lpImage->width = lpImage->bitmapPtr->bmiHeader.biWidth;
    /*  Icons are stored in funky format where height is doubled - account for it */
    lpImage->height = (lpImage->bitmapPtr->bmiHeader.biHeight)/2;
    /*  How many colors? */
    lpImage->colors = lpImage->bitmapPtr->bmiHeader.biPlanes * lpImage->bitmapPtr->bmiHeader.biBitCount;
    /*  XOR bits follow the header and color table */
    lpImage->lpXOR = (LPBYTE)FindDIBBits(((LPSTR)lpImage->bitmapPtr));
    /*  AND bits follow the XOR bits */
    lpImage->lpAND = lpImage->lpXOR + (lpImage->Height*BytesPerLine((PBITMAPINFOHEADER2)(lpImage->bitmapPtr)));
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeIconFromResource --
 *
 *      Construct an actual HPOINTER structure from the information
 *      in a resource.
 *
 * Results:
 *
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */
static HPOINTER MakeIconFromResource( LPICONIMAGE lpIcon ){
    HPOINTER hIcon ;
    static FARPROC pfnCreateIconFromResourceEx=NULL;
    static int initinfo=0;
    /*  Sanity Check */
    if (lpIcon == NULL)
        return NULL;
    if (lpIcon->lpBits == NULL)
        return NULL;
    if (!initinfo) {
        HMODULE hMod = GetModuleHandleA("USER32.DLL");
        initinfo=1;
        if(hMod){
            pfnCreateIconFromResourceEx = GetProcAddress(hMod,"CreateIconFromResourceEx");
        }
    }
    /*  Let the OS do the real work :) */
    if (pfnCreateIconFromResourceEx!=NULL) {
        hIcon = (HPOINTER) (pfnCreateIconFromResourceEx)
        (lpIcon->lpBits, lpIcon->dwNumBytes, TRUE, 0x00030000,
         (*(PBITMAPINFOHEADER2)(lpIcon->lpBits)).biWidth,
         (*(PBITMAPINFOHEADER2)(lpIcon->lpBits)).biHeight/2, 0 );
    } else {
         hIcon = NULL;
    }
    /*  It failed, odds are good we're on NT so try the non-Ex way */
    if (hIcon == NULL)    {
        /*  We would break on NT if we try with a 16bpp image */
        if (lpIcon->bitmapPtr->bmiHeader.biBitCount != 16) {
            hIcon = CreateIconFromResource( lpIcon->lpBits, lpIcon->dwNumBytes, TRUE, 0x00030000 );
        }
    }
    return hIcon;
}

/*
 *----------------------------------------------------------------------
 *
 * ReadICOHeader --
 *
 *      Reads the header from an ICO file, as specfied by channel.
 *
 * Results:
 *      int - Number of images in file, -1 for failure.
 *      If this succeeds, there is a decent chance this is a
 *      valid icon file.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */
static int ReadICOHeader( Tcl_Channel channel )
{
    WORD    Input;
    ULONG       bytesRead;

    /*  Read the 'reserved' WORD */
    bytesRead = Tcl_Read( channel, (char*)&Input, sizeof( WORD ));
    /*  Did we get a WORD? */
    if (bytesRead != sizeof( WORD ))
        return -1;
    /*  Was it 'reserved' ?   (ie 0) */
    if (Input != 0)
        return -1;
    /*  Read the type WORD */
    bytesRead = Tcl_Read( channel, (char*)&Input, sizeof( WORD ));
    /*  Did we get a WORD? */
    if (bytesRead != sizeof( WORD ))
        return -1;
    /*  Was it type 1? */
    if (Input != 1)
        return -1;
    /*  Get the count of images */
    bytesRead = Tcl_Read( channel, (char*)&Input, sizeof( WORD ));
    /*  Did we get a WORD? */
    if (bytesRead != sizeof( WORD ))
        return -1;
    /*  Return the count */
    return (int)Input;
}

/*
 *----------------------------------------------------------------------
 *
 * InitWindowClass --
 *
 *      This routine creates the Wm toplevel decorative frame class.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Registers a new window class.
 *
 *----------------------------------------------------------------------
 */
static int InitWindowClass(OS2IconPtr titlebarIcon) {
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    WNDCLASS * classPtr;
    classPtr = &toplevelClass;

    if (! tsdPtr->initialized) {
        tsdPtr->initialized = 1;
        tsdPtr->firstWindow = 1;
        tsdPtr->iconPtr = NULL;
    }
    if (! initialized) {
        Tcl_MutexLock(&os2WmMutex);
        if (! initialized) {
            initialized = 1;

            /*
             * When threads are enabled, we cannot use CLASSDC because
             * threads will then write into the same device context.
             *
             * This is a hack; we should add a subsystem that manages
             * device context on a per-thread basis.  See also tkWinX.c,
             * which also initializes a WNDCLASS structure.
             */

        #ifdef TCL_THREADS
            classPtr->style = CS_HREDRAW | CS_VREDRAW;
        #else
            classPtr->style = CS_HREDRAW | CS_VREDRAW | CS_CLASSDC;
        #endif
            classPtr->cbClsExtra = 0;
            classPtr->cbWndExtra = 0;
            classPtr->hInstance = Tk_GetHINSTANCE();
            classPtr->hbrBackground = NULL;
            classPtr->lpszMenuName = NULL;
            classPtr->lpszClassName = TK_WIN_TOPLEVEL_CLASS_NAME;
            classPtr->lpfnWndProc = WmProc;
            if (titlebarIcon == NULL) {
                classPtr->hIcon = LoadIcon(Tk_GetHINSTANCE(), "tk");
            } else {
                classPtr->hIcon = GetIcon(titlebarIcon, ICON_BIG);
                if (classPtr->hIcon == NULL) {
                    return TCL_ERROR;
                }
                /*
                 * Store pointer to default icon so we know when
                 * we need to free that information
                 */
                tsdPtr->iconPtr = titlebarIcon;
            }
            classPtr->hCursor = LoadCursor(NULL, IDC_ARROW);

            if (!RegisterClass(classPtr)) {
                panic("Unable to register TkTopLevel class");
            }
/*
            if (!WinRegisterClass(TclOS2GetHAB(), TOC_TOPLEVEL, WmProc,
                                  CS_SIZEREDRAW, 0)) {
                panic("Unable to register TkTopLevel class");
            }
*/
        }
        Tcl_MutexUnlock(&os2WmMutex);
    }
    return TCL_OK;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * InitWm --
 *
 *      This initialises the window manager.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Registers a new window class.
 *
 *----------------------------------------------------------------------
 */

static void
InitWm(void)
{
#ifdef VERBOSE
    printf("InitWm\n");
#endif
    /* Ignore return result */
#if 0
    (void) InitWindowClass(NULL);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * OS2SetIcon --
 *
 *      Sets either the default toplevel titlebar icon, or the icon
 *      for a specific toplevel (if tkw is given, then only that
 *      window is used).
 *
 *      The ref-count of the titlebarIcon is NOT changed.  If this
 *      function returns successfully, the caller should assume
 *      the icon was used (and therefore the ref-count should
 *      be adjusted to reflect that fact).  If the function returned
 *      an error, the caller should assume the icon was not used
 *      (and may wish to free the memory associated with it).
 *
 * Results:
 *      A standard Tcl return code.
 *
 * Side effects:
 *      One or all windows may have their icon changed.
 *      The Tcl result may be modified.
 *      The window-manager will be initialised if it wasn't already.
 *      The given window will be forced into existence.
 *
 *----------------------------------------------------------------------
 */
#if 0
static int
OS2SetIcon(interp, titlebarIcon, tkw)
    Tcl_Interp *interp;
    OS2IconPtr titlebarIcon;
    Tk_Window tkw;
{
    WmInfo *wmPtr;
    HWND hwnd;
    int application = 0;

    if (tkw == NULL) {
        tkw = Tk_MainWindow(interp);
        application = 1;
    }

    if (!(Tk_IsTopLevel(tkw))) {
        Tcl_AppendResult(interp, "window \"", Tk_PathName(tkw),
                "\" isn't a top-level window", (char *) NULL);
        return TCL_ERROR;
    }
    if (Tk_WindowId(tkw) == None) {
        Tk_MakeWindowExist(tkw);
    }
    /* We must get the window's wrapper, not the window itself */
    wmPtr = ((TkWindow*)tkw)->wmInfoPtr;
    hwnd = wmPtr->wrapper;

    if (application) {
        if (hwnd == NULL) {
            /*
             * I don't actually think this is ever the correct thing, unless
             * perhaps the window doesn't have a wrapper.  But I believe all
             * windows have wrappers.
             */
            hwnd = Tk_GetHWND(Tk_WindowId(tkw));
        }
        /*
         * If we aren't initialised, then just initialise with the user's
         * icon.  Otherwise our icon choice will be ignored moments later
         * when Tk finishes initialising.
         */
        if (!initialized) {
            if (InitWindowClass(titlebarIcon) != TCL_OK) {
                Tcl_AppendResult(interp,"Unable to set icon", (char*)NULL);
                return TCL_ERROR;
            }
        } else {
            ThreadSpecificData *tsdPtr;
            if ( !SetClassLong(hwnd, GCL_HPOINTERSM,
                        (MPARAM)GetIcon(titlebarIcon, ICON_SMALL))) {
                /*
                 * For some reason this triggers, even though it seems
                 * to be successful This is probably related to the
                 * WNDCLASS vs WNDCLASSEX difference.  Anyway it seems
                 * we have to ignore errors returned here.
                 */

                /*
                 * Tcl_AppendResult(interp,"Unable to set new small icon", (char*)NULL);
                 * return TCL_ERROR;
                 */
            }
            if ( !SetClassLong(hwnd, GCL_HPOINTER,
                        (MPARAM)GetIcon(titlebarIcon, ICON_BIG))) {
                Tcl_AppendResult(interp,"Unable to set new icon", (char*)NULL);
                return TCL_ERROR;
            }
            tsdPtr = (ThreadSpecificData *)
                    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
            if (tsdPtr->iconPtr != NULL) {
                DecrIconRefCount(tsdPtr->iconPtr);
            }
            tsdPtr->iconPtr = titlebarIcon;
        }
    } else {
        if (!initialized) {
            /*
             * Need to initialise the wm otherwise we will fail on
             * code which tries to set a toplevel's icon before that
             * happens.  Ignore return result.
             */
            (void)InitWindowClass(NULL);
        }
        /*
         * The following code is exercised if you do
         *
         *   toplevel .t ; wm titlebarIcon .t foo.icr
         *
         * i.e. the wm hasn't had time to properly create
         * the '.t' window before you set the icon.
         */
        if (hwnd == NULLHANDLE) {
            /*
             * This little snippet is copied from the 'Map' function,
             * and should probably be placed in one proper location
             */
            if (wmPtr->titleUid == NULL) {
                wmPtr->titleUid = wmPtr->winPtr->nameUid;
            }
            UpdateWrapper(wmPtr->winPtr);
            wmPtr = ((TkWindow*)tkw)->wmInfoPtr;
            hwnd = wmPtr->wrapper;
            if (hwnd == NULLHANDLE) {
                Tcl_AppendResult(interp,"Can't set icon; window has no wrapper.", (char*)NULL);
                return TCL_ERROR;
            }
        }
        SendMessage(hwnd,WM_SETICON,ICON_SMALL,(MPARAM)GetIcon(titlebarIcon, ICON_SMALL));
        SendMessage(hwnd,WM_SETICON,ICON_BIG,(MPARAM)GetIcon(titlebarIcon, ICON_BIG));

        /* Update the iconPtr we keep for each WmInfo structure. */
        if (wmPtr->iconPtr != NULL) {
            /* Free any old icon ptr which is associated with this window. */
            DecrIconRefCount(wmPtr->iconPtr);
        }
        /*
         * We do not need to increment the ref count for the
         * titlebarIcon, because it was already incremented when we
         * retrieved it.
         */
        wmPtr->iconPtr = titlebarIcon;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ReadIconFromFile --
 *
 *      Read the contents of a file (usually .ico, .icr) and extract an
 *      icon resource, if possible, otherwise NULL is returned, and an
 *      error message will already be in the interpreter.
 *
 * Results:
 *      A OS2IconPtr structure containing the icons in the file, with
 *      its ref count already incremented. The calling procedure should
 *      either place this structure inside a WmInfo structure, or it should
 *      pass it on to DecrIconRefCount() to ensure no memory leaks occur.
 *
 *      If the given fileName did not contain a valid icon structure,
 *      return NULL.
 *
 * Side effects:
 *      Memory is allocated for the returned structure and the icons
 *      it contains.  If the structure is not wanted, it should be
 *      passed to DecrIconRefCount, and in any case a valid ref count
 *      should be ensured to avoid memory leaks.
 *
 *      Currently icon resources are not shared, so the ref count of
 *      one of these structures will always be 0 or 1.  However all we
 *      need do is implement some sort of lookup function between
 *      filenames and OS2IconPtr structures and no other code will need
 *      to be changed.  The pseudo-code for this is implemented below
 *      in the 'if (0)' branch.  It did not seem necessary to implement
 *      this optimisation here, since moving to icon<->image
 *      conversions will probably make it obsolete.
 *
 *----------------------------------------------------------------------
 */
static OS2IconPtr
ReadIconFromFile(interp, fileName)
    Tcl_Interp *interp;
    char *fileName;
{
    OS2IconPtr titlebarIcon = NULL;

    if (0 /* If we already have an icon for this filename */) {
        titlebarIcon = NULL; /* Get the real value from a lookup */
        titlebarIcon->refCount++;
        return titlebarIcon;
    } else {
        BlockOfIconImagesPtr lpIR = ReadIconFromICOFile(interp, fileName);
        if (lpIR != NULL) {
            titlebarIcon = (OS2IconPtr) ckalloc(sizeof(OS2IconInstance));
            titlebarIcon->iconBlock = lpIR;
            titlebarIcon->refCount = 1;
        }
        return titlebarIcon;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DecrIconRefCount --
 *
 *      Reduces the reference count.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If the ref count falls to zero, free the memory associated
 *      with the icon resource structures.  In this case the pointer
 *      passed into this function is no longer valid.
 *
 *----------------------------------------------------------------------
 */
static void DecrIconRefCount(OS2IconPtr titlebarIcon) {
    titlebarIcon->refCount--;

    if (titlebarIcon->refCount <= 0) {
        if (titlebarIcon->iconBlock != NULL) {
            FreeIconBlock(titlebarIcon->iconBlock);
        }
        titlebarIcon->iconBlock = NULL;

        ckfree((char*)titlebarIcon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FreeIconBlock --
 *
 *      Frees all memory associated with a previously loaded
 *      titlebarIcon.  The icon block pointer is no longer
 *      valid once this function returns.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */
static void FreeIconBlock(BlockOfIconImagesPtr lpIR) {
    int i;

    /* Free all the bits */
    for (i=0; i< lpIR->nNumImages; i++) {
        if (lpIR->IconImages[i].lpBits != NULL) {
            ckfree((char*)lpIR->IconImages[i].lpBits);
        }
        if (lpIR->IconImages[i].hIcon != NULL) {
            DestroyIcon(lpIR->IconImages[i].hIcon);
        }
    }
    ckfree ((char*)lpIR);
}

/*
 *----------------------------------------------------------------------
 *
 * GetIcon --
 *
 *      Extracts an icon of a given size from an icon resource
 *
 * Results:
 *      Returns the icon, if found, else NULL.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */
static HPOINTER GetIcon(OS2IconPtr titlebarIcon, int icon_size) {
    BlockOfIconImagesPtr lpIR = titlebarIcon->iconBlock;
    if (lpIR == NULL) {
        return NULL;
    } else {
        unsigned int size = (icon_size == 0 ? 16 : 32);
        int i;

        for (i = 0; i < lpIR->nNumImages; i++) {
            /* Take the first or a 32x32 16 color icon*/
            if((lpIR->IconImages[i].height == size)
               && (lpIR->IconImages[i].width == size)
               && (lpIR->IconImages[i].colors >= 4)) {
                return lpIR->IconImages[i].hIcon;
            }
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ReadIconFromICOFile --
 *
 *      Reads an Icon Resource from an ICO file, as given by
 *      char* fileName - Name of the ICO file. This name should
 *      be in Utf format.
 *
 * Results:
 *      Returns an icon resource, if found, else NULL.
 *
 * Side effects:
 *      May leave error messages in the Tcl interpreter.
 *
 *----------------------------------------------------------------------
 */
BlockOfIconImagesPtr ReadIconFromICOFile(Tcl_Interp* interp, char* fileName){
    BlockOfIconImagesPtr        lpIR , lpNew ;
    Tcl_Channel         channel;
    int                 i;
    ULONG               bytesRead;
    LPICONDIRENTRY      lpIDE;

    /*  Open the file */
    if ((channel = Tcl_OpenFileChannel(interp, fileName, "r", 0)) == NULL) {
        Tcl_AppendResult(interp,"Error opening file \"", fileName,
                         "\" for reading",(char*)NULL);
        return NULL;
    }
    if (Tcl_SetChannelOption(interp, channel, "-translation", "binary")
            != TCL_OK) {
        Tcl_Close(NULL, channel);
        return NULL;
    }
    if (Tcl_SetChannelOption(interp, channel, "-encoding", "binary")
            != TCL_OK) {
        Tcl_Close(NULL, channel);
        return NULL;
    }
    /*  Allocate memory for the resource structure */
    if ((lpIR = (BlockOfIconImagesPtr) ckalloc( sizeof(BlockOfIconImages) )) == NULL)    {
        Tcl_AppendResult(interp,"Error allocating memory",(char*)NULL);
        Tcl_Close(NULL, channel);
        return NULL;
    }
    /*  Read in the header */
    if ((lpIR->nNumImages = ReadICOHeader( channel )) == -1)    {
        Tcl_AppendResult(interp,"Invalid file header",(char*)NULL);
        Tcl_Close(NULL, channel);
        ckfree((char*) lpIR );
        return NULL;
    }
    /*  Adjust the size of the struct to account for the images */
    if ((lpNew = (BlockOfIconImagesPtr) ckrealloc( (char*)lpIR, sizeof(BlockOfIconImages) + ((lpIR->nNumImages-1) * sizeof(ICONIMAGE)) )) == NULL)    {
        Tcl_AppendResult(interp,"Error allocating memory",(char*)NULL);
        Tcl_Close(NULL, channel);
        ckfree( (char*)lpIR );
        return NULL;
    }
    lpIR = lpNew;
    /*  Allocate enough memory for the icon directory entries */
    if ((lpIDE = (LPICONDIRENTRY) ckalloc( lpIR->nNumImages * sizeof( ICONDIRENTRY ) ) ) == NULL)     {
        Tcl_AppendResult(interp,"Error allocating memory",(char*)NULL);
        Tcl_Close(NULL, channel);
        ckfree( (char*)lpIR );
        return NULL;
    }
    /*  Read in the icon directory entries */
    bytesRead = Tcl_Read( channel, (char*)lpIDE, lpIR->nNumImages * sizeof( ICONDIRENTRY ));
    if (bytesRead != lpIR->nNumImages * sizeof( ICONDIRENTRY ))    {
        Tcl_AppendResult(interp,"Error reading file",(char*)NULL);
        Tcl_Close(NULL, channel);
        ckfree( (char*)lpIR );
        return NULL;
    }
    /*  Loop through and read in each image */
    for( i = 0; i < lpIR->nNumImages; i++ )    {
        /*  Allocate memory for the resource */
        if ((lpIR->IconImages[i].lpBits = (LPBYTE) ckalloc(lpIDE[i].dwBytesInRes)) == NULL)
        {
            Tcl_AppendResult(interp,"Error allocating memory",(char*)NULL);
            Tcl_Close(NULL, channel);
            ckfree( (char*)lpIR );
            ckfree( (char*)lpIDE );
            return NULL;
        }
        lpIR->IconImages[i].dwNumBytes = lpIDE[i].dwBytesInRes;
        /*  Seek to beginning of this image */
        if (Tcl_Seek(channel, lpIDE[i].dwImageOffset, FILE_BEGIN) == -1) {
            Tcl_AppendResult(interp,"Error seeking in file",(char*)NULL);
            Tcl_Close(NULL, channel);
            ckfree( (char*)lpIR );
            ckfree( (char*)lpIDE );
            return NULL;
        }
        /*  Read it in */
        bytesRead = Tcl_Read( channel, lpIR->IconImages[i].lpBits, lpIDE[i].dwBytesInRes);
        if (bytesRead != lpIDE[i].dwBytesInRes) {
            Tcl_AppendResult(interp,"Error reading file",(char*)NULL);
            Tcl_Close(NULL, channel);
            ckfree( (char*)lpIDE );
            ckfree( (char*)lpIR );
            return NULL;
        }
        /*  Set the internal pointers appropriately */
        if (!AdjustIconImagePointers( &(lpIR->IconImages[i]))) {
            Tcl_AppendResult(interp,"Error converting to internal format",(char*)NULL);
            Tcl_Close(NULL, channel);
            ckfree( (char*)lpIDE );
            ckfree( (char*)lpIR );
            return NULL;
        }
        lpIR->IconImages[i].hIcon=MakeIconFromResource(&(lpIR->IconImages[i]));
    }
    /*  Clean up */
    ckfree((char*)lpIDE);
    Tcl_Close(NULL, channel);
    if (lpIR == NULL){
        Tcl_AppendResult(interp,"Reading of ",fileName," failed!",(char*)NULL);
        return NULL;
    }
    return lpIR;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * GetTopLevel --
 *
 *      This function retrieves the TkWindow associated with the
 *      given HWND.
 *
 * Results:
 *      Returns the matching TkWindow.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static TkWindow *
GetTopLevel(hwnd)
    HWND hwnd;
{
#ifdef VERBOSE
    printf("GetTopLevel hwnd %x => %x\n", hwnd,
           WinQueryWindowULong(hwnd, QWL_USER));
#endif
    return (TkWindow *) WinQueryWindowULong(hwnd, QWL_USER);
}

/*
 *----------------------------------------------------------------------
 *
 * SetLimits --
 *
 *      Updates the minimum and maximum window size constraints for
 *      tracking.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Changes the values of the track pointer to reflect the current
 *      minimum and maximum size values.
 *
 *----------------------------------------------------------------------
 */

void
SetLimits(hwnd, trackPtr)
    HWND hwnd;
    TRACKINFO *trackPtr;
{
    register WmInfo *wmPtr;
    int maxWidth, maxHeight;
    int minWidth, minHeight;
    int base;
    TkWindow *winPtr = GetTopLevel(hwnd);
    SWP pos;

#ifdef VERBOSE
    printf("SetLimits hwnd %x, flags %x\n", hwnd, trackPtr->fs);
#endif

    if (winPtr == NULL) {
        return;
    }

    wmPtr = winPtr->wmInfoPtr;

    /* Get present size as default max/min */
    rc = WinQueryWindowPos(hwnd, &pos);
    /* Fill in defaults */
    trackPtr->cxBorder = trackPtr->cyBorder = 4; /* 4 pixels tracking */
    trackPtr->cxGrid = trackPtr->cyGrid = 1; /* smooth tracking */
    trackPtr->cxKeyboard = trackPtr->cyKeyboard = 8; /* fast keyboardtracking */
    trackPtr->rclTrack.xLeft = pos.x;
    trackPtr->rclTrack.yBottom = pos.y;
    trackPtr->rclTrack.xRight = pos.x + pos.cx;
    trackPtr->rclTrack.yTop = pos.y + pos.cy;
    trackPtr->rclBoundary.xLeft = 0;
    trackPtr->rclBoundary.yBottom = 0;
    trackPtr->rclBoundary.xRight = xScreen;
    trackPtr->rclBoundary.yTop = yScreen;
    trackPtr->ptlMinTrackSize.x = 0;
    trackPtr->ptlMinTrackSize.y = 0;
    trackPtr->ptlMaxTrackSize.x = xScreen;
    trackPtr->ptlMaxTrackSize.y = yScreen;
    
    /*
     * Copy latest constraint info.
     */

    wmPtr->defMinWidth = trackPtr->ptlMinTrackSize.x;
    wmPtr->defMinHeight = trackPtr->ptlMinTrackSize.y;
    wmPtr->defMaxWidth = trackPtr->ptlMaxTrackSize.x;
    wmPtr->defMaxHeight = trackPtr->ptlMaxTrackSize.y;

#ifdef VERBOSE
    printf("SetLimits, defMin %dx%d min %dx%d defMax %dx%d max %dx%d\n",
           wmPtr->defMinWidth, wmPtr->defMinHeight,
           wmPtr->minWidth, wmPtr->minHeight,
           wmPtr->defMaxWidth, wmPtr->defMaxHeight,
           wmPtr->minWidth, wmPtr->minHeight);
    printf("    trackinfo: cxBorder %d, cyBorder %d, cxGrid %d, cyGrid %d,
           cxKeyboard %d, cyKeyboard %d,
           rclTrack (%d,%d->%d,%d), rclBoundary (%d,%d->%d,%d),
           ptlMinTrackSize (%d,%d), ptlMaxTrackSize (%d,%d)\n",
           trackPtr->cxBorder, trackPtr->cyBorder,
           trackPtr->cxGrid, trackPtr->cyGrid,
           trackPtr->cxKeyboard, trackPtr->cyKeyboard, trackPtr->rclTrack.xLeft,
           trackPtr->rclTrack.yBottom, trackPtr->rclTrack.xRight,
           trackPtr->rclTrack.yTop, trackPtr->rclBoundary.xLeft,
           trackPtr->rclBoundary.yBottom, trackPtr->rclBoundary.xRight,
           trackPtr->rclBoundary.yTop, trackPtr->ptlMinTrackSize.x,
           trackPtr->ptlMinTrackSize.y, trackPtr->ptlMaxTrackSize.x,
           trackPtr->ptlMaxTrackSize.y);
#endif

    GetMaxSize(wmPtr, &maxWidth, &maxHeight);
    GetMinSize(wmPtr, &minWidth, &minHeight);
    
    if (wmPtr->gridWin != NULL) {
        base = winPtr->reqWidth - (wmPtr->reqGridWidth * wmPtr->widthInc);
        if (base < 0) {
            base = 0;
        }
        base += wmPtr->borderWidth;
        trackPtr->ptlMinTrackSize.x = base + (minWidth * wmPtr->widthInc);
        trackPtr->ptlMaxTrackSize.x = base + (maxWidth * wmPtr->widthInc);

        base = winPtr->reqHeight - (wmPtr->reqGridHeight * wmPtr->heightInc);
        if (base < 0) {
            base = 0;
        }
        base += wmPtr->borderHeight;
        trackPtr->ptlMinTrackSize.y = base + (minHeight * wmPtr->heightInc);
        trackPtr->ptlMaxTrackSize.y = base + (maxHeight * wmPtr->heightInc);
    } else {
        trackPtr->ptlMaxTrackSize.x = maxWidth + wmPtr->borderWidth;
        trackPtr->ptlMaxTrackSize.y = maxHeight + wmPtr->borderHeight;
        trackPtr->ptlMinTrackSize.x = minWidth + wmPtr->borderWidth;
        trackPtr->ptlMinTrackSize.y = minHeight + wmPtr->borderHeight;
    }

    /*
     * If the window isn't supposed to be resizable, then set the
     * minimum and maximum dimensions to be the same as the current size.
     */

    if (!(wmPtr->flags & WM_SYNC_PENDING)) {
        if (wmPtr->flags & WM_WIDTH_NOT_RESIZABLE) {
            trackPtr->ptlMinTrackSize.x = winPtr->changes.width +
                                          wmPtr->borderWidth;
            trackPtr->ptlMaxTrackSize.x = trackPtr->ptlMinTrackSize.x;
        }
        if (wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE) {
            trackPtr->ptlMinTrackSize.y = winPtr->changes.height +
                                          wmPtr->borderHeight;
            trackPtr->ptlMaxTrackSize.y = trackPtr->ptlMinTrackSize.y;
        }
    }
#ifdef VERBOSE
    printf("    now: cxBorder %d, cyBorder %d, cxGrid %d, cyGrid %d,
           cxKeyboard %d, cyKeyboard %d,
           rclTrack (%d,%d->%d,%d), rclBoundary (%d,%d->%d,%d),
           ptlMinTrackSize (%d,%d), ptlMaxTrackSize (%d,%d)\n",
           trackPtr->cxBorder, trackPtr->cyBorder,
           trackPtr->cxGrid, trackPtr->cyGrid,
           trackPtr->cxKeyboard, trackPtr->cyKeyboard, trackPtr->rclTrack.xLeft,
           trackPtr->rclTrack.yBottom, trackPtr->rclTrack.xRight,
           trackPtr->rclTrack.yTop, trackPtr->rclBoundary.xLeft,
           trackPtr->rclBoundary.yBottom, trackPtr->rclBoundary.xRight,
           trackPtr->rclBoundary.yTop, trackPtr->ptlMinTrackSize.x,
           trackPtr->ptlMinTrackSize.y, trackPtr->ptlMaxTrackSize.x,
           trackPtr->ptlMaxTrackSize.y);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2WmCleanup --
 *
 *      Unregisters classes registered by the window manager. This is
 *      called from the DLL main entry point when the DLL is unloaded.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The window classes are discarded.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2WmCleanup(hInstance)
    HMODULE hInstance;
{
    ThreadSpecificData *tsdPtr;

    /*
     * If we're using stubs to access the Tcl library, and they
     * haven't been initialized, we can't call Tcl_GetThreadData.
     */

#ifdef USE_TCL_STUBS
    if (tclStubsPtr == NULL) {
        return;
    }
#endif

    tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (!tsdPtr->initialized) {
        return;
    }
    tsdPtr->initialized = 0;
}

/*
 *--------------------------------------------------------------
 *
 * TkWmNewWindow --
 *
 *	This procedure is invoked whenever a new top-level
 *	window is created.  Its job is to initialize the WmInfo
 *	structure for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A WmInfo structure gets allocated and initialized.
 *
 *--------------------------------------------------------------
 */

void
TkWmNewWindow(winPtr)
    TkWindow *winPtr;		/* Newly-created top-level window. */
{
    register WmInfo *wmPtr;

    wmPtr = (WmInfo *) ckalloc(sizeof(WmInfo));
#ifdef VERBOSE
    printf("TkWmNewWindow, wmPtr %x\n", wmPtr);
#endif
    winPtr->wmInfoPtr = wmPtr;
    wmPtr->winPtr = winPtr;
    wmPtr->wrapper = NULLHANDLE;
    wmPtr->titleUid = NULL;
    wmPtr->iconName = NULL;
    wmPtr->masterPtr = NULL;
    wmPtr->hints.flags = InputHint | StateHint;
    wmPtr->hints.input = True;
    wmPtr->hints.initial_state = NormalState;
    wmPtr->hints.icon_pixmap = None;
    wmPtr->hints.icon_window = None;
    wmPtr->hints.icon_x = wmPtr->hints.icon_y = 0;
    wmPtr->hints.icon_mask = None;
    wmPtr->hints.window_group = None;
    wmPtr->leaderName = NULL;
    wmPtr->icon = NULL;
    wmPtr->iconFor = NULL;
    wmPtr->sizeHintsFlags = 0;

    /*
     * Default the maximum dimensions to the size of the display.
     */

    wmPtr->defMinWidth = wmPtr->defMinHeight = 0;
    wmPtr->defMaxWidth = DisplayWidth(winPtr->display,
            winPtr->screenNum);
    wmPtr->defMaxHeight = DisplayHeight(winPtr->display,
            winPtr->screenNum);
    wmPtr->minWidth = wmPtr->minHeight = 1;
    wmPtr->maxWidth = wmPtr->maxHeight = 0;
    wmPtr->gridWin = NULL;
    wmPtr->widthInc = wmPtr->heightInc = 1;
    wmPtr->minAspect.x = wmPtr->minAspect.y = 1;
    wmPtr->maxAspect.x = wmPtr->maxAspect.y = 1;
    wmPtr->reqGridWidth = wmPtr->reqGridHeight = -1;
    wmPtr->gravity = NorthWestGravity;
    wmPtr->width = -1;
    wmPtr->height = -1;
    wmPtr->hMenu = NULLHANDLE;
    wmPtr->x = winPtr->changes.x;
    wmPtr->y = winPtr->changes.y;

#ifdef VERBOSE
    printf("TkWmNewWindow, x %d y %d\n", wmPtr->x, wmPtr->y);
#endif
    wmPtr->borderWidth = -1;
    wmPtr->borderHeight = -1;

    wmPtr->cmapList = NULL;
    wmPtr->cmapCount = 0;
    wmPtr->numTransients = 0;

    wmPtr->configWidth = -1;
    wmPtr->configHeight = -1;
    wmPtr->protPtr = NULL;
    wmPtr->cmdArgv = NULL;
    wmPtr->clientMachine = NULL;
    wmPtr->flags = WM_NEVER_MAPPED;
#if 0
    wmPtr->iconPtr = NULL;
#endif
    wmPtr->nextPtr = winPtr->dispPtr->firstWmPtr;
    winPtr->dispPtr->firstWmPtr = wmPtr;

    /*
     * Tk must monitor structure events for top-level windows, in order
     * to detect size and position changes caused by window managers.
     */

    Tk_CreateEventHandler((Tk_Window) winPtr, StructureNotifyMask,
            TopLevelEventProc, (ClientData) winPtr);

    /*
     * Arrange for geometry requests to be reflected from the window
     * to the window manager.
     */

    Tk_ManageGeometry((Tk_Window) winPtr, &wmMgrType, (ClientData) 0);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateWrapper --
 *
 *      This function creates the wrapper window that contains the
 *      window decorations and menus for a toplevel.  This function
 *      may be called after a window is mapped to change the window
 *      style.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys any old wrapper window and replaces it with a newly
 *      created wrapper.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateWrapper(winPtr)
    TkWindow *winPtr;           /* Top-level window to redecorate. */
{
    register WmInfo *wmPtr = winPtr->wmInfoPtr;
    HWND parentHWND, oldWrapper;
    HWND child;
    int x, y, width, height, state;
#if 0
    HPOINTER hSmallIcon = NULL;
    HPOINTER hBigIcon = NULL;
#endif
    Tcl_DString titleString;
    int *childStateInfo = NULL;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    SWP place;
    printf("UpdateWrapper winPtr %x, child %x, state %s\n", winPtr, child,
           wmPtr->hints.initial_state == NormalState ? "NormalState" :
           (wmPtr->hints.initial_state == IconicState ?  "IconicState" :
           (wmPtr->hints.initial_state == ZoomState ? "ZoomState" :
           (wmPtr->hints.initial_state == WithdrawnState ? "WithdrawnState" :
            "unknown"))));
#endif

    if (winPtr->window == None) {
        /*
         * Ensure existence of the window to update the wrapper for.
         */
        Tk_MakeWindowExist((Tk_Window) winPtr);
    }

    child = TkOS2GetHWND(winPtr->window);
    parentHWND = HWND_DESKTOP;

    if (winPtr->flags & TK_EMBEDDED) {
        wmPtr->wrapper = (HWND) winPtr->privatePtr;
        if (wmPtr->wrapper == NULLHANDLE) {
            panic("UpdateWrapper: Cannot find container window");
        }
        if (!WinIsWindow(TclOS2GetHAB(), wmPtr->wrapper)) {
            panic("UpdateWrapper: Container was destroyed");
        }

    } else {
        FRAMECDATA fcdata;
        /*
         * Pick the decorative frame style.
         * Override redirect windows get created as undecorated popups.
         * Transient windows get a modal dialog frame.
         * Neither override, nor transient windows appear in the tasklist.
         * Note that a transient window does not resize
         * by default, so we need to explicitly add the FCF_SIZEBORDER frame
         * control flag and remove the FCF_DLGBORDER flag if we want it to be
         * resizeable.
         */

        if (winPtr->atts.override_redirect) {
            wmPtr->style = WM_OVERRIDE_STYLE;
            wmPtr->exStyle = EX_OVERRIDE_STYLE;
        } else if (wmPtr->masterPtr) {
            wmPtr->style = WM_TRANSIENT_STYLE;
            wmPtr->exStyle = EX_TRANSIENT_STYLE;
            parentHWND = Tk_GetHWND(Tk_WindowId(wmPtr->masterPtr));
            if (! ((wmPtr->flags & WM_WIDTH_NOT_RESIZABLE) &&
                    (wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE))) {
                wmPtr->exStyle &= ~FCF_DLGBORDER;
                wmPtr->exStyle |= FCF_SIZEBORDER;
            }
        } else {
            wmPtr->style = WM_TOPLEVEL_STYLE;
            wmPtr->exStyle = EX_TOPLEVEL_STYLE;
        }

        /*
         * Compute the geometry of the parent and child windows.
         */

        wmPtr->flags |= WM_CREATE_PENDING|WM_MOVE_PENDING;
        UpdateGeometryInfo((ClientData)winPtr);
        wmPtr->flags &= ~(WM_CREATE_PENDING|WM_MOVE_PENDING);

        width = wmPtr->borderWidth + winPtr->changes.width;
        height = wmPtr->borderHeight + winPtr->changes.height;
#ifdef VERBOSE
        printf("width: %d + %d => %d\nheight: %d + %d => %d\n",
               wmPtr->borderWidth, winPtr->changes.width, width,
               wmPtr->borderHeight, winPtr->changes.height, height);
#endif

        /*
         * Set the initial position from the user or program specified
         * location.  If nothing has been specified, then let the system
         * pick a location.
         */

        if (!(wmPtr->sizeHintsFlags & (USPosition | PPosition))
                && (wmPtr->flags & WM_NEVER_MAPPED)) {
            SWP recommendedPos;
            rc = WinQueryTaskSizePos(TclOS2GetHAB(), 0L, &recommendedPos);
            if (rc == 0) {
                x = recommendedPos.x;
                y = recommendedPos.y;
#ifdef VERBOSE
                printf("Positioning: WinQueryTaskSizePos OK: (%d,%d)\n", x, y);
#endif
            } else {
                x = winPtr->changes.x;
                y = winPtr->changes.y;
#ifdef VERBOSE
                printf("Positioning: WinQueryTaskSizePos ERROR: (%d,%d)\n",
                       x, y);
#endif
            }
        } else {
            x = winPtr->changes.x;
            y = winPtr->changes.y;
        }

        /*
         * Create the containing window, and set the user data to point
         * to the TkWindow.
         */

        Tcl_UtfToExternalDString(NULL, wmPtr->titleUid, -1, &titleString);

        fcdata.cb = sizeof(FRAMECDATA);
        fcdata.flCreateFlags = (ULONG)wmPtr->exStyle;
        fcdata.hmodResources = 0L;
        fcdata.idResources = 0;
        /*
         * Tk Dialogs as children of a toplevel and therefore limited to
         * the space that toplevel offers is dumb in my opinion (and it
         * wasn't that way in 4.2. The only reason I see for it would be
         * in the Tcl Plugin, but that probably doesn't work anyway.
         * I therefore always create a Wm window the way I think it should
         * be as a "Window Manager window": as a toplevel = child of the
         * desktop. Anybody disagreeing with me can change the HWND_DESKTOP
         * to parentHWND to get the Windows equivalent.
         */
#define ID_FRAME 1
        wmPtr->wrapper  = WinCreateWindow(
                HWND_DESKTOP,                   /* Parent */
                WC_FRAME,                       /* Class */
                Tcl_DStringValue(&titleString), /* Window text */
                wmPtr->style,                   /* Style */
                x, yScreen - height - y,        /* Initial X and Y coords */
                width,                          /* Width */
                height,                         /* Height */
                NULLHANDLE,                     /* Owner */
                HWND_TOP,                       /* Insertbehind (sibling) */
                ID_FRAME,                       /* Window ID */
                &fcdata,                        /* Ptr to control data */
                NULL);                          /* Ptr to presentation params */
        Tcl_DStringFree(&titleString);

#ifdef VERBOSE
        printf("WinCreateWindow frame %x (%s) wmPtr %x (%d,%d) %dx%d\n",
               wmPtr->wrapper, wmPtr->titleUid, wmPtr, x, yScreen - height - y,
               width, height);
        printf("     exStyle %x (%s) style %x (%s)\n", wmPtr->exStyle,
               (wmPtr->exStyle & ~FCF_SHELLPOSITION) == EX_TOPLEVEL_STYLE
                                                     ? "EX_TOPLEVEL_STYLE" :
               ((wmPtr->exStyle & ~FCF_SHELLPOSITION) == EX_OVERRIDE_STYLE
                                                     ? "EX_OVERRIDE_STYLE" :
               ((wmPtr->exStyle & ~FCF_SHELLPOSITION) == EX_TRANSIENT_STYLE
                                                     ? "EX_TRANSIENT_STYLE"
                                                     : "unknown")),
               wmPtr->style,
               (wmPtr->style == WM_TOPLEVEL_STYLE ? "WM_TOPLEVEL_STYLE" :
               (wmPtr->style == WM_OVERRIDE_STYLE ? "WM_OVERRIDE_STYLE" :
               (wmPtr->style == WM_TRANSIENT_STYLE ? "WM_TRANSIENT_STYLE"
                                                   : "unknown"))));
#endif

        /* Subclass Frame window */
        oldFrameProc = WinSubclassWindow(wmPtr->wrapper, WmProc);
        /* Force frame window to display the title bar, buttons, ... */
        WinSendMsg(wmPtr->wrapper, WM_UPDATEFRAME, MPVOID, MPVOID);
        WinSetWindowULong(wmPtr->wrapper, QWL_USER, (ULONG)winPtr);

#ifdef VERBOSE
        rc = WinQueryWindowPos(wmPtr->wrapper, &place);
        if (rc == TRUE) {
            printf("    WinQueryWindowPos frame %x OK: (%d,%d) %dx%d\n",
                   wmPtr->wrapper, place.x, place.y, place.cx, place.cy);
        } else {
            printf("    WinQueryWindowPos frame %x ERROR %x\n", wmPtr->wrapper,
                   WinGetLastError(TclOS2GetHAB()));
        }
#endif

        TkInstallFrameMenu((Tk_Window) winPtr);
    }

    /*
     * Now we need to reparent the contained window.
     */

    if (winPtr->flags & TK_EMBEDDED) {
        WinSetWindowPtr(child, QWP_PFNWP, (PVOID) TopLevelProc);
    }
    /* Determine old parent */
    oldWrapper = WinQueryWindow(child, QW_PARENT);
    rc = WinSetParent(child, wmPtr->wrapper, TRUE);
#ifdef VERBOSE
    printf("    WinSetParent(child %x, parent %x, TRUE) returns %d\n", child,
           wmPtr->wrapper, rc);
    rc = WinQueryWindowPos(child, &place);
    if (rc == TRUE) {
        printf("    WinQueryWindowPos child %x OK: (%d,%d) %dx%d\n",
               child, place.x, place.y, place.cx, place.cy);
    } else {
        printf("    WinQueryWindowPos child %x ERROR %x\n", child,
               WinGetLastError(TclOS2GetHAB()));
    }
#endif
#if 0
    if (oldWrapper != NULLHANDLE) {
/* WinQueryPointer */
        hSmallIcon = (HPOINTER) SendMessage(oldWrapper,WM_GETICON,ICON_SMALL,
                                         (MPARAM)NULL);
        hBigIcon = (HPOINTER) SendMessage(oldWrapper,WM_GETICON,ICON_BIG,
                                       (MPARAM)NULL);
    }
#endif
    if (oldWrapper != NULLHANDLE && (oldWrapper != wmPtr->wrapper)
         && (oldWrapper != WinQueryDesktopWindow(TclOS2GetHAB(), NULLHANDLE))) {
        HWND menu;

        WinSetWindowULong(oldWrapper, QWL_USER, (ULONG) NULL);

        if (wmPtr->numTransients > 0) {
            /*
             * Unset the current wrapper as the parent for all transient
             * children for whom this is the master
             */
            WmInfo *wmPtr2;

            childStateInfo = (int *)ckalloc((unsigned) wmPtr->numTransients
                * sizeof(int));
            state = 0;
            for (wmPtr2 = winPtr->dispPtr->firstWmPtr; wmPtr2 != NULL;
                 wmPtr2 = wmPtr2->nextPtr) {
                if (wmPtr2->masterPtr == winPtr) {
                    if (!(wmPtr2->flags & WM_NEVER_MAPPED)) {
                        childStateInfo[state++] = wmPtr2->hints.initial_state;
                        WinSetParent(TkOS2GetHWND(wmPtr2->winPtr->window),
                                     HWND_DESKTOP, TRUE);
                        WinSetOwner(TkOS2GetHWND(wmPtr2->winPtr->window),
                                    NULLHANDLE);
                    }
                }
            }
        }

        /*
         * Remove the menubar before destroying the window so the menubar
         * isn't destroyed.
         */

        /* Set parent and owner to HWND_DESKTOP */
        menu = WinWindowFromID(oldWrapper, FID_MENU);
        if (menu != NULLHANDLE) {
            rc = WinSetParent(menu, HWND_DESKTOP, FALSE);
#ifdef VERBOSE
            printf("    WinSetParent(menu %x, HWND_DESKTOP,FALSE) returns %d\n",
                   menu, rc);
#endif
            WinSetOwner(menu, NULLHANDLE);
        }
        WinDestroyWindow(oldWrapper);
    }

    wmPtr->flags &= ~WM_NEVER_MAPPED;
#ifdef VERBOSE
    printf("    before TK_ATTACHWINDOW\n");
    fflush(stdout);
#endif
    WinSendMsg(wmPtr->wrapper, TK_ATTACHWINDOW, (MPARAM) child, MPVOID);
#ifdef VERBOSE
    printf("    after TK_ATTACHWINDOW\n");
    fflush(stdout);
#endif
/*
*/

    /*
     * Force an initial transition from withdrawn to the real
     * initial state.
     */

    state = wmPtr->hints.initial_state;
    wmPtr->hints.initial_state = WithdrawnState;
    TkpWmSetState(winPtr, state);

#if 0
    if (hSmallIcon != NULL) {
        SendMessage(wmPtr->wrapper,WM_SETICON,ICON_SMALL,(MPARAM)hSmallIcon);
    }
    if (hBigIcon != NULL) {
        SendMessage(wmPtr->wrapper,WM_SETICON,ICON_BIG,(MPARAM)hBigIcon);
    }
#endif

    /*
     * If we are embedded then force a mapping of the window now,
     * because we do not necessarily own the wrapper and may not
     * get another opportunity to map ourselves. We should not be
     * in either iconified or zoomed states when we get here, so
     * it is safe to just check for TK_EMBEDDED without checking
     * what state we are supposed to be in (default to NormalState).
     */

    if (winPtr->flags & TK_EMBEDDED) {
        XMapWindow(winPtr->display, winPtr->window);
    }

    /*
     * Set up menus on the wrapper if required.
     */

    if (wmPtr->hMenu != NULLHANDLE) {
        wmPtr->flags = WM_SYNC_PENDING;
        rc = WinSetOwner(wmPtr->hMenu, wmPtr->wrapper);
#ifdef VERBOSE
        printf("    WinSetOwner(menu %x, parent %x) returns %d\n",
               wmPtr->hMenu, wmPtr->wrapper, rc);
#endif
        rc = WinSetParent(wmPtr->hMenu, wmPtr->wrapper, FALSE);
#ifdef VERBOSE
        printf("    WinSetParent(menu %x, parent %x, FALSE) returns %d\n",
               wmPtr->hMenu, wmPtr->wrapper, rc);
#endif
        rc = (LONG) WinSendMsg(wmPtr->wrapper, WM_UPDATEFRAME,
                               MPFROMLONG(FCF_MENU), MPVOID);
#ifdef VERBOSE
        printf("    WinSendMsg(%x, WM_UPDATEFRAME, FCF_MENU %x) returns %d\n",
               wmPtr->wrapper, FCF_MENU, rc);
#endif
        wmPtr->flags &= ~WM_SYNC_PENDING;
    }

    if (childStateInfo) {
        if (wmPtr->numTransients > 0) {
            /*
             * Reset all transient children for whom this is the master
             */
            WmInfo *wmPtr2;

            state = 0;
            for (wmPtr2 = winPtr->dispPtr->firstWmPtr; wmPtr2 != NULL;
                 wmPtr2 = wmPtr2->nextPtr) {
                if (wmPtr2->masterPtr == winPtr) {
                    if (!(wmPtr2->flags & WM_NEVER_MAPPED)) {
                        UpdateWrapper(wmPtr2->winPtr);
                        TkpWmSetState(wmPtr2->winPtr, childStateInfo[state++]);
                    }
                }
            }
        }

        ckfree((char *) childStateInfo);
    }

    /*
     * If this is the first window created by the application, then
     * we should activate the initial window.
     */

    if (tsdPtr->firstWindow) {
        tsdPtr->firstWindow = 0;
#ifdef VERBOSE
        printf("    before WinSetActiveWindow %x\n", wmPtr->wrapper);
#endif
        rc = WinSetActiveWindow(HWND_DESKTOP, wmPtr->wrapper);
#ifdef VERBOSE
        if (rc == TRUE) {
             printf("    WinSetActiveWindow %x OK\n", wmPtr->wrapper);
        } else {
             printf("    WinSetActiveWindow %x ERROR %x\n", wmPtr->wrapper,
                    WinGetLastError(TclOS2GetHAB()));
        }
#endif
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkWmMapWindow --
 *
 *      This procedure is invoked to map a top-level window.  This
 *      module gets a chance to update all window-manager-related
 *      information in properties before the window manager sees
 *      the map event and checks the properties.  It also gets to
 *      decide whether or not to even map the window after all.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Properties of winPtr may get updated to provide up-to-date
 *      information to the window manager.  The window may also get
 *      mapped, but it may not be if this procedure decides that
 *      isn't appropriate (e.g. because the window is withdrawn).
 *
 *--------------------------------------------------------------
 */

void
TkWmMapWindow(winPtr)
    TkWindow *winPtr;           /* Top-level window that's about to
                                 * be mapped. */
{
    register WmInfo *wmPtr = winPtr->wmInfoPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("TkWmMapWindow winPtr %x\n", winPtr);
#endif

    if (!tsdPtr->initialized) {
        InitWm();
    }

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        if (wmPtr->hints.initial_state == WithdrawnState) {
            return;
        }

        /*
         * Map the window in either the iconified or normal state.  Note that
         * we only send a map event if the window is in the normal state.
         */

        TkpWmSetState(winPtr, wmPtr->hints.initial_state);
    }

    /*
     * This is the first time this window has ever been mapped.
     * Store all the window-manager-related information for the
     * window.
     */

    if (wmPtr->titleUid == NULL) {
        wmPtr->titleUid = winPtr->nameUid;
    }
    UpdateWrapper(winPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TkWmUnmapWindow --
 *
 *	This procedure is invoked to unmap a top-level window.  The
 *	only thing it does special is unmap the decorative frame before
 *	unmapping the toplevel window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unmaps the decorative frame and the window.
 *
 *--------------------------------------------------------------
 */

void
TkWmUnmapWindow(winPtr)
    TkWindow *winPtr;		/* Top-level window that's about to
        			 * be unmapped. */
{
#ifdef VERBOSE
    printf("TkWmUnmapWindow winPtr %x\n", winPtr);
#endif
    TkpWmSetState(winPtr, WithdrawnState);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWmSetState --
 *
 *      Sets the window manager state for the wrapper window of a
 *      given toplevel window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May maximize, minimize, restore, or withdraw a window.
 *
 *----------------------------------------------------------------------
 */

void
TkpWmSetState(winPtr, state)
     TkWindow *winPtr;          /* Toplevel window to operate on. */
     int state;                 /* One of IconicState, ZoomState, NormalState,
                                 * or WithdrawnState. */
{
    register WmInfo *wmPtr = winPtr->wmInfoPtr;
    ULONG flags = 0;

#ifdef VERBOSE
    SWP pos;

    printf("TkpWmSetState %s winPtr %x state %s\n",
           wmPtr->flags & WM_NEVER_MAPPED ? "WM_NEVER_MAPPED" : "mapped",
           winPtr, state == IconicState ? "IconicState"
                         : (state == ZoomState ? "ZoomState"
                                  : (state == NormalState ? "NormalState"
                                           : "WithdrawnState" )));
#endif

    if ((wmPtr->flags & WM_NEVER_MAPPED) ||
            (wmPtr->masterPtr && !Tk_IsMapped(wmPtr->masterPtr))) {
        wmPtr->hints.initial_state = state;
        return;
    }

    if (wmPtr->flags & WM_UPDATE_PENDING) {
        Tk_CancelIdleCall(UpdateGeometryInfo, (ClientData) winPtr);
    }
    UpdateGeometryInfo((ClientData) winPtr);

#ifdef VERBOSE
    rc = WinQueryWindowPos(wmPtr->wrapper, &pos);
    if (rc != TRUE) {
        printf("TkpWmSetState win %x h %x %s oldpos ERROR %x (%d,%d;%dx%d)\n",
               winPtr, wmPtr->wrapper,
               state == NormalState ? "NormalState" : (state == IconicState ?
               "IconicState" : (state == ZoomState ? "ZoomState" :
               (state == WithdrawnState ? "WithdrawnState" : "unknown"))),
               WinGetLastError(TclOS2GetHAB()),
               pos.x, pos.y, pos.cx, pos.cy);
    } else {
        printf("TkpWmSetState win %x h %x %s, oldpos OK (%d,%d;%dx%d)\n",
               winPtr, wmPtr->wrapper,
               state == NormalState ? "NormalState" : (state == IconicState ?
               "IconicState" : (state == ZoomState ? "ZoomState" :
               (state == WithdrawnState ? "WithdrawnState" : "unknown"))),
               pos.x, pos.y, pos.cx, pos.cy);
    }
#endif

    wmPtr->flags |= WM_SYNC_PENDING;
    if (state == WithdrawnState) {
        /*
        rc = WinShowWindow(wmPtr->wrapper, FALSE);
#ifdef VERBOSE
        if (rc==TRUE) printf("    WinShowWindow FALSE %x OK\n", wmPtr->wrapper);
        else printf("    WinShowWindow FALSE %x ERROR: %x\n", wmPtr->wrapper,
                    WinGetLastError(TclOS2GetHAB()));
#endif
        */
        flags |= SWP_HIDE;
    } else if (state == IconicState) {
        flags |= SWP_MINIMIZE | SWP_SHOW;
    } else if (state == NormalState) {
        /*
        rc = WinShowWindow(wmPtr->wrapper, TRUE);
#ifdef VERBOSE
        if (rc==TRUE) {
            printf("    WinShowWindow TRUE %x OK\n", wmPtr->wrapper);
        }
        else {
            printf("    WinShowWindow TRUE %x ERROR: %x\n", wmPtr->wrapper,
                    WinGetLastError(TclOS2GetHAB()));
        }
#endif
        */
        flags |= SWP_RESTORE | SWP_SHOW | SWP_ZORDER;
    } else if (state == ZoomState) {
        flags |= SWP_MAXIMIZE | SWP_SHOW | SWP_ZORDER;
    }

    if (flags != 0) {
#ifdef VERBOSE
        printf("    before WinSetWindowPos (%x) %x\n", flags, wmPtr->wrapper);
        fflush(stdout);
#endif
        rc = WinSetWindowPos(wmPtr->wrapper, HWND_TOP, 0L, 0L, 0L, 0L, flags);
#ifdef VERBOSE
        if (rc==TRUE) printf("    WinSetWindowPos (%x) %x OK\n", flags,
                             wmPtr->wrapper);
        else printf("    WinSetWindowPos (%x) %x ERROR: %x\n", flags,
                    wmPtr->wrapper, WinGetLastError(TclOS2GetHAB()));
#endif
    }

    /* If applicable, add to/remove from task list */
    if ((wmPtr->exStyle & FCF_TASKLIST) && wmPtr->wrapper != NULLHANDLE) {
        HSWITCH hSwitch;
        SWCNTRL switchData;
        hSwitch = WinQuerySwitchHandle(wmPtr->wrapper, 0);
#ifdef VERBOSE
        if (hSwitch == NULLHANDLE) {
            printf("WinQuerySwitchHandle %x ERROR %x\n", wmPtr->wrapper,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("WinQuerySwitchHandle %x OK:%x\n", wmPtr->wrapper, hSwitch);
        }
#endif
        if (hSwitch == NULLHANDLE) goto end;
        rc = WinQuerySwitchEntry(hSwitch, &switchData);
#ifdef VERBOSE
        if (rc != 0) {
            printf("WinQuerySwitchEntry %x ERROR %x\n", hSwitch,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("WinQuerySwitchEntry %x OK\n", hSwitch);
        }
#endif
        if (rc != 0) goto end;
        if (state == WithdrawnState) {
            /* Set visibility off */
            switchData.uchVisibility = SWL_INVISIBLE;
        } else {
            /* Set visibility on */
            switchData.uchVisibility = SWL_VISIBLE;
        }
        rc = WinChangeSwitchEntry(hSwitch, &switchData);
#ifdef VERBOSE
        if (rc != 0) {
            printf("WinChangeSwitchEntry %x ERROR %x\n", hSwitch,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("WinChangeSwitchEntry %x OK\n", hSwitch);
        }
#endif
    }

end:
    wmPtr->flags &= ~WM_SYNC_PENDING;
#ifdef VERBOSE
    WinQueryWindowPos(wmPtr->wrapper, &pos);
    printf("TkpWmSetState %x, newpos (%d,%d;%dx%d)\n", wmPtr->wrapper,
           pos.x, pos.y, pos.cx, pos.cy);
#endif
}

/*
 *--------------------------------------------------------------
 *
 * TkWmDeadWindow --
 *
 *	This procedure is invoked when a top-level window is
 *	about to be deleted.  It cleans up the wm-related data
 *	structures for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The WmInfo structure for winPtr gets freed up.
 *
 *--------------------------------------------------------------
 */

void
TkWmDeadWindow(winPtr)
    TkWindow *winPtr;		/* Top-level window that's being deleted. */
{
    register WmInfo *wmPtr = winPtr->wmInfoPtr;
    WmInfo *wmPtr2;

    if (wmPtr == NULL) {
#ifdef VERBOSE
        printf("TkWmDeadWindow, null wmPtr\n");
#endif
        return;
    }
#ifdef VERBOSE
    printf("TkWmDeadWindow\n");
#endif

    /*
     * Clean up event related window info.
     */

    if (winPtr->dispPtr->firstWmPtr == wmPtr) {
        winPtr->dispPtr->firstWmPtr = wmPtr->nextPtr;
    } else {
        register WmInfo *prevPtr;

        for (prevPtr = winPtr->dispPtr->firstWmPtr; ;
             prevPtr = prevPtr->nextPtr) {
            if (prevPtr == NULL) {
                panic("couldn't unlink window in TkWmDeadWindow");
            }
            if (prevPtr->nextPtr == wmPtr) {
                prevPtr->nextPtr = wmPtr->nextPtr;
                break;
            }
        }
    }

    /*
     * Reset all transient windows whose master is the dead window.
     */
    for (wmPtr2 = winPtr->dispPtr->firstWmPtr; wmPtr2 != NULL;
         wmPtr2 = wmPtr2->nextPtr) {
        if (wmPtr2->masterPtr == winPtr) {
            wmPtr2->masterPtr = NULL;
            if ((wmPtr2->wrapper != None)
                    && !(wmPtr2->flags & (WM_NEVER_MAPPED))) {
                UpdateWrapper(wmPtr2->winPtr);
            }
        }
    }

    if (wmPtr->hints.flags & IconPixmapHint) {
        Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_pixmap);
    }
    if (wmPtr->hints.flags & IconMaskHint) {
        Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_mask);
    }
    if (wmPtr->leaderName != NULL) {
        ckfree(wmPtr->leaderName);
    }
    if (wmPtr->icon != NULL) {
        wmPtr2 = ((TkWindow *) wmPtr->icon)->wmInfoPtr;
        wmPtr2->iconFor = NULL;
    }
    if (wmPtr->iconFor != NULL) {
        wmPtr2 = ((TkWindow *) wmPtr->iconFor)->wmInfoPtr;
        wmPtr2->icon = NULL;
        wmPtr2->hints.flags &= ~IconWindowHint;
    }
    while (wmPtr->protPtr != NULL) {
        ProtocolHandler *protPtr;

        protPtr = wmPtr->protPtr;
        wmPtr->protPtr = protPtr->nextPtr;
        Tcl_EventuallyFree((ClientData) protPtr, TCL_DYNAMIC);
    }
    if (wmPtr->cmdArgv != NULL) {
        ckfree((char *) wmPtr->cmdArgv);
    }
    if (wmPtr->clientMachine != NULL) {
        ckfree((char *) wmPtr->clientMachine);
    }
    if (wmPtr->flags & WM_UPDATE_PENDING) {
        Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData) winPtr);
    }
    if (wmPtr->masterPtr != NULL) {
        wmPtr2 = wmPtr->masterPtr->wmInfoPtr;
        /*
         * If we had a master, tell them that we aren't tied
         * to them anymore
         */
        if (wmPtr2 != NULL) {
            wmPtr2->numTransients--;
        }
        Tk_DeleteEventHandler((Tk_Window) wmPtr->masterPtr,
                VisibilityChangeMask,
                WmWaitVisibilityProc, (ClientData) winPtr);
        wmPtr->masterPtr = NULL;
    }

    /*
     * Destroy the decorative frame window.
     */

    if (!(winPtr->flags & TK_EMBEDDED)) {
        if (wmPtr->wrapper != NULLHANDLE) {
            WinDestroyWindow(wmPtr->wrapper);
        } else {
            WinDestroyWindow(Tk_GetHWND(winPtr->window));
        }
    }
#if 0
    if (wmPtr->iconPtr != NULL) {
        /*
         * This may delete the icon resource data.  I believe we
         * should do this after destroying the decorative frame,
         * because the decorative frame is using this icon.
         */
        DecrIconRefCount(wmPtr->iconPtr);
    }
#endif

    ckfree((char *) wmPtr);
#ifdef VERBOSE
    printf("TkWmDeadWindow freed wmPtr %x\n", wmPtr);
#endif
    winPtr->wmInfoPtr = NULL;
}

/*
 *--------------------------------------------------------------
 *
 * TkWmSetClass --
 *
 *	This procedure is invoked whenever a top-level window's
 *	class is changed.  If the window has been mapped then this
 *	procedure updates the window manager property for the
 *	class.  If the window hasn't been mapped, the update is
 *	deferred until just before the first mapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A window property may get updated.
 *
 *--------------------------------------------------------------
 */

void
TkWmSetClass(winPtr)
    TkWindow *winPtr;		/* Newly-created top-level window. */
{
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_WmCmd --
 *
 *	This procedure is invoked to process the "wm" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

        /* ARGSUSED */
int
Tk_WmCmd(clientData, interp, argc, argv)
    ClientData clientData;	/* Main window associated with
        			 * interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int argc;			/* Number of arguments. */
    char **argv;		/* Argument strings. */
{
    Tk_Window tkwin = (Tk_Window) clientData;
    TkWindow *winPtr = NULL;
    register WmInfo *wmPtr;
    int c;
    size_t length;
    TkDisplay *dispPtr = ((TkWindow *) tkwin)->dispPtr;

#ifdef VERBOSE
    printf("Tk_WmCmd\n");
#endif

    if (argc < 2) {
        wrongNumArgs:
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " option window ?arg ...?\"", (char *) NULL);
        return TCL_ERROR;
    }
    c = argv[1][0];
    length = strlen(argv[1]);
    if ((c == 't') && (strncmp(argv[1], "tracing", length) == 0)
            && (length >= 3)) {
        if ((argc != 2) && (argc != 3)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
                    argv[0], " tracing ?boolean?\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 2) {
            Tcl_SetResult(interp, ((dispPtr->wmTracing) ? "on" : "off"),
                    TCL_STATIC);
            return TCL_OK;
        }
        return Tcl_GetBoolean(interp, argv[2], &dispPtr->wmTracing);
    }

    if (argc < 3) {
        goto wrongNumArgs;
    }
#ifdef VERBOSE
    printf("   %s %s %s\n", argv[0], argv[1], argv[2]);
    fflush(stdout);
#endif
    winPtr = (TkWindow *) Tk_NameToWindow(interp, argv[2], tkwin);
    if (winPtr == NULL) {
        return TCL_ERROR;
    }
    if (!(winPtr->flags & TK_TOP_LEVEL)) {
        Tcl_AppendResult(interp, "window \"", winPtr->pathName,
                "\" isn't a top-level window", (char *) NULL);
        return TCL_ERROR;
    }
    wmPtr = winPtr->wmInfoPtr;
    if ((c == 'a') && (strncmp(argv[1], "aspect", length) == 0)) {
        int numer1, denom1, numer2, denom2;

        if ((argc != 3) && (argc != 7)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
                    argv[0], " aspect window ?minNumer minDenom ",
                    "maxNumer maxDenom?\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->sizeHintsFlags & PAspect) {
                char buf[TCL_INTEGER_SPACE * 4];

                sprintf(buf, "%d %d %d %d", wmPtr->minAspect.x,
                        wmPtr->minAspect.y, wmPtr->maxAspect.x,
                        wmPtr->maxAspect.y);
                Tcl_SetResult(interp, buf, TCL_VOLATILE);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            wmPtr->sizeHintsFlags &= ~PAspect;
        } else {
            if ((Tcl_GetInt(interp, argv[3], &numer1) != TCL_OK)
                    || (Tcl_GetInt(interp, argv[4], &denom1) != TCL_OK)
                    || (Tcl_GetInt(interp, argv[5], &numer2) != TCL_OK)
                    || (Tcl_GetInt(interp, argv[6], &denom2) != TCL_OK)) {
                return TCL_ERROR;
            }
            if ((numer1 <= 0) || (denom1 <= 0) || (numer2 <= 0) ||
                    (denom2 <= 0)) {
                Tcl_SetResult(interp, "aspect number can't be <= 0",
                        TCL_STATIC);
                return TCL_ERROR;
            }
            wmPtr->minAspect.x = numer1;
            wmPtr->minAspect.y = denom1;
            wmPtr->maxAspect.x = numer2;
            wmPtr->maxAspect.y = denom2;
            wmPtr->sizeHintsFlags |= PAspect;
        }
        goto updateGeom;
    } else if ((c == 'c') && (strncmp(argv[1], "client", length) == 0)
            && (length >= 2)) {
        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
                    argv[0], " client window ?name?\"",
                    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->clientMachine != NULL) {
                Tcl_SetResult(interp, wmPtr->clientMachine, TCL_STATIC);
            }
            return TCL_OK;
        }
        if (argv[3][0] == 0) {
            if (wmPtr->clientMachine != NULL) {
                ckfree((char *) wmPtr->clientMachine);
                wmPtr->clientMachine = NULL;
                if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
                    XDeleteProperty(winPtr->display, winPtr->window,
                            Tk_InternAtom((Tk_Window) winPtr,
                            "WM_CLIENT_MACHINE"));
                }
            }
            return TCL_OK;
        }
        if (wmPtr->clientMachine != NULL) {
            ckfree((char *) wmPtr->clientMachine);
        }
        wmPtr->clientMachine = (char *)
                ckalloc((unsigned) (strlen(argv[3]) + 1));
        strcpy(wmPtr->clientMachine, argv[3]);
        if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
            XTextProperty textProp;
            if (XStringListToTextProperty(&wmPtr->clientMachine, 1, &textProp)
                    != 0) {
                XSetWMClientMachine(winPtr->display, winPtr->window,
                	&textProp);
        	XFree((char *) textProp.value);
            }
        }
    } else if ((c == 'c') && (strncmp(argv[1], "colormapwindows", length) == 0)
            && (length >= 3)) {
        TkWindow **cmapList;
        TkWindow *winPtr2;
        int i, windowArgc, gotToplevel = 0;
        char **windowArgv;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " colormapwindows window ?windowList?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            Tk_MakeWindowExist((Tk_Window) winPtr);
            for (i = 0; i < wmPtr->cmapCount; i++) {
        	if ((i == (wmPtr->cmapCount-1))
        		&& (wmPtr->flags & WM_ADDED_TOPLEVEL_COLORMAP)) {
        	    break;
        	}
        	Tcl_AppendElement(interp, wmPtr->cmapList[i]->pathName);
            }
            return TCL_OK;
        }
        if (Tcl_SplitList(interp, argv[3], &windowArgc, &windowArgv)
        	!= TCL_OK) {
            return TCL_ERROR;
        }
        cmapList = (TkWindow **) ckalloc((unsigned)
        	((windowArgc+1)*sizeof(TkWindow*)));
        for (i = 0; i < windowArgc; i++) {
            winPtr2 = (TkWindow *) Tk_NameToWindow(interp, windowArgv[i],
        	    tkwin);
            if (winPtr2 == NULL) {
        	ckfree((char *) cmapList);
        	ckfree((char *) windowArgv);
        	return TCL_ERROR;
            }
            if (winPtr2 == winPtr) {
        	gotToplevel = 1;
            }
            if (winPtr2->window == None) {
        	Tk_MakeWindowExist((Tk_Window) winPtr2);
            }
            cmapList[i] = winPtr2;
#ifdef VERBOSE
            printf("    cmaplist[%d] = %s\n", i, winPtr2->pathName);
            fflush(stdout);
#endif
        }
        if (!gotToplevel) {
            wmPtr->flags |= WM_ADDED_TOPLEVEL_COLORMAP;
            cmapList[windowArgc] = winPtr;
#ifdef VERBOSE
            printf("    cmapList[%d] = %s\n", windowArgc, winPtr->pathName);
            fflush(stdout);
#endif
            windowArgc++;
        } else {
            wmPtr->flags &= ~WM_ADDED_TOPLEVEL_COLORMAP;
        }
        wmPtr->flags |= WM_COLORMAPS_EXPLICIT;
        if (wmPtr->cmapList != NULL) {
            ckfree((char *)wmPtr->cmapList);
        }
        wmPtr->cmapList = cmapList;
        wmPtr->cmapCount = windowArgc;
        ckfree((char *) windowArgv);

        /*
         * Now we need to force the updated colormaps to be installed.
         */

        if (wmPtr == winPtr->dispPtr->foregroundWmPtr) {
            /* WM_QUERYNEWPALETTE -> WM_REALIZEPALETTE + focus notification */
            InstallColormaps(wmPtr->wrapper, WM_REALIZEPALETTE, 1);
        } else {
            /* WM_PALETTECHANGED -> WM_REALIZEPALETTE + focus notification */
            InstallColormaps(wmPtr->wrapper, WM_REALIZEPALETTE, 0);
        }
        return TCL_OK;
    } else if ((c == 'c') && (strncmp(argv[1], "command", length) == 0)
            && (length >= 3)) {
        int cmdArgc;
        char **cmdArgv;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " command window ?value?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->cmdArgv != NULL) {
                Tcl_SetResult(interp,
                        Tcl_Merge(wmPtr->cmdArgc, wmPtr->cmdArgv),
                        TCL_DYNAMIC);
            }
            return TCL_OK;
        }
        if (argv[3][0] == 0) {
            if (wmPtr->cmdArgv != NULL) {
        	ckfree((char *) wmPtr->cmdArgv);
        	wmPtr->cmdArgv = NULL;
        	if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        	    XDeleteProperty(winPtr->display, winPtr->window,
        		    Tk_InternAtom((Tk_Window) winPtr, "WM_COMMAND"));
        	}
            }
            return TCL_OK;
        }
        if (Tcl_SplitList(interp, argv[3], &cmdArgc, &cmdArgv) != TCL_OK) {
            return TCL_ERROR;
        }
        if (wmPtr->cmdArgv != NULL) {
            ckfree((char *) wmPtr->cmdArgv);
        }
        wmPtr->cmdArgc = cmdArgc;
        wmPtr->cmdArgv = cmdArgv;
        if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
            XSetCommand(winPtr->display, winPtr->window, cmdArgv, cmdArgc);
        }
    } else if ((c == 'd') && (strncmp(argv[1], "deiconify", length) == 0)) {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " deiconify window\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (wmPtr->iconFor != NULL) {
            Tcl_AppendResult(interp, "can't deiconify ", argv[2],
        	    ": it is an icon for ", winPtr->pathName, (char *) NULL);
            return TCL_ERROR;
        }
        if (winPtr->flags & TK_EMBEDDED) {
            Tcl_AppendResult(interp, "can't deiconify ", winPtr->pathName,
                    ": it is an embedded window", (char *) NULL);
            return TCL_ERROR;
        }
        /*
         * If WM_UPDATE_PENDING is true, a pending UpdateGeometryInfo may
         * need to be called first to update a withdrew toplevel's geometry
         * before it is deiconified by TkpWmSetState.
         * Don't bother if we've never been mapped.
         */
        if ((wmPtr->flags & WM_UPDATE_PENDING)) {
            Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData) winPtr);
            UpdateGeometryInfo((ClientData) winPtr);
        }

        /*
         * If we were in the ZoomState (maximized), 'wm deiconify'
         * should not cause the window to shrink
         */
        if (wmPtr->hints.initial_state == ZoomState) {
            TkpWmSetState(winPtr, ZoomState);
        } else {
            TkpWmSetState(winPtr, NormalState);
        }

        /*
         * Follow Windows-like style here, raising the window to the top.
         */
        TkWmRestackToplevel(winPtr, Above, NULL);
        if (!(Tk_Attributes((Tk_Window) winPtr)->override_redirect)) {
            TkSetFocusWin(winPtr, 1);
        }
    } else if ((c == 'f') && (strncmp(argv[1], "focusmodel", length) == 0)
            && (length >= 2)) {
        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " focusmodel window ?active|passive?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            Tcl_SetResult(interp, (wmPtr->hints.input ? "passive" : "active"),
                    TCL_STATIC);
            return TCL_OK;
        }
        c = argv[3][0];
        length = strlen(argv[3]);
        if ((c == 'a') && (strncmp(argv[3], "active", length) == 0)) {
            wmPtr->hints.input = False;
        } else if ((c == 'p') && (strncmp(argv[3], "passive", length) == 0)) {
            wmPtr->hints.input = True;
        } else {
            Tcl_AppendResult(interp, "bad argument \"", argv[3],
        	    "\": must be active or passive", (char *) NULL);
            return TCL_ERROR;
        }
    } else if ((c == 'f') && (strncmp(argv[1], "frame", length) == 0)
            && (length >= 2)) {
        HWND hwnd;
        char buf[TCL_INTEGER_SPACE];

        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " frame window\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (Tk_WindowId((Tk_Window) winPtr) == None) {
            Tk_MakeWindowExist((Tk_Window) winPtr);
        }
        hwnd = wmPtr->wrapper;
        if (hwnd == NULLHANDLE) {
            hwnd = Tk_GetHWND(Tk_WindowId((Tk_Window) winPtr));
        }
        sprintf(buf, "0x%x", (unsigned int) hwnd);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
    } else if ((c == 'g') && (strncmp(argv[1], "geometry", length) == 0)
            && (length >= 2)) {
        char xSign, ySign;
        int width, height;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " geometry window ?newGeometry?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            char buf[16 + TCL_INTEGER_SPACE * 4];

            xSign = (wmPtr->flags & WM_NEGATIVE_X) ? '-' : '+';
            ySign = (wmPtr->flags & WM_NEGATIVE_Y) ? '-' : '+';
            if (wmPtr->gridWin != NULL) {
        	width = wmPtr->reqGridWidth + (winPtr->changes.width
        		- winPtr->reqWidth)/wmPtr->widthInc;
        	height = wmPtr->reqGridHeight + (winPtr->changes.height
        		- winPtr->reqHeight)/wmPtr->heightInc;
            } else {
        	width = winPtr->changes.width;
        	height = winPtr->changes.height;
            }
            sprintf(buf, "%dx%d%c%d%c%d", width, height, xSign, wmPtr->x,
                    ySign, wmPtr->y);
            Tcl_SetResult(interp, buf, TCL_VOLATILE);
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            wmPtr->width = -1;
            wmPtr->height = -1;
            goto updateGeom;
        }
        return ParseGeometry(interp, argv[3], winPtr);
    } else if ((c == 'g') && (strncmp(argv[1], "grid", length) == 0)
            && (length >= 3)) {
        int reqWidth, reqHeight, widthInc, heightInc;

        if ((argc != 3) && (argc != 7)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " grid window ?baseWidth baseHeight ",
        	    "widthInc heightInc?\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->sizeHintsFlags & PBaseSize) {
                char buf[TCL_INTEGER_SPACE * 4];

                sprintf(buf, "%d %d %d %d", wmPtr->reqGridWidth,
                        wmPtr->reqGridHeight, wmPtr->widthInc,
                        wmPtr->heightInc);
                Tcl_SetResult(interp, buf, TCL_VOLATILE);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            /*
             * Turn off gridding and reset the width and height
             * to make sense as ungridded numbers.
             */

            wmPtr->sizeHintsFlags &= ~(PBaseSize|PResizeInc);
            if (wmPtr->width != -1) {
        	wmPtr->width = winPtr->reqWidth + (wmPtr->width
        		- wmPtr->reqGridWidth)*wmPtr->widthInc;
        	wmPtr->height = winPtr->reqHeight + (wmPtr->height
        		- wmPtr->reqGridHeight)*wmPtr->heightInc;
            }
            wmPtr->widthInc = 1;
            wmPtr->heightInc = 1;
        } else {
            if ((Tcl_GetInt(interp, argv[3], &reqWidth) != TCL_OK)
        	    || (Tcl_GetInt(interp, argv[4], &reqHeight) != TCL_OK)
        	    || (Tcl_GetInt(interp, argv[5], &widthInc) != TCL_OK)
        	    || (Tcl_GetInt(interp, argv[6], &heightInc) != TCL_OK)) {
        	return TCL_ERROR;
            }
            if (reqWidth < 0) {
                Tcl_SetResult(interp, "baseWidth can't be < 0", TCL_STATIC);
        	return TCL_ERROR;
            }
            if (reqHeight < 0) {
                Tcl_SetResult(interp, "baseHeight can't be < 0", TCL_STATIC);
        	return TCL_ERROR;
            }
            if (widthInc < 0) {
                Tcl_SetResult(interp, "widthInc can't be < 0", TCL_STATIC);
        	return TCL_ERROR;
            }
            if (heightInc < 0) {
                Tcl_SetResult(interp, "heightInc can't be < 0", TCL_STATIC);
        	return TCL_ERROR;
            }
            Tk_SetGrid((Tk_Window) winPtr, reqWidth, reqHeight, widthInc,
        	    heightInc);
        }
        goto updateGeom;
    } else if ((c == 'g') && (strncmp(argv[1], "group", length) == 0)
            && (length >= 3)) {
        Tk_Window tkwin2;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " group window ?pathName?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->hints.flags & WindowGroupHint) {
                Tcl_SetResult(interp, wmPtr->leaderName, TCL_STATIC);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            wmPtr->hints.flags &= ~WindowGroupHint;
            if (wmPtr->leaderName != NULL) {
                ckfree(wmPtr->leaderName);
            }
            wmPtr->leaderName = NULL;
        } else {
            tkwin2 = Tk_NameToWindow(interp, argv[3], tkwin);
            if (tkwin2 == NULL) {
        	return TCL_ERROR;
            }
            Tk_MakeWindowExist(tkwin2);
            wmPtr->hints.window_group = Tk_WindowId(tkwin2);
            wmPtr->hints.flags |= WindowGroupHint;
            wmPtr->leaderName = ckalloc((unsigned) (strlen(argv[3])+1));
            strcpy(wmPtr->leaderName, argv[3]);
        }
    } else if ((c == 'i') && (strncmp(argv[1], "iconbitmap", length) == 0)
            && (length >= 5)) {
        /* If true, then set for all windows. */
        int isDefault = 0;

        if ((argc < 3) || (argc > 5)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
                    argv[0], " iconbitmap window ?-default? ?image?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        } else if (argc == 5) {
            /* If we have 5 arguments, we must have a '-default' flag */
            if (strcmp(argv[3],"-default")) {
                Tcl_AppendResult(interp, "illegal option \"",
                        argv[3], " must be \"-default\"",
                        (char *) NULL);
                return TCL_ERROR;
            }
            isDefault = 1;
        } else if (argc == 3) {
            /* No arguments were given */
            if (wmPtr->hints.flags & IconPixmapHint) {
                Tcl_SetResult(interp,
                        Tk_NameOfBitmap(winPtr->display,
                                wmPtr->hints.icon_pixmap), TCL_STATIC);
            }
            return TCL_OK;
        }
        if (*argv[argc-1] == '\0') {
            if (wmPtr->hints.icon_pixmap != None) {
        	Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_pixmap);
            }
            wmPtr->hints.flags &= ~IconPixmapHint;
        } else {
            /*
             * In the future this block of code will use Tk's 'image'
             * functionality to allow all supported image formats.
             * However, this will require a change to the way icons are
             * handled.  We will need to add icon<->image conversions
             * routines.
             *
             * Until that happens we simply try to find an icon in the
             * given argument, and if that fails, we use the older
             * bitmap code.  We do things this way round (icon then
             * bitmap), because the bitmap code actually seems to have
             * no visible effect, so we want to give the icon code the
             * first try at doing something.
             */

            /*
             * Either return NULL, or return a valid titlebarIcon with its
             * ref count already incremented.
             */
#if 0
            OS2IconPtr titlebarIcon = ReadIconFromFile(interp, argv[argc-1]);
            if (titlebarIcon != NULL) {
                /*
                 * Try to set the icon for the window.  If it is a '-default'
                 * icon, we must pass in NULL
                 */
                if (WinSetIcon(interp, titlebarIcon,
                               (isDefault ? NULL : (Tk_Window) winPtr)) != TCL_OK) {
                    /* We didn't use the titlebarIcon after all */
                    DecrIconRefCount(titlebarIcon);
                    titlebarIcon = NULL;
                }
            }
            if (titlebarIcon == NULL) {
                /*
                 * We didn't manage to handle the argument as a valid
                 * icon.  Try as a bitmap.  First we must clear the
                 * error message which was placed in the interpreter
                 */
                Pixmap pixmap;
                Tcl_ResetResult(interp);
                pixmap = Tk_GetBitmap(interp, (Tk_Window) winPtr,
        	        Tk_GetUid(argv[3]));
                if (pixmap == None) {
        	    return TCL_ERROR;
                }
                wmPtr->hints.icon_pixmap = pixmap;
                wmPtr->hints.flags |= IconPixmapHint;
            }
#endif
        }
    } else if ((c == 'i') && (strncmp(argv[1], "iconify", length) == 0)
            && (length >= 5)) {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " iconify window\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (Tk_Attributes((Tk_Window) winPtr)->override_redirect) {
            Tcl_AppendResult(interp, "can't iconify \"", winPtr->pathName,
        	    "\": override-redirect flag is set", (char *) NULL);
            return TCL_ERROR;
        }
        if (wmPtr->masterPtr != NULL) {
            Tcl_AppendResult(interp, "can't iconify \"", winPtr->pathName,
        	    "\": it is a transient", (char *) NULL);
            return TCL_ERROR;
        }
        if (wmPtr->iconFor != NULL) {
            Tcl_AppendResult(interp, "can't iconify ", argv[2],
        	    ": it is an icon for ", winPtr->pathName, (char *) NULL);
            return TCL_ERROR;
        }
        if (winPtr->flags & TK_EMBEDDED) {
            Tcl_AppendResult(interp, "can't iconify ", winPtr->pathName,
                    ": it is an embedded window", (char *) NULL);
            return TCL_ERROR;
        }
        TkpWmSetState(winPtr, IconicState);
    } else if ((c == 'i') && (strncmp(argv[1], "iconmask", length) == 0)
            && (length >= 5)) {
        Pixmap pixmap;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " iconmask window ?bitmap?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->hints.flags & IconMaskHint) {
                Tcl_SetResult(interp,
                       Tk_NameOfBitmap(winPtr->display, wmPtr->hints.icon_mask),
                       TCL_STATIC);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            if (wmPtr->hints.icon_mask != None) {
        	Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_mask);
            }
            wmPtr->hints.flags &= ~IconMaskHint;
        } else {
            pixmap = Tk_GetBitmap(interp, tkwin, Tk_GetUid(argv[3]));
            if (pixmap == None) {
        	return TCL_ERROR;
            }
            wmPtr->hints.icon_mask = pixmap;
            wmPtr->hints.flags |= IconMaskHint;
        }
    } else if ((c == 'i') && (strncmp(argv[1], "iconname", length) == 0)
            && (length >= 5)) {
        if (argc > 4) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " iconname window ?newName?\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            Tcl_SetResult(interp,
                    ((wmPtr->iconName != NULL) ? wmPtr->iconName : ""),
                    TCL_STATIC);
            return TCL_OK;
        } else {
            wmPtr->iconName = Tk_GetUid(argv[3]);
            if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        	XSetIconName(winPtr->display, winPtr->window, wmPtr->iconName);
            }
        }
    } else if ((c == 'i') && (strncmp(argv[1], "iconposition", length) == 0)
            && (length >= 5)) {
        int x, y;

        if ((argc != 3) && (argc != 5)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " iconposition window ?x y?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->hints.flags & IconPositionHint) {
                char buf[TCL_INTEGER_SPACE * 2];

                sprintf(buf, "%d %d", wmPtr->hints.icon_x,
                        wmPtr->hints.icon_y);
                Tcl_SetResult(interp, buf, TCL_VOLATILE);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            wmPtr->hints.flags &= ~IconPositionHint;
        } else {
            if ((Tcl_GetInt(interp, argv[3], &x) != TCL_OK)
        	    || (Tcl_GetInt(interp, argv[4], &y) != TCL_OK)){
        	return TCL_ERROR;
            }
            wmPtr->hints.icon_x = x;
            wmPtr->hints.icon_y = y;
            wmPtr->hints.flags |= IconPositionHint;
        }
    } else if ((c == 'i') && (strncmp(argv[1], "iconwindow", length) == 0)
            && (length >= 5)) {
        Tk_Window tkwin2;
        WmInfo *wmPtr2;
        XSetWindowAttributes atts;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " iconwindow window ?pathName?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->icon != NULL) {
                Tcl_SetResult(interp, Tk_PathName(wmPtr->icon), TCL_STATIC);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            wmPtr->hints.flags &= ~IconWindowHint;
            if (wmPtr->icon != NULL) {
                /*
                 * Let the window use button events again, then remove
                 * it as icon window.
                 */

                atts.event_mask = Tk_Attributes(wmPtr->icon)->event_mask
                        | ButtonPressMask;
                Tk_ChangeWindowAttributes(wmPtr->icon, CWEventMask, &atts);
        	wmPtr2 = ((TkWindow *) wmPtr->icon)->wmInfoPtr;
        	wmPtr2->iconFor = NULL;
        	wmPtr2->hints.initial_state = WithdrawnState;
            }
            wmPtr->icon = NULL;
        } else {
            tkwin2 = Tk_NameToWindow(interp, argv[3], tkwin);
            if (tkwin2 == NULL) {
        	return TCL_ERROR;
            }
            if (!Tk_IsTopLevel(tkwin2)) {
        	Tcl_AppendResult(interp, "can't use ", argv[3],
        		" as icon window: not at top level", (char *) NULL);
        	return TCL_ERROR;
            }
            wmPtr2 = ((TkWindow *) tkwin2)->wmInfoPtr;
            if (wmPtr2->iconFor != NULL) {
        	Tcl_AppendResult(interp, argv[3], " is already an icon for ",
        		Tk_PathName(wmPtr2->iconFor), (char *) NULL);
        	return TCL_ERROR;
            }
            if (wmPtr->icon != NULL) {
        	WmInfo *wmPtr3 = ((TkWindow *) wmPtr->icon)->wmInfoPtr;
        	wmPtr3->iconFor = NULL;

                /*
                 * Let the window use button events again.
                 */

                atts.event_mask = Tk_Attributes(wmPtr->icon)->event_mask
                        | ButtonPressMask;
                Tk_ChangeWindowAttributes(wmPtr->icon, CWEventMask, &atts);
            }

            /*
             * Disable button events in the icon window:  some window
             * managers (like olvwm) want to get the events themselves,
             * but X only allows one application at a time to receive
             * button events for a window.
             */

            atts.event_mask = Tk_Attributes(tkwin2)->event_mask
                    & ~ButtonPressMask;
            Tk_ChangeWindowAttributes(tkwin2, CWEventMask, &atts);
            Tk_MakeWindowExist(tkwin2);
            wmPtr->hints.icon_window = Tk_WindowId(tkwin2);
            wmPtr->hints.flags |= IconWindowHint;
            wmPtr->icon = tkwin2;
            wmPtr2->iconFor = (Tk_Window) winPtr;
            if (!(wmPtr2->flags & WM_NEVER_MAPPED)) {
#ifdef VERBOSE
                printf("calling XWithdrawWindow(%x, %x, %d)\n",
                       Tk_Display(tkwin2), Tk_WindowId(tkwin2),
                       Tk_ScreenNumber(tkwin2));
#endif
        	if (XWithdrawWindow(Tk_Display(tkwin2), Tk_WindowId(tkwin2),
        		Tk_ScreenNumber(tkwin2)) == 0) {
                    Tcl_SetResult(interp,
                            "couldn't send withdraw message to window manager",
                            TCL_STATIC);
        	    return TCL_ERROR;
        	}
            }
        }
    } else if ((c == 'm') && (strncmp(argv[1], "maxsize", length) == 0)
            && (length >= 2)) {
        int width, height;
        if ((argc != 3) && (argc != 5)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " maxsize window ?width height?\"",
                    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            char buf[TCL_INTEGER_SPACE * 2];

            GetMaxSize(wmPtr, &width, &height);
            sprintf(buf, "%d %d", width, height);
            Tcl_SetResult(interp, buf, TCL_VOLATILE);
            return TCL_OK;
        }
        if ((Tcl_GetInt(interp, argv[3], &width) != TCL_OK)
        	|| (Tcl_GetInt(interp, argv[4], &height) != TCL_OK)) {
            return TCL_ERROR;
        }
        wmPtr->maxWidth = width;
        wmPtr->maxHeight = height;
        goto updateGeom;
    } else if ((c == 'm') && (strncmp(argv[1], "minsize", length) == 0)
            && (length >= 2)) {
        int width, height;
        if ((argc != 3) && (argc != 5)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " minsize window ?width height?\"",
                    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            char buf[TCL_INTEGER_SPACE * 2];

            GetMinSize(wmPtr, &width, &height);
            sprintf(buf, "%d %d", width, height);
            Tcl_SetResult(interp, buf, TCL_VOLATILE);
#ifdef VERBOSE
            printf("wm minsize returns %d %d\n", width, height);
#endif
            return TCL_OK;
        }
        if ((Tcl_GetInt(interp, argv[3], &width) != TCL_OK)
        	|| (Tcl_GetInt(interp, argv[4], &height) != TCL_OK)) {
            return TCL_ERROR;
        }
        wmPtr->minWidth = width;
        wmPtr->minHeight = height;
#ifdef VERBOSE
        printf("wm minsize %d %d set\n", width, height);
#endif
        goto updateGeom;
    } else if ((c == 'o')
            && (strncmp(argv[1], "overrideredirect", length) == 0)) {
        int boolean, curValue;
        XSetWindowAttributes atts;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " overrideredirect window ?boolean?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        curValue = Tk_Attributes((Tk_Window) winPtr)->override_redirect;
        if (argc == 3) {
            Tcl_SetBooleanObj(Tcl_GetObjResult(interp), curValue);
            return TCL_OK;
        }
        if (Tcl_GetBoolean(interp, argv[3], &boolean) != TCL_OK) {
            return TCL_ERROR;
        }
        if (curValue != boolean) {
            /*
             * Only do this if we are really changing value, because it
             * causes some funky stuff to occur
             */
            atts.override_redirect = (boolean) ? True : False;
            Tk_ChangeWindowAttributes((Tk_Window) winPtr, CWOverrideRedirect,
                    &atts);
            if (!(wmPtr->flags & (WM_NEVER_MAPPED)
                    && !(winPtr->flags & TK_EMBEDDED))) {
                UpdateWrapper(winPtr);
            }
        }
    } else if ((c == 'p') && (strncmp(argv[1], "positionfrom", length) == 0)
            && (length >= 2)) {
        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " positionfrom window ?user/program?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->sizeHintsFlags & USPosition) {
                Tcl_SetResult(interp, "user", TCL_STATIC);
            } else if (wmPtr->sizeHintsFlags & PPosition) {
                Tcl_SetResult(interp, "program", TCL_STATIC);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            wmPtr->sizeHintsFlags &= ~(USPosition|PPosition);
        } else {
            c = argv[3][0];
            length = strlen(argv[3]);
            if ((c == 'u') && (strncmp(argv[3], "user", length) == 0)) {
        	wmPtr->sizeHintsFlags &= ~PPosition;
        	wmPtr->sizeHintsFlags |= USPosition;
            } else if ((c == 'p')
                    && (strncmp(argv[3], "program", length) == 0)) {
        	wmPtr->sizeHintsFlags &= ~USPosition;
        	wmPtr->sizeHintsFlags |= PPosition;
            } else {
        	Tcl_AppendResult(interp, "bad argument \"", argv[3],
        		"\": must be program or user", (char *) NULL);
        	return TCL_ERROR;
            }
        }
        goto updateGeom;
    } else if ((c == 'p') && (strncmp(argv[1], "protocol", length) == 0)
            && (length >= 2)) {
        register ProtocolHandler *protPtr, *prevPtr;
        Atom protocol;
        int cmdLength;

        if ((argc < 3) || (argc > 5)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " protocol window ?name? ?command?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            /*
             * Return a list of all defined protocols for the window.
             */
            for (protPtr = wmPtr->protPtr; protPtr != NULL;
        	    protPtr = protPtr->nextPtr) {
        	Tcl_AppendElement(interp,
        		Tk_GetAtomName((Tk_Window) winPtr, protPtr->protocol));
            }
            return TCL_OK;
        }
        protocol = Tk_InternAtom((Tk_Window) winPtr, argv[3]);
        if (argc == 4) {
            /*
             * Return the command to handle a given protocol.
             */
            for (protPtr = wmPtr->protPtr; protPtr != NULL;
        	    protPtr = protPtr->nextPtr) {
        	if (protPtr->protocol == protocol) {
                    Tcl_SetResult(interp, protPtr->command, TCL_STATIC);
        	    return TCL_OK;
        	}
            }
            return TCL_OK;
        }

        /*
         * Delete any current protocol handler, then create a new
         * one with the specified command, unless the command is
         * empty.
         */

        for (protPtr = wmPtr->protPtr, prevPtr = NULL; protPtr != NULL;
        	prevPtr = protPtr, protPtr = protPtr->nextPtr) {
            if (protPtr->protocol == protocol) {
        	if (prevPtr == NULL) {
        	    wmPtr->protPtr = protPtr->nextPtr;
        	} else {
        	    prevPtr->nextPtr = protPtr->nextPtr;
        	}
        	Tcl_EventuallyFree((ClientData) protPtr, TCL_DYNAMIC);
        	break;
            }
        }
        cmdLength = strlen(argv[4]);
        if (cmdLength > 0) {
            protPtr = (ProtocolHandler *) ckalloc(HANDLER_SIZE(cmdLength));
            protPtr->protocol = protocol;
            protPtr->nextPtr = wmPtr->protPtr;
            wmPtr->protPtr = protPtr;
            protPtr->interp = interp;
            strcpy(protPtr->command, argv[4]);
        }
    } else if ((c == 'r') && (strncmp(argv[1], "resizable", length) == 0)) {
        int width, height;

        if ((argc != 3) && (argc != 5)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
                    argv[0], " resizable window ?width height?\"",
                    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            char buf[TCL_INTEGER_SPACE * 2];

            sprintf(buf, "%d %d",
                    (wmPtr->flags  & WM_WIDTH_NOT_RESIZABLE) ? 0 : 1,
                    (wmPtr->flags  & WM_HEIGHT_NOT_RESIZABLE) ? 0 : 1);
            Tcl_SetResult(interp, buf, TCL_VOLATILE);
            return TCL_OK;
        }
        if ((Tcl_GetBoolean(interp, argv[3], &width) != TCL_OK)
                || (Tcl_GetBoolean(interp, argv[4], &height) != TCL_OK)) {
            return TCL_ERROR;
        }
        if (width) {
            wmPtr->flags &= ~WM_WIDTH_NOT_RESIZABLE;
        } else {
            wmPtr->flags |= WM_WIDTH_NOT_RESIZABLE;
        }
        if (height) {
            wmPtr->flags &= ~WM_HEIGHT_NOT_RESIZABLE;
        } else {
            wmPtr->flags |= WM_HEIGHT_NOT_RESIZABLE;
        }
        if (!((wmPtr->flags & WM_NEVER_MAPPED)
                && !(winPtr->flags & TK_EMBEDDED))) {
            UpdateWrapper(winPtr);
        }
        goto updateGeom;
    } else if ((c == 's') && (strncmp(argv[1], "sizefrom", length) == 0)
            && (length >= 2)) {
        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " sizefrom window ?user|program?\"",
        	    (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (wmPtr->sizeHintsFlags & USSize) {
                Tcl_SetResult(interp, "user", TCL_STATIC);
            } else if (wmPtr->sizeHintsFlags & PSize) {
                Tcl_SetResult(interp, "program", TCL_STATIC);
            }
            return TCL_OK;
        }
        if (*argv[3] == '\0') {
            wmPtr->sizeHintsFlags &= ~(USSize|PSize);
        } else {
            c = argv[3][0];
            length = strlen(argv[3]);
            if ((c == 'u') && (strncmp(argv[3], "user", length) == 0)) {
        	wmPtr->sizeHintsFlags &= ~PSize;
        	wmPtr->sizeHintsFlags |= USSize;
            } else if ((c == 'p')
        	    && (strncmp(argv[3], "program", length) == 0)) {
        	wmPtr->sizeHintsFlags &= ~USSize;
        	wmPtr->sizeHintsFlags |= PSize;
            } else {
        	Tcl_AppendResult(interp, "bad argument \"", argv[3],
        		"\": must be program or user", (char *) NULL);
        	return TCL_ERROR;
            }
        }
        goto updateGeom;
    } else if ((c == 's') && (strncmp(argv[1], "state", length) == 0)
            && (length >= 2)) {
        if ((argc < 3) || (argc > 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " state window ?state?\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 4) {
            if (wmPtr->iconFor != NULL) {
                Tcl_AppendResult(interp, "can't change state of ", argv[2],
                        ": it is an icon for ", Tk_PathName(wmPtr->iconFor),
                        (char *) NULL);
                return TCL_ERROR;
            }
            if (winPtr->flags & TK_EMBEDDED) {
                Tcl_AppendResult(interp, "can't change state of ",
                        winPtr->pathName, ": it is an embedded window",
                        (char *) NULL);
                return TCL_ERROR;
            }

            c = argv[3][0];
            length = strlen(argv[3]);

            if ((c == 'n') && (strncmp(argv[3], "normal", length) == 0)) {
                TkpWmSetState(winPtr, NormalState);
                /*
                 * This varies from 'wm deiconify' because it does not
                 * force the window to be raised and receive focus
                 */
            } else if ((c == 'i')
                    && (strncmp(argv[3], "iconic", length) == 0)) {
                if (Tk_Attributes((Tk_Window) winPtr)->override_redirect) {
                    Tcl_AppendResult(interp, "can't iconify \"",
                            winPtr->pathName,
                            "\": override-redirect flag is set",
                            (char *) NULL);
                    return TCL_ERROR;
                }
                if (wmPtr->masterPtr != NULL) {
                    Tcl_AppendResult(interp, "can't iconify \"",
                            winPtr->pathName,
                            "\": it is a transient", (char *) NULL);
                    return TCL_ERROR;
                }
                TkpWmSetState(winPtr, IconicState);
            } else if ((c == 'w')
                    && (strncmp(argv[3], "withdrawn", length) == 0)) {
                TkpWmSetState(winPtr, WithdrawnState);
            } else if ((c == 'z')
                    && (strncmp(argv[3], "zoomed", length) == 0)) {
                TkpWmSetState(winPtr, ZoomState);
            } else {
                Tcl_AppendResult(interp, "bad argument \"", argv[3],
                        "\": must be normal, iconic, withdrawn or zoomed",
                        (char *) NULL);
                return TCL_ERROR;
            }
        } else {
            if (wmPtr->iconFor != NULL) {
                Tcl_SetResult(interp, "icon", TCL_STATIC);
            } else {
                switch (wmPtr->hints.initial_state) {
                    case NormalState:
                        Tcl_SetResult(interp, "normal", TCL_STATIC);
                        break;
                    case IconicState:
                        Tcl_SetResult(interp, "iconic", TCL_STATIC);
                        break;
                    case WithdrawnState:
                        Tcl_SetResult(interp, "withdrawn", TCL_STATIC);
                        break;
                    case ZoomState:
                        Tcl_SetResult(interp, "zoomed", TCL_STATIC);
                        break;
                }
            }
        }
    } else if ((c == 't') && (strncmp(argv[1], "title", length) == 0)
            && (length >= 2)) {
        if (argc > 4) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " title window ?newTitle?\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            Tcl_SetResult(interp, ((wmPtr->titleUid != NULL) ?
                    wmPtr->titleUid : winPtr->nameUid), TCL_STATIC);
            return TCL_OK;
        } else {
            wmPtr->titleUid = Tk_GetUid(argv[3]);
            if ((!(wmPtr->flags & WM_NEVER_MAPPED)) &&
                    wmPtr->wrapper != NULLHANDLE) {
                HSWITCH hSwitch;
                SWCNTRL switchData;
                Tcl_DString titleString;

                Tcl_UtfToExternalDString(NULL, wmPtr->titleUid, -1,
                        &titleString);

                hSwitch = WinQuerySwitchHandle(wmPtr->wrapper, 0);
#ifdef VERBOSE
                if (hSwitch == NULLHANDLE) {
                    printf("WinQuerySwitchHandle %x ERROR %x\n", wmPtr->wrapper,
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("WinQuerySwitchHandle %x OK:%x\n", wmPtr->wrapper,
                           hSwitch);
                }
#endif
                rc = WinQuerySwitchEntry(hSwitch, &switchData);
#ifdef VERBOSE
                if (rc != 0) {
                    printf("WinQuerySwitchEntry %x ERROR %x\n", hSwitch,
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("WinQuerySwitchEntry %x OK\n", hSwitch);
                }
#endif
                /* Set visibility on */
                strncpy(switchData.szSwtitle, Tcl_DStringValue(&titleString),
                        MAXNAMEL+4);
                rc = WinChangeSwitchEntry(hSwitch, &switchData);
#ifdef VERBOSE
                if (rc != 0) {
                    printf("WinChangeSwitchEntry %x ERROR %x\n", hSwitch,
                           WinGetLastError(TclOS2GetHAB()));
                } else {
                    printf("WinChangeSwitchEntry %x OK\n", hSwitch);
                }
#endif
        	WinSetWindowText(wmPtr->wrapper,
                                 Tcl_DStringValue(&titleString));

                Tcl_DStringFree(&titleString);
            }
        }
    } else if ((c == 't') && (strncmp(argv[1], "transient", length) == 0)
            && (length >= 3)) {
        TkWindow *masterPtr = wmPtr->masterPtr;

        if ((argc != 3) && (argc != 4)) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " transient window ?master?\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (argc == 3) {
            if (masterPtr != NULL) {
                Tcl_SetResult(interp, Tk_PathName(masterPtr), TCL_STATIC);
            }
            return TCL_OK;
        }
        if (masterPtr != NULL) {
            /*
             * If we had a master, tell them that we aren't tied
             * to them anymore
             */
            masterPtr->wmInfoPtr->numTransients--;
            Tk_DeleteEventHandler((Tk_Window) masterPtr,
                    VisibilityChangeMask,
                    WmWaitVisibilityProc, (ClientData) winPtr);
        }
        if (argv[3][0] == '\0') {
            wmPtr->masterPtr = NULL;
        } else {
            masterPtr = (TkWindow *) Tk_NameToWindow(interp, argv[3], tkwin);
            if (masterPtr == NULL) {
        	return TCL_ERROR;
            }
            if (masterPtr == winPtr) {
                wmPtr->masterPtr = NULL;
            } else if (masterPtr != wmPtr->masterPtr) {
                Tk_MakeWindowExist((Tk_Window)masterPtr);

                /*
                 * Ensure that the master window is actually a Tk toplevel.
                 */

                while (!(masterPtr->flags & TK_TOP_LEVEL)) {
                    masterPtr = masterPtr->parentPtr;
                }
                wmPtr->masterPtr = masterPtr;
                masterPtr->wmInfoPtr->numTransients++;

                /*
                 * Bind a visibility event handler to the master window,
                 * to ensure that when it is mapped, the children will
                 * have their state set properly.
                 */

                Tk_CreateEventHandler((Tk_Window) masterPtr,
                        VisibilityChangeMask,
                        WmWaitVisibilityProc, (ClientData) winPtr);
            }
        }
        if (!((wmPtr->flags & WM_NEVER_MAPPED)
                && !(winPtr->flags & TK_EMBEDDED))) {
            UpdateWrapper(winPtr);
        }
    } else if ((c == 'w') && (strncmp(argv[1], "withdraw", length) == 0)) {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # arguments: must be \"",
        	    argv[0], " withdraw window\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (wmPtr->iconFor != NULL) {
            Tcl_AppendResult(interp, "can't withdraw ", argv[2],
        	    ": it is an icon for ", Tk_PathName(wmPtr->iconFor),
        	    (char *) NULL);
            return TCL_ERROR;
        }
        TkpWmSetState(winPtr, WithdrawnState);
    } else {
        Tcl_AppendResult(interp, "unknown or ambiguous option \"", argv[1],
        	"\": must be aspect, client, command, deiconify, ",
        	"focusmodel, frame, geometry, grid, group, iconbitmap, ",
        	"iconify, iconmask, iconname, iconposition, ",
        	"iconwindow, maxsize, minsize, overrideredirect, ",
        	"positionfrom, protocol, resizable, sizefrom, state, title, ",
        	"transient, or withdraw",
        	(char *) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;

    updateGeom:
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
    return TCL_OK;
}
        /*ARGSUSED*/
static void
WmWaitVisibilityProc(clientData, eventPtr)
    ClientData clientData;      /* Pointer to window. */
    XEvent *eventPtr;           /* Information about event. */
{
    TkWindow *winPtr = (TkWindow *) clientData;
    TkWindow *masterPtr = winPtr->wmInfoPtr->masterPtr;

    if ((eventPtr->type == VisibilityNotify) && (masterPtr != NULL)) {
        int state = masterPtr->wmInfoPtr->hints.initial_state;

        if ((state == NormalState) || (state == ZoomState)) {
            state = winPtr->wmInfoPtr->hints.initial_state;
            if ((state == NormalState) || (state == ZoomState)) {
                UpdateWrapper(winPtr);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetGrid --
 *
 *	This procedure is invoked by a widget when it wishes to set a grid
 *	coordinate system that controls the size of a top-level window.
 *	It provides a C interface equivalent to the "wm grid" command and
 *	is usually asscoiated with the -setgrid option.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Grid-related information will be passed to the window manager, so
 *	that the top-level window associated with tkwin will resize on
 *	even grid units.  If some other window already controls gridding
 *	for the top-level window then this procedure call has no effect.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetGrid(tkwin, reqWidth, reqHeight, widthInc, heightInc)
    Tk_Window tkwin;		/* Token for window.  New window mgr info
        			 * will be posted for the top-level window
        			 * associated with this window. */
    int reqWidth;		/* Width (in grid units) corresponding to
        			 * the requested geometry for tkwin. */
    int reqHeight;		/* Height (in grid units) corresponding to
        			 * the requested geometry for tkwin. */
    int widthInc, heightInc;	/* Pixel increments corresponding to a
        			 * change of one grid unit. */
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    register WmInfo *wmPtr;

#ifdef VERBOSE
    printf("Tk_SetGrid\n");
#endif

    /*
     * Find the top-level window for tkwin, plus the window manager
     * information.
     */

    while (!(winPtr->flags & TK_TOP_LEVEL)) {
        winPtr = winPtr->parentPtr;
    }
    wmPtr = winPtr->wmInfoPtr;

    if ((wmPtr->gridWin != NULL) && (wmPtr->gridWin != tkwin)) {
        return;
    }

    if ((wmPtr->reqGridWidth == reqWidth)
            && (wmPtr->reqGridHeight == reqHeight)
            && (wmPtr->widthInc == widthInc)
            && (wmPtr->heightInc == heightInc)
            && ((wmPtr->sizeHintsFlags & (PBaseSize|PResizeInc))
        	    == (PBaseSize|PResizeInc))) {
        return;
    }

    /*
     * If gridding was previously off, then forget about any window
     * size requests made by the user or via "wm geometry":  these are
     * in pixel units and there's no easy way to translate them to
     * grid units since the new requested size of the top-level window in
     * pixels may not yet have been registered yet (it may filter up
     * the hierarchy in DoWhenIdle handlers).  However, if the window
     * has never been mapped yet then just leave the window size alone:
     * assume that it is intended to be in grid units but just happened
     * to have been specified before this procedure was called.
     */

    if ((wmPtr->gridWin == NULL) && !(wmPtr->flags & WM_NEVER_MAPPED)) {
        wmPtr->width = -1;
        wmPtr->height = -1;
    }

    /* 
     * Set the new gridding information, and start the process of passing
     * all of this information to the window manager.
     */

    wmPtr->gridWin = tkwin;
    wmPtr->reqGridWidth = reqWidth;
    wmPtr->reqGridHeight = reqHeight;
    wmPtr->widthInc = widthInc;
    wmPtr->heightInc = heightInc;
    wmPtr->sizeHintsFlags |= PBaseSize|PResizeInc;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UnsetGrid --
 *
 *	This procedure cancels the effect of a previous call
 *	to Tk_SetGrid.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If tkwin currently controls gridding for its top-level window,
 *	gridding is cancelled for that top-level window;  if some other
 *	window controls gridding then this procedure has no effect.
 *
 *----------------------------------------------------------------------
 */

void
Tk_UnsetGrid(tkwin)
    Tk_Window tkwin;		/* Token for window that is currently
        			 * controlling gridding. */
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    register WmInfo *wmPtr;

#ifdef VERBOSE
    printf("Tk_UnsetGrid\n");
#endif

    /*
     * Find the top-level window for tkwin, plus the window manager
     * information.
     */

    while (!(winPtr->flags & TK_TOP_LEVEL)) {
        winPtr = winPtr->parentPtr;
    }
    wmPtr = winPtr->wmInfoPtr;
    if (tkwin != wmPtr->gridWin) {
        return;
    }

    wmPtr->gridWin = NULL;
    wmPtr->sizeHintsFlags &= ~(PBaseSize|PResizeInc);
    if (wmPtr->width != -1) {
        wmPtr->width = winPtr->reqWidth + (wmPtr->width
        	- wmPtr->reqGridWidth)*wmPtr->widthInc;
        wmPtr->height = winPtr->reqHeight + (wmPtr->height
        	- wmPtr->reqGridHeight)*wmPtr->heightInc;
    }
    wmPtr->widthInc = 1;
    wmPtr->heightInc = 1;

    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelEventProc --
 *
 *	This procedure is invoked when a top-level (or other externally-
 *	managed window) is restructured in any way.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Tk's internal data structures for the window get modified to
 *	reflect the structural change.
 *
 *----------------------------------------------------------------------
 */

static void
TopLevelEventProc(clientData, eventPtr)
    ClientData clientData;		/* Window for which event occurred. */
    XEvent *eventPtr;			/* Event that just happened. */
{
    register TkWindow *winPtr = (TkWindow *) clientData;

#ifdef VERBOSE
    printf("TopLevelEventProc, winPtr %x, eventPtr %x\n", winPtr, eventPtr);
#endif

    if (eventPtr->type == DestroyNotify) {
        Tk_ErrorHandler handler;

        if (!(winPtr->flags & TK_ALREADY_DEAD)) {
            /*
             * A top-level window was deleted externally (e.g., by the window
             * manager).  This is probably not a good thing, but cleanup as
             * best we can.  The error handler is needed because
             * Tk_DestroyWindow will try to destroy the window, but of course
             * it's already gone.
             */
    
#ifdef VERBOSE
            printf("    TK_ALREADY_DEAD\n");
#endif
            handler = Tk_CreateErrorHandler(winPtr->display, -1, -1, -1,
        	    (Tk_ErrorProc *) NULL, (ClientData) NULL);
            Tk_DestroyWindow((Tk_Window) winPtr);
            Tk_DeleteErrorHandler(handler);
        }
    } else if (eventPtr->type == ConfigureNotify) {
        WmInfo *wmPtr;
        wmPtr = winPtr->wmInfoPtr;

        if (winPtr->flags & TK_EMBEDDED) {
            Tk_Window tkwin = (Tk_Window)winPtr;
            WinSendMsg(wmPtr->wrapper, TK_GEOMETRYREQ,
                       (MPARAM) Tk_ReqWidth(tkwin),
                       (MPARAM) Tk_ReqHeight(tkwin));
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelReqProc --
 *
 *	This procedure is invoked by the geometry manager whenever
 *	the requested size for a top-level window is changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Arrange for the window to be resized to satisfy the request
 *	(this happens as a when-idle action).
 *
 *----------------------------------------------------------------------
 */

        /* ARGSUSED */
static void
TopLevelReqProc(dummy, tkwin)
    ClientData dummy;			/* Not used. */
    Tk_Window tkwin;			/* Information about window. */
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    WmInfo *wmPtr;

#ifdef VERBOSE
    printf("TopLevelReqProc\n");
#endif

    wmPtr = winPtr->wmInfoPtr;
    if (winPtr->flags & TK_EMBEDDED) {
        WinSendMsg(wmPtr->wrapper, TK_GEOMETRYREQ,
                   MPFROMLONG(Tk_ReqWidth(tkwin)),
                   MPFROMLONG(Tk_ReqHeight(tkwin)));
    }
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateGeometryInfo --
 *
 *	This procedure is invoked when a top-level window is first
 *	mapped, and also as a when-idle procedure, to bring the
 *	geometry and/or position of a top-level window back into
 *	line with what has been requested by the user and/or widgets.
 *	This procedure doesn't return until the system has
 *	responded to the geometry change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window's size and location may change, unless the WM prevents
 *	that from happening.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateGeometryInfo(clientData)
    ClientData clientData;		/* Pointer to the window's record. */
{
    int x, y;			/* Position of border on desktop */
    int width, height;		/* Size of client area */
    register TkWindow *winPtr = (TkWindow *) clientData;
    register WmInfo *wmPtr = winPtr->wmInfoPtr;
    ULONG wrapperStyle;

#ifdef VERBOSE
    SWP oldPos;
    printf("UpdateGeometryInfo winPtr %x wrapper %x\n", winPtr, wmPtr->wrapper);
    if (wmPtr->wrapper != NULLHANDLE) {
    rc = WinQueryWindowPos(wmPtr->wrapper, &oldPos);
    printf("UpdateGeometryInfo pos before processing: (%d,%d) %dx%d x11y %d\n",
           oldPos.x, oldPos.y, oldPos.cx, oldPos.cy,
           yScreen - oldPos.y - oldPos.cy);
    }
#endif

    wmPtr->flags &= ~WM_UPDATE_PENDING;

    /*
     * If the window is minimized or maximized, we should not update
     * our geometry since it will end up with the wrong values.
     * ConfigureToplevel will reschedule UpdateGeometryInfo when the
     * state of the window changes.
     */

    if (wmPtr->wrapper != NULLHANDLE) {
        wrapperStyle = WinQueryWindowULong(wmPtr->wrapper, QWL_STYLE);
        if ((wrapperStyle & WS_MINIMIZED) || (wrapperStyle & WS_MAXIMIZED)) {
            return;
        }
    }

    /*
     * Compute the border size for the current window style.  This
     * size will include the resize handles, the title bar and the
     * menubar.
     * wmPtr->wrapper may be 0, so we cannot use WinCalcFrameRect
     * and there will not be a menu bar => use defines.
     */

    if (!wmPtr->wrapper) {
        if (winPtr->atts.override_redirect) {
            wmPtr->borderWidth = BORDERWIDTH_OVERRIDE;
            wmPtr->borderHeight = BORDERHEIGHT_OVERRIDE;
        } else if (wmPtr->masterPtr) {
            if (! ((wmPtr->flags & WM_WIDTH_NOT_RESIZABLE) &&
                    (wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE))) {
                wmPtr->borderWidth = BORDERWIDTH_TOPLEVEL;
                wmPtr->borderHeight = BORDERHEIGHT_TOPLEVEL;
            } else {
                wmPtr->borderWidth = BORDERWIDTH_TRANSIENT;
                wmPtr->borderHeight = BORDERHEIGHT_TRANSIENT;
            }
        } else {
            wmPtr->borderWidth = BORDERWIDTH_TOPLEVEL;
            wmPtr->borderHeight = BORDERHEIGHT_TOPLEVEL;
        }
    } else {
        SWP oldPos;
        RECTL rectl;
        rc = WinQueryWindowPos(wmPtr->wrapper, &oldPos);
#ifdef VERBOSE
        if (rc == TRUE) {
             printf("    WinQueryWindowPos returned %d\n", rc);
        } else {
             printf("    WinQueryWindowPos ERROR %d\n",
                    WinGetLastError(TclOS2GetHAB()));
        }
#endif
        rectl.xLeft = oldPos.x;
        rectl.yBottom = oldPos.y;
        rectl.xRight = oldPos.x + oldPos.cx;
        rectl.yTop = oldPos.y + oldPos.cy;
#ifdef VERBOSE
        printf("    border WinCalcFrameRect (%d,%d) (%d,%d) =>\n", rectl.xLeft,
               rectl.yBottom, rectl.xRight, rectl.yTop);
#endif
        rc = WinCalcFrameRect(wmPtr->wrapper, &rectl, TRUE);
#ifdef VERBOSE
        if (rc == TRUE) {
            printf("   border WinCalcFrameRect OK => (%d,%d) (%d,%d)\n",
                   rectl.xLeft, rectl.yBottom, rectl.xRight, rectl.yTop);
        } else {
            if (rectl.xRight-rectl.xLeft ==0 || rectl.yTop-rectl.yBottom ==0) {
                printf("   border WinCalcFrameRect %x empty=> (%d,%d)(%d,%d)\n",
                       wmPtr->wrapper, rectl.xLeft, rectl.yBottom, rectl.xRight,
                       rectl.yTop);
            } else {
            printf("   border WinCalcFrameRect %x ERROR %x => (%d,%d)(%d,%d)\n",
                   wmPtr->wrapper, WinGetLastError(TclOS2GetHAB()), rectl.xLeft,
                   rectl.yBottom, rectl.xRight, rectl.yTop);
            }
        }
#endif
        wmPtr->borderWidth = oldPos.cx - (rectl.xRight - rectl.xLeft);
        wmPtr->borderHeight = oldPos.cy - (rectl.yTop - rectl.yBottom);
    }
#ifdef VERBOSE
    printf("    borderWidth %d, borderHeight %d\n", wmPtr->borderWidth,
           wmPtr->borderHeight);
#endif

    /*
     * Compute the new size for the top-level window.  See the
     * user documentation for details on this, but the size
     * requested depends on (a) the size requested internally
     * by the window's widgets, (b) the size requested by the
     * user in a "wm geometry" command or via wm-based interactive
     * resizing (if any), and (c) whether or not the window is
     * gridded.  Don't permit sizes <= 0 because this upsets
     * the X server.
     */

    if (wmPtr->width == -1) {
        width = winPtr->reqWidth;
#ifdef VERBOSE
        printf("UpdateGeometryInfo width = winPtr->reqWidth: %d\n", width);
#endif
    } else if (wmPtr->gridWin != NULL) {
        width = winPtr->reqWidth
        	+ (wmPtr->width - wmPtr->reqGridWidth)*wmPtr->widthInc;
#ifdef VERBOSE
        printf("UpdateGeometryInfo width = winPtr->reqWidth + ...: %d (%d + (%d - %d)* %d\n",
               width, winPtr->reqWidth, wmPtr->width, wmPtr->reqGridWidth,
               wmPtr->widthInc);
#endif
    } else {
        width = wmPtr->width;
#ifdef VERBOSE
        printf("UpdateGeometryInfo width = wmPtr->width: %d\n", width);
#endif
    }
    if (width <= 0) {
        width = 1;
#ifdef VERBOSE
        printf("UpdateGeometryInfo width = 1\n");
#endif
    }
    if (wmPtr->height == -1) {
        height = winPtr->reqHeight;
#ifdef VERBOSE
        printf("UpdateGeometryInfo height = winPtr->reqHeight: %d\n", height);
#endif
    } else if (wmPtr->gridWin != NULL) {
        height = winPtr->reqHeight
                + (wmPtr->height - wmPtr->reqGridHeight)*wmPtr->heightInc;
#ifdef VERBOSE
        printf("UpdateGeometryInfo height = winPtr->reqHeight + ...: %d (%d + (%d - %d)* %d\n",
               height, winPtr->reqHeight, wmPtr->height, wmPtr->reqGridHeight,
               wmPtr->heightInc);
#endif
    } else {
        height = wmPtr->height;
#ifdef VERBOSE
        printf("UpdateGeometryInfo height = wmPtr->height: %d\n", height);
#endif
    }
    if (height <= 0) {
        height = 1;
#ifdef VERBOSE
        printf("UpdateGeometryInfo height = 1\n");
#endif
    }

    /*
     * Compute the new position for the upper-left pixel of the window's
     * decorative frame.  This is tricky, because we need to include the
     * border widths supplied by a reparented parent in this calculation,
     * but can't use the parent's current overall size since that may
     * change as a result of this code.
     */

    if (wmPtr->flags & WM_NEGATIVE_X) {
        x = DisplayWidth(winPtr->display, winPtr->screenNum) - wmPtr->x
        	- (width + wmPtr->borderWidth);
    } else {
        x =  wmPtr->x;
#ifdef VERBOSE
        printf("UpdateGeometryInfo x = wmPtr->x: %d\n", x);
#endif
    }
    if (wmPtr->flags & WM_NEGATIVE_Y) {
#ifdef VERBOSE
        printf("UpdateGeometryInfo WM_NEGATIVE_Y\n");
#endif
        y = DisplayHeight(winPtr->display, winPtr->screenNum) - wmPtr->y
        	- (height + wmPtr->borderHeight);
    } else {
#ifdef VERBOSE
        printf("UpdateGeometryInfo non-WM_NEGATIVE_Y\n");
#endif
        y =  wmPtr->y;
#ifdef VERBOSE
        printf("UpdateGeometryInfo y = wmPtr->y: %d\n", y);
#endif
    }

    /*
     * If this window is embedded and the container is also in this
     * process, we don't need to do anything special about the
     * geometry, except to make sure that the desired size is known
     * by the container.  Also, zero out any position information,
     * since embedded windows are not allowed to move.
     */

    if (winPtr->flags & TK_BOTH_HALVES) {
        wmPtr->x = wmPtr->y = 0;
        wmPtr->flags &= ~(WM_NEGATIVE_X|WM_NEGATIVE_Y);
        Tk_GeometryRequest((Tk_Window) TkpGetOtherWindow(winPtr),
                width, height);
        return;
    }

    /*
     * Reconfigure the window if it isn't already configured correctly.  Base
     * the size check on what we *asked for* last time, not what we got.
     * Return immediately if there have been no changes in the requested
     * geometry of the toplevel.
     */
    /* TODO: need to add flag for possible menu size change */

    if (!((wmPtr->flags & WM_MOVE_PENDING)
            || (width != wmPtr->configWidth)
            || (height != wmPtr->configHeight))) {
        return;
    }
    wmPtr->flags &= ~WM_MOVE_PENDING;

    wmPtr->configWidth = width;
    wmPtr->configHeight = height;

   /*
    * Don't bother moving the window if we are in the process of
    * creating it.  Just update the geometry info based on what
    * we asked for.
    */

    if (wmPtr->flags & WM_CREATE_PENDING) {
        winPtr->changes.x = x;
        winPtr->changes.y = y;
        winPtr->changes.width = width;
        winPtr->changes.height = height;
#ifdef VERBOSE
        printf("    now: wP->c.w %d (width %d), wP->c.h %d (height %d)\n",
               winPtr->changes.width, width, winPtr->changes.height, height);
#endif
        return;
    }

    wmPtr->flags |= WM_SYNC_PENDING;
    if (winPtr->flags & TK_EMBEDDED) {
        /*
         * The wrapper window is in a different process, so we need
         * to send it a geometry request.  This protocol assumes that
         * the other process understands this Tk message, otherwise
         * our requested geometry will be ignored.
         */

        WinSendMsg(wmPtr->wrapper, TK_GEOMETRYREQ, MPFROMLONG(width),
                   MPFROMLONG(height));
    } else {
        int reqHeight, reqWidth;
        RECTL windowRect;
        int menuInc = WinQuerySysValue(HWND_DESKTOP, SV_CYMENU);
        int newHeight;
#ifdef VERBOSE
        printf("SV_CYMENU %d\n", menuInc);
#endif

        /*
         * We have to keep resizing the window until we get the
         * requested height in the client area. If the client
         * area has zero height, then the window rect is too
         * small by definition. Try increasing the border height
         * and try again. Once we have a positive size, then
         * we can adjust the height exactly. If the window
         * rect comes back smaller than we requested, we have
         * hit the maximum constraints that OS/2 imposes.
         * Once we find a positive client size, the next size
         * is the one we try no matter what.
         * Use SWP_NOADJUST to prevent getting a WM_QUERYTRACKINFO msg
         * (because of WM_ADJUSTWINDOWPOS message handling), which causes
         * the window to move up.
         */

        reqHeight = height + wmPtr->borderHeight;
        reqWidth = width + wmPtr->borderWidth;

        while (1) {
#ifdef VERBOSE
            printf("   WinSetWindowPos(%x TOP (%d,%d) %dx%d S|M|NA)\n",
                   wmPtr->wrapper, x, yScreen - y - reqHeight,
                   reqWidth, reqHeight);
#endif
            rc = WinSetWindowPos(wmPtr->wrapper, HWND_TOP, x,
                                 yScreen - y - reqHeight,
                                 reqWidth, reqHeight,
                                 SWP_SIZE | SWP_MOVE | SWP_NOADJUST);
#ifdef VERBOSE
            if (rc == TRUE) {
                 printf("    WinSetWindowPos returned %d\n", rc);
            } else {
                 printf("    WinSetWindowPos ERROR %x\n",
                        WinGetLastError(TclOS2GetHAB()));
            }
#endif
            rc = WinQueryWindowRect(wmPtr->wrapper, &windowRect);
            newHeight = windowRect.yTop - windowRect.yBottom;
#ifdef VERBOSE
            printf("    WinQueryWindowRect %x returns %x, newHeight %d\n",
                   wmPtr->wrapper, rc, newHeight);
#endif

            /*
             * If the request wasn't satisfied, we have hit an external
             * constraint and must stop.
             */

            if (newHeight < reqHeight) {
                break;
            }

            /*
             * Now check the size of the client area against our ideal.
             */

            rc = WinCalcFrameRect(wmPtr->wrapper, &windowRect, TRUE);
            newHeight = windowRect.yTop - windowRect.yBottom;
#ifdef VERBOSE
            printf("    WinCalcFrameRect %x returns %x newHeight %d\n",
                   wmPtr->wrapper, rc, newHeight);
#endif

            if (newHeight == height) {
                /*
                 * We're done.
                 */
                break;
            } else if (newHeight > height) {
                /*
                 * One last resize to get rid of the extra space.
                 */
                menuInc = newHeight - height;
                reqHeight -= menuInc;
                if (wmPtr->flags & WM_NEGATIVE_Y) {
                    y += menuInc;
                }
#ifdef VERBOSE
                printf("  last WinSetWindowPos(%x TOP (%d,%d) %dx%d S|M|NA)\n",
                       wmPtr->wrapper, x, yScreen - y - reqHeight,
                       reqWidth, reqHeight);
#endif
                rc = WinSetWindowPos(wmPtr->wrapper, HWND_TOP, x,
                                     yScreen - y - reqHeight,
        			     reqWidth, reqHeight,
                                     SWP_SIZE | SWP_MOVE | SWP_NOADJUST);
#ifdef VERBOSE
                if (rc == TRUE) {
                     printf("    WinSetWindowPos returned %d\n", rc);
                } else {
                     printf("    WinSetWindowPos ERROR %d\n",
                            WinGetLastError(TclOS2GetHAB()));
                }
#endif
                break;
            }

            /*
             * We didn't get enough space to satisfy our requested
             * height, so the menu must have wrapped.  Increase the
             * size of the window by one menu height and move the
             * window if it is positioned relative to the lower right
             * corner of the screen.
             */

            reqHeight += menuInc;
            if (wmPtr->flags & WM_NEGATIVE_Y) {
                y -= menuInc;
            }
        }
        if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
            /* Draw menu bar */
            WinSendMsg(wmPtr->wrapper, WM_UPDATEFRAME, MPFROMLONG(FCF_MENU),
                       MPVOID);
        }
    }
    wmPtr->flags &= ~WM_SYNC_PENDING;
}

/*
 *--------------------------------------------------------------
 *
 * ParseGeometry --
 *
 *	This procedure parses a geometry string and updates
 *	information used to control the geometry of a top-level
 *	window.
 *
 * Results:
 *	A standard Tcl return value, plus an error message in
 *      the interp's result if an error occurs.
 *
 * Side effects:
 *	The size and/or location of winPtr may change.
 *
 *--------------------------------------------------------------
 */

static int
ParseGeometry(interp, string, winPtr)
    Tcl_Interp *interp;		/* Used for error reporting. */
    char *string;		/* String containing new geometry.  Has the
        			 * standard form "=wxh+x+y". */
    TkWindow *winPtr;		/* Pointer to top-level window whose
        			 * geometry is to be changed. */
{
    register WmInfo *wmPtr = winPtr->wmInfoPtr;
    int x, y, width, height, flags;
    char *end;
    register char *p = string;
#ifdef VERBOSE
    printf("ParseGeometry %s\n", p);
    fflush(stdout);
#endif

    /*
     * The leading "=" is optional.
     */

    if (*p == '=') {
        p++;
    }

    /*
     * Parse the width and height, if they are present.  Don't
     * actually update any of the fields of wmPtr until we've
     * successfully parsed the entire geometry string.
     */

    width = wmPtr->width;
    height = wmPtr->height;
    x = wmPtr->x;
    y = wmPtr->y;
    flags = wmPtr->flags;
    if (isdigit(UCHAR(*p))) {
        width = strtoul(p, &end, 10);
        p = end;
        if (*p != 'x') {
            goto error;
        }
        p++;
        if (!isdigit(UCHAR(*p))) {
            goto error;
        }
        height = strtoul(p, &end, 10);
        p = end;
    }

    /*
     * Parse the X and Y coordinates, if they are present.
     */

    if (*p != '\0') {
        flags &= ~(WM_NEGATIVE_X | WM_NEGATIVE_Y);
        if (*p == '-') {
            flags |= WM_NEGATIVE_X;
        } else if (*p != '+') {
            goto error;
        }
        p++;
        if (!isdigit(UCHAR(*p)) && (*p != '-')) {
            goto error;
        }
        x = strtol(p, &end, 10);
        /* Beware of apps using +-x, eg. SpecTcl 1.1 */
        if (x < 0) {
            flags |= WM_NEGATIVE_X;
            x = -x;
        }
        p = end;
        if (*p == '-') {
            flags |= WM_NEGATIVE_Y;
        } else if (*p != '+') {
            goto error;
        }
        p++;
        if (!isdigit(UCHAR(*p)) && (*p != '-')) {
            goto error;
        }
        y = strtol(p, &end, 10);
        /* Beware of apps using +-y, eg. SpecTcl 1.1 */
        if (y < 0) {
            flags |= WM_NEGATIVE_Y;
            y = -y;
        }
        if (*end != '\0') {
            goto error;
        }

        /*
         * Assume that the geometry information came from the user,
         * unless an explicit source has been specified.  Otherwise
         * most window managers assume that the size hints were
         * program-specified and they ignore them.
         */

        if ((wmPtr->sizeHintsFlags & (USPosition|PPosition)) == 0) {
            wmPtr->sizeHintsFlags |= USPosition;
        }
    }

#ifdef VERBOSE
    printf("ParseGeometry %dx%d+%d+%d\n", width, height, x, y);
#endif

    /*
     * Everything was parsed OK.  Update the fields of *wmPtr and
     * arrange for the appropriate information to be percolated out
     * to the window manager at the next idle moment.
     */

    wmPtr->width = width;
    wmPtr->height = height;
    wmPtr->x = x;
    wmPtr->y = y;
    flags |= WM_MOVE_PENDING;
    wmPtr->flags = flags;

    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
    return TCL_OK;

    error:
    Tcl_AppendResult(interp, "bad geometry specifier \"",
            string, "\"", (char *) NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetRootCoords --
 *
 *	Given a token for a window, this procedure traces through the
 *	window's lineage to find the (virtual) root-window coordinates
 *	corresponding to point (0,0) in the window.
 *
 * Results:
 *	The locations pointed to by xPtr and yPtr are filled in with
 *	the root coordinates of the (0,0) point in tkwin.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetRootCoords(tkwin, xPtr, yPtr)
    Tk_Window tkwin;		/* Token for window. */
    int *xPtr;			/* Where to store x-displacement of (0,0). */
    int *yPtr;			/* Where to store y-displacement of (0,0). */
{
    register TkWindow *winPtr = (TkWindow *) tkwin;

#ifdef VERBOSE
    printf("Tk_GetRootCoords tkwin %x\n", tkwin);
#endif

    /*
     * If the window is mapped, let OS/2 figure out the translation by taking
     * the top-left position in window coordinates: (0,0) on X and Win,
     * (0,height) on OS/2.
     */

    if (winPtr->window != None) {
        HWND hwnd = Tk_GetHWND(winPtr->window);
        POINTL point;
        SWP pos;
        rc = WinQueryWindowPos(hwnd, &pos);
#ifdef VERBOSE
        printf("Tk_GetRootCoords h %x WinQueryWindowPos (%d,%d) %dx%d\n", hwnd,
               pos.x, pos.y, pos.cx, pos.cy);
#endif

        point.x = 0;
        point.y = 0;
#ifdef VERBOSE
        printf("Tk_GetRootCoords h %x (%d,%d) =>\n", hwnd, point.x, point.y);
#endif

        rc = WinMapWindowPoints (hwnd, HWND_DESKTOP, &point, 1);

        *xPtr = point.x;
        /* Translate to PM coordinates */
        *yPtr = yScreen - point.y - pos.cy;

#ifdef VERBOSE
        printf("Tk_GetRootCoords => (%d,%d) (PM %d,%d)\n", *xPtr, *yPtr,
               point.x, point.y);
        if (rc == TRUE) {
            printf("    WinMapWindowPoints %x (%d,%d) OK\n", hwnd, point.x,
                   point.y);
        } else {
            printf("    WinMapWindowPoints %x (%d,%d) ERROR %x\n", hwnd,
                   point.x, point.y, WinGetLastError(TclOS2GetHAB()));
        }
#endif
    } else {
        *xPtr = 0;
        *yPtr = 0;
#ifdef VERBOSE
        printf("Tk_GetRootCoords not mapped (0,0)\n");
#endif
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_CoordsToWindow --
 *
 *	Given the (virtual) root coordinates of a point, this procedure
 *	returns the token for the top-most window covering that point,
 *	if there exists such a window in this application.
 *
 * Results:
 *	The return result is either a token for the window corresponding
 *	to rootX and rootY, or else NULL to indicate that there is no such
 *	window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_CoordsToWindow(rootX, rootY, tkwin)
    int rootX, rootY;		/* Coordinates of point in root window.  If
        			 * a virtual-root window manager is in use,
        			 * these coordinates refer to the virtual
        			 * root, not the real root. */
    Tk_Window tkwin;		/* Token for any window in application;
        			 * used to identify the display. */
{
    POINTL pos;
    HWND hwnd;
    TkWindow *winPtr;

    pos.x = rootX;
    /* Translate to PM coordinates */
    pos.y = yScreen - rootY;
    hwnd = WinWindowFromPoint(HWND_DESKTOP, &pos, TRUE);

#ifdef VERBOSE
    printf("Tk_CoordsToWindow (%d,%d) PM %d tkwin %x gave hwnd %x\n", rootX,
           rootY, pos.y, tkwin, hwnd);
#endif

    winPtr = (TkWindow *) Tk_HWNDToWindow(hwnd);
#ifdef VERBOSE
    printf("Tk_HWNDToWindow (%x) winPtr %x\n", hwnd, winPtr);
#endif
    if (winPtr && (winPtr->mainPtr == ((TkWindow *) tkwin)->mainPtr)) {
        return (Tk_Window) winPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetVRootGeometry --
 *
 *	This procedure returns information about the virtual root
 *	window corresponding to a particular Tk window.
 *
 * Results:
 *	The values at xPtr, yPtr, widthPtr, and heightPtr are set
 *	with the offset and dimensions of the root window corresponding
 *	to tkwin.  If tkwin is being managed by a virtual root window
 *	manager these values correspond to the virtual root window being
 *	used for tkwin;  otherwise the offsets will be 0 and the
 *	dimensions will be those of the screen.
 *
 * Side effects:
 *	Vroot window information is refreshed if it is out of date.
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetVRootGeometry(tkwin, xPtr, yPtr, widthPtr, heightPtr)
    Tk_Window tkwin;		/* Window whose virtual root is to be
        			 * queried. */
    int *xPtr, *yPtr;		/* Store x and y offsets of virtual root
        			 * here. */
    int *widthPtr, *heightPtr;	/* Store dimensions of virtual root here. */
{
    TkWindow *winPtr = (TkWindow *) tkwin;

#ifdef VERBOSE
    printf("Tk_GetVRootGeometry\n");
#endif

    *xPtr = 0;
    *yPtr = 0;
    *widthPtr = DisplayWidth(winPtr->display, winPtr->screenNum);
    *heightPtr = DisplayHeight(winPtr->display, winPtr->screenNum);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MoveToplevelWindow --
 *
 *	This procedure is called instead of Tk_MoveWindow to adjust
 *	the x-y location of a top-level window.  It delays the actual
 *	move to a later time and keeps window-manager information
 *	up-to-date with the move
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is eventually moved so that its upper-left corner
 *	(actually, the upper-left corner of the window's decorative
 *	frame, if there is one) is at (x,y).
 *
 *----------------------------------------------------------------------
 */

void
Tk_MoveToplevelWindow(tkwin, x, y)
    Tk_Window tkwin;		/* Window to move. */
    int x, y;			/* New location for window (within
        			 * parent). */
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    register WmInfo *wmPtr = winPtr->wmInfoPtr;

#ifdef VERBOSE
    printf("Tk_MoveToplevelWindow\n");
#endif

    if (!(winPtr->flags & TK_TOP_LEVEL)) {
        panic("Tk_MoveToplevelWindow called with non-toplevel window");
    }
    wmPtr->x = x;
    wmPtr->y = y;
    wmPtr->flags |= WM_MOVE_PENDING;
    wmPtr->flags &= ~(WM_NEGATIVE_X|WM_NEGATIVE_Y);
    if ((wmPtr->sizeHintsFlags & (USPosition|PPosition)) == 0) {
        wmPtr->sizeHintsFlags |= USPosition;
    }

    /*
     * If the window has already been mapped, must bring its geometry
     * up-to-date immediately, otherwise an event might arrive from the
     * server that would overwrite wmPtr->x and wmPtr->y and lose the
     * new position.
     */

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            Tk_CancelIdleCall(UpdateGeometryInfo, (ClientData) winPtr);
        }
        UpdateGeometryInfo((ClientData) winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmProtocolEventProc --
 *
 *	This procedure is called by the Tk_HandleEvent whenever a
 *	ClientMessage event arrives whose type is "WM_PROTOCOLS".
 *	This procedure handles the message from the window manager
 *	in an appropriate fashion.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on what sort of handler, if any, was set up for the
 *	protocol.
 *
 *----------------------------------------------------------------------
 */

void
TkWmProtocolEventProc(winPtr, eventPtr)
    TkWindow *winPtr;		/* Window to which the event was sent. */
    XEvent *eventPtr;		/* X event. */
{
    WmInfo *wmPtr;
    register ProtocolHandler *protPtr;
    Atom protocol;
    int result;
    Tcl_Interp *interp;

#ifdef VERBOSE
    printf("TkWmProtocolEventProc\n");
#endif

    wmPtr = winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
        return;
    }
    protocol = (Atom) eventPtr->xclient.data.l[0];
    for (protPtr = wmPtr->protPtr; protPtr != NULL;
            protPtr = protPtr->nextPtr) {
        if (protocol == protPtr->protocol) {
            /*
             * Cache atom name, as we might destroy the window as a
             * result of the eval.
             */
            char *name = Tk_GetAtomName((Tk_Window) winPtr, protocol);

            Tcl_Preserve((ClientData) protPtr);
            interp = protPtr->interp;
            Tcl_Preserve((ClientData) interp);
            result = Tcl_GlobalEval(interp, protPtr->command);
            if (result != TCL_OK) {
                Tcl_AddErrorInfo(interp, "\n    (command for \"");
                Tcl_AddErrorInfo(interp, name);
                Tcl_AddErrorInfo(interp, "\" window manager protocol)");
                Tcl_BackgroundError(interp);
            }
            Tcl_Release((ClientData) interp);
            Tcl_Release((ClientData) protPtr);
            return;
        }
    }

    /*
     * No handler was present for this protocol.  If this is a
     * WM_DELETE_WINDOW message then just destroy the window.
     */

    if (protocol == Tk_InternAtom((Tk_Window) winPtr, "WM_DELETE_WINDOW")) {
        Tk_DestroyWindow((Tk_Window) winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmRestackToplevel --
 *
 *	This procedure restacks a top-level window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	WinPtr gets restacked  as specified by aboveBelow and otherPtr.
 *	This procedure doesn't return until the restack has taken
 *	effect and the ConfigureNotify event for it has been received.
 *
 *----------------------------------------------------------------------
 */

void
TkWmRestackToplevel(winPtr, aboveBelow, otherPtr)
    TkWindow *winPtr;		/* Window to restack. */
    int aboveBelow;		/* Gives relative position for restacking;
        			 * must be Above or Below. */
    TkWindow *otherPtr;		/* Window relative to which to restack;
        			 * if NULL, then winPtr gets restacked
        			 * above or below *all* siblings. */
{
    HWND hwnd, insertAfter;

#ifdef VERBOSE
    printf("TkWmRestackToplevel\n");
#endif
    /*
     * Can't set stacking order properly until the window is on the
     * screen (mapping it may give it a reparent window).
     */

    if (winPtr->window == None) {
        Tk_MakeWindowExist((Tk_Window) winPtr);
    }
    if (winPtr->wmInfoPtr->flags & WM_NEVER_MAPPED) {
        TkWmMapWindow(winPtr);
    }
    hwnd = (winPtr->wmInfoPtr->wrapper != NULLHANDLE)
        ? winPtr->wmInfoPtr->wrapper : Tk_GetHWND(winPtr->window);

    if (otherPtr != NULL) {
        if (otherPtr->window == None) {
            Tk_MakeWindowExist((Tk_Window) otherPtr);
        }
        if (otherPtr->wmInfoPtr->flags & WM_NEVER_MAPPED) {
            TkWmMapWindow(otherPtr);
        }
        insertAfter = (otherPtr->wmInfoPtr->wrapper != NULLHANDLE)
            ? otherPtr->wmInfoPtr->wrapper : Tk_GetHWND(otherPtr->window);
    } else {
        insertAfter = NULLHANDLE;
    }

    TkOS2SetWindowPos(hwnd, insertAfter, aboveBelow);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmAddToColormapWindows --
 *
 *	This procedure is called to add a given window to the
 *	WM_COLORMAP_WINDOWS property for its top-level, if it
 *	isn't already there.  It is invoked by the Tk code that
 *	creates a new colormap, in order to make sure that colormap
 *	information is propagated to the window manager by default.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	WinPtr's window gets added to the WM_COLORMAP_WINDOWS
 *	property of its nearest top-level ancestor, unless the
 *	colormaps have been set explicitly with the
 *	"wm colormapwindows" command.
 *
 *----------------------------------------------------------------------
 */

void
TkWmAddToColormapWindows(winPtr)
    TkWindow *winPtr;		/* Window with a non-default colormap.
        			 * Should not be a top-level window. */
{
    TkWindow *topPtr;
    TkWindow **oldPtr, **newPtr;
    int count, i;

#ifdef VERBOSE
    printf("TkWmAddToColormapWindows\n");
#endif

    if (winPtr->window == None) {
        return;
    }

    for (topPtr = winPtr->parentPtr; ; topPtr = topPtr->parentPtr) {
        if (topPtr == NULL) {
            /*
             * Window is being deleted.  Skip the whole operation.
             */

            return;
        }
        if (topPtr->flags & TK_TOP_LEVEL) {
            break;
        }
    }
    if (topPtr->wmInfoPtr->flags & WM_COLORMAPS_EXPLICIT) {
        return;
    }

    /*
     * Make sure that the window isn't already in the list.
     */

    count = topPtr->wmInfoPtr->cmapCount;
    oldPtr = topPtr->wmInfoPtr->cmapList;

    for (i = 0; i < count; i++) {
#ifdef VERBOSE
        printf("    oldPtr[%d] = %s\n", i, oldPtr[i]->pathName);
        fflush(stdout);
#endif
        if (oldPtr[i] == winPtr) {
            return;
        }
    }

    /*
     * Make a new bigger array and use it to reset the property.
     * Automatically add the toplevel itself as the last element
     * of the list.
     */

    newPtr = (TkWindow **) ckalloc((unsigned) ((count+2)*sizeof(TkWindow*)));
    if (count > 0) {
        memcpy(newPtr, oldPtr, count * sizeof(TkWindow*));
    }
    if (count == 0) {
        count++;
    }
    newPtr[count-1] = winPtr;
    newPtr[count] = topPtr;
#ifdef VERBOSE
    printf("    cmapList[count-1 = %d] = %s, cmapList[count] = %s\n", count-1,
           winPtr->pathName, topPtr->pathName);
    fflush(stdout);
#endif
    if (oldPtr != NULL) {
        ckfree((char *) oldPtr);
    }

    topPtr->wmInfoPtr->cmapList = newPtr;
    topPtr->wmInfoPtr->cmapCount = count+1;

    /*
     * Now we need to force the updated colormaps to be installed.
     */

    if (topPtr->wmInfoPtr == winPtr->dispPtr->foregroundWmPtr) {
        /* WM_QUERYNEWPALETTE -> WM_REALIZEPALETTE + focus notification */
        InstallColormaps(topPtr->wmInfoPtr->wrapper, WM_REALIZEPALETTE, 1);
    } else {
        /* WM_PALETTECHANGED -> WM_REALIZEPALETTE + focus notification */
        InstallColormaps(topPtr->wmInfoPtr->wrapper, WM_REALIZEPALETTE, 0);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmRemoveFromColormapWindows --
 *
 *      This procedure is called to remove a given window from the
 *      WM_COLORMAP_WINDOWS property for its top-level.  It is invoked
 *      when windows are deleted.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      WinPtr's window gets removed from the WM_COLORMAP_WINDOWS
 *      property of its nearest top-level ancestor, unless the
 *      top-level itself is being deleted too.
 *
 *----------------------------------------------------------------------
 */

void
TkWmRemoveFromColormapWindows(winPtr)
    TkWindow *winPtr;           /* Window that may be present in
                                 * WM_COLORMAP_WINDOWS property for its
                                 * top-level.  Should not be a top-level
                                 * window. */
{
    TkWindow *topPtr;
    TkWindow **oldPtr;
    int count, i, j;

#ifdef VERBOSE
    printf("TkWmRemoveFromColormapWindows\n");
#endif

    for (topPtr = winPtr->parentPtr; ; topPtr = topPtr->parentPtr) {
        if (topPtr == NULL) {
            /*
             * Ancestors have been deleted, so skip the whole operation.
             * Seems like this can't ever happen?
             */

            return;
        }
        if (topPtr->flags & TK_TOP_LEVEL) {
            break;
        }
    }
    if (topPtr->flags & TK_ALREADY_DEAD) {
        /*
         * Top-level is being deleted, so there's no need to cleanup
         * the WM_COLORMAP_WINDOWS property.
         */

        return;
    }

    /*
     * Find the window and slide the following ones down to cover
     * it up.
     */

    count = topPtr->wmInfoPtr->cmapCount;
    oldPtr = topPtr->wmInfoPtr->cmapList;
    for (i = 0; i < count; i++) {
        if (oldPtr[i] == winPtr) {
            for (j = i ; j < count-1; j++) {
                oldPtr[j] = oldPtr[j+1];
            }
            topPtr->wmInfoPtr->cmapCount = count-1;
            break;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2SetMenu --
 *
 *      Associcates a given menu window handle to a window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The menu will end up being drawn in the window, and the geometry
 *      of the window will have to be changed.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2SetMenu(tkwin, hMenu)
    Tk_Window tkwin;            /* the window to put the menu in */
    HWND hMenu;                 /* the menu to set */
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    WmInfo *wmPtr = winPtr->wmInfoPtr;
#ifdef VERBOSE
    printf("TkOS2SetMenu tkwin %x, hMenu %x\n", tkwin, hMenu);
#endif

    wmPtr->hMenu = hMenu;

    if (!(wmPtr->flags & TK_EMBEDDED)) {
        if (!(wmPtr->flags & WM_NEVER_MAPPED) && (hMenu != NULLHANDLE)) {
            int syncPending = wmPtr->flags & WM_SYNC_PENDING;

            wmPtr->flags |= WM_SYNC_PENDING;
            rc = WinSetParent(hMenu, wmPtr->wrapper, FALSE);
#ifdef VERBOSE
            printf("    WinSetParent(menu %x, parent %x, FALSE) returns %d\n",
                   hMenu, wmPtr->wrapper, rc);
#endif
            WinSetOwner(hMenu, wmPtr->wrapper);
            WinSendMsg(wmPtr->wrapper, WM_UPDATEFRAME, MPFROMLONG(FCF_MENU),
                       MPVOID);
            if (!syncPending) {
                wmPtr->flags &= ~WM_SYNC_PENDING;
            }
        }
        if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
            Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
            wmPtr->flags |= WM_UPDATE_PENDING|WM_MOVE_PENDING;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureTopLevel --
 *
 *	Generate a ConfigureNotify event based on the current position
 *	information.  This procedure is called by WmProc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues a new event.
 *
 *----------------------------------------------------------------------
 */

static void
ConfigureTopLevel(pos)
    SWP *pos; /* OS/2 PM y-coordinate */
{
    TkWindow *winPtr = GetTopLevel(pos->hwnd);
    WmInfo *wmPtr;
    int state;                  /* Current window state. */
    RECTL rectl;
    HWND client;

#ifdef VERBOSE
    printf("ConfigureTopLevel %x fl %x old %x, (%d,%d) %dx%d (x11y %d) c %x\n",
           pos->hwnd, pos[0].fl, pos[1].fl, pos->x, pos->y, pos->cx, pos->cy,
           yScreen - pos->cy - pos->y, WinWindowFromID(pos->hwnd, FID_CLIENT));
#endif

    if (winPtr == NULL) {
        return;
    }

    wmPtr = winPtr->wmInfoPtr;

    /*
     * Determine the current window state.
     */

    if (!WinIsWindowVisible(wmPtr->wrapper)) {
        state = WithdrawnState;
    } else {
        if (pos[0].fl & SWP_MAXIMIZE) {
            state = ZoomState;
        } else if (pos[0].fl & SWP_MINIMIZE) {
            state = IconicState;
        } else {
            state = NormalState;
        }
    }
#ifdef VERBOSE
    printf("ConfigureTopLevel %x state %s\n", wmPtr->wrapper,
           state == WithdrawnState ? "WithdrawnState" :
           (state == ZoomState ? "ZoomState" :
            (state == IconicState ? "IconicState" :
             (state == NormalState ? "NormalState" : "UNKNOWN"))));
    fflush(stdout);
#endif

    /*
     * If the state of the window just changed, be sure to update the
     * child window information.
     */

    if (wmPtr->hints.initial_state != state) {
        wmPtr->hints.initial_state = state;
        switch (state) {
            case WithdrawnState:
            case IconicState:
                XUnmapWindow(winPtr->display, winPtr->window);
                break;

            case NormalState:
                /*
                 * Schedule a geometry update.  Since we ignore geometry
                 * requests while in any other state, the geometry info
                 * may be stale.
                 */

                if (!(wmPtr->flags & WM_UPDATE_PENDING)) {
                    Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
                    wmPtr->flags |= WM_UPDATE_PENDING;
                }
                /* fall through */
            case ZoomState:
                XMapWindow(winPtr->display, winPtr->window);
                pos[0].fl &= ~(SWP_MOVE | SWP_SIZE);
                break;
        }
    }

    /*
     * Don't report geometry changes in the Iconic or Withdrawn states.
     */

    if (state == WithdrawnState || state == IconicState) {
        return;
    }

    /*
     * Compute the current geometry of the client area, reshape the
     * Tk window and generate a ConfigureNotify event.
     */

    client = WinWindowFromID(pos->hwnd, FID_CLIENT);
    rectl.xLeft = pos->x;
    rectl.xRight = pos->x + pos->cx;
    rectl.yBottom = pos->y;
    rectl.yTop = pos->y + pos->cy;
#ifdef VERBOSE
    printf("WinCalcFrameRect frame %x (%d,%d)(%d,%d) =>\n", pos->hwnd,
           rectl.xLeft, rectl.yBottom, rectl.xRight, rectl.yTop);
#endif
    rc = WinCalcFrameRect(pos->hwnd, &rectl, TRUE);
#ifdef VERBOSE
    printf("WinCalcFrameRect => (%d,%d)(%d,%d)\n", rectl.xLeft, rectl.yBottom,
           rectl.xRight, rectl.yTop);
    if (rc == TRUE) {
        printf("    configure WinCalcFrameRect OK => (%d,%d) (%d,%d)\n",
               rectl.xLeft, rectl.yBottom, rectl.xRight, rectl.yTop);
    } else {
        printf("    configure WinCalcFrameRect ERROR %x => (%d,%d) (%d,%d)\n",
               WinGetLastError(TclOS2GetHAB()), rectl.xLeft, rectl.yBottom,
    	       rectl.xRight, rectl.yTop);
    }
#endif
    winPtr->changes.x = rectl.xLeft;
    winPtr->changes.width = rectl.xRight - rectl.xLeft;
    winPtr->changes.height = rectl.yTop - rectl.yBottom;
    winPtr->changes.y = yScreen - rectl.yTop;
#ifdef VERBOSE
    printf("winPtr->changes (%d,%d) %dx%d\n", winPtr->changes.x,
           winPtr->changes.y, winPtr->changes.width, winPtr->changes.height);
#endif
    wmPtr->borderHeight = pos->cy - winPtr->changes.height;
#ifdef VERBOSE
    printf("    borderHeight %d (%d - %d)\n", wmPtr->borderHeight, pos->cy,
           winPtr->changes.height);
#endif
    rc = WinSetWindowPos(client, HWND_TOP, rectl.xLeft - pos->x,
                         rectl.yBottom - pos->y,
                         winPtr->changes.width, winPtr->changes.height,
                         SWP_SIZE | SWP_MOVE | SWP_NOADJUST);
#ifdef VERBOSE
    if (rc == TRUE) {
         printf("    WinSetWindowPos h %x (%d,%d) %dx%d returned %d (BH %d)\n",
                client, rectl.xLeft - pos->x, rectl.yBottom - pos->y,
                winPtr->changes.width, winPtr->changes.height, rc,
                wmPtr->borderHeight);
    } else {
         printf("    WinSetWindowPos h %x (%d,%d) %dx%d ERROR %d (BH %d)\n",
                client, rectl.xLeft - pos->x, rectl.yBottom - pos->y,
                winPtr->changes.width, winPtr->changes.height,
                WinGetLastError(TclOS2GetHAB()), wmPtr->borderHeight);
    }
#endif
    GenerateConfigureNotify(winPtr);

    /*
     * Update window manager geometry info if needed.
     */

    if (state == NormalState) {

        /*
         * Update size information from the event.  There are a couple of
         * tricky points here:
         *
         * 1. If the user changed the size externally then set wmPtr->width
         *    and wmPtr->height just as if a "wm geometry" command had been
         *    invoked with the same information.
         * 2. However, if the size is changing in response to a request
         *    coming from us (sync is set), then don't set
         *    wmPtr->width or wmPtr->height (otherwise the window will stop
         *    tracking geometry manager requests).
         */

        if (!(wmPtr->flags & WM_SYNC_PENDING)) {
            if (pos->fl & SWP_SIZE) {
                if ((wmPtr->width == -1)
                        && (winPtr->changes.width == winPtr->reqWidth)) {
                    /*
                     * Don't set external width, since the user didn't
                     * change it from what the widgets asked for.
                     */
                } else {
                    if (wmPtr->gridWin != NULL) {
                        wmPtr->width = wmPtr->reqGridWidth
                            + (winPtr->changes.width - winPtr->reqWidth)
                            / wmPtr->widthInc;
                        if (wmPtr->width < 0) {
                            wmPtr->width = 0;
                        }
                    } else {
                        wmPtr->width = winPtr->changes.width;
                    }
                }
                if ((wmPtr->height == -1)
                        && (winPtr->changes.height == winPtr->reqHeight)) {
                    /*
                     * Don't set external height, since the user didn't change
                     * it from what the widgets asked for.
                     */
                } else {
                    if (wmPtr->gridWin != NULL) {
                        wmPtr->height = wmPtr->reqGridHeight
                            + (winPtr->changes.height - winPtr->reqHeight)
                            / wmPtr->heightInc;
                        if (wmPtr->height < 0) {
                            wmPtr->height = 0;
                        }
                    } else {
                        wmPtr->height = winPtr->changes.height;
                    }
                }
                wmPtr->configWidth = winPtr->changes.width;
                wmPtr->configHeight = winPtr->changes.height;
            }
            /*
             * If the user moved the window, we should switch back
             * to normal coordinates.
             */

            if (pos->fl & SWP_MOVE) {
                wmPtr->flags &= ~(WM_NEGATIVE_X | WM_NEGATIVE_Y);
            }
        }

        /*
         * Update the wrapper window location information.
         */

        if (wmPtr->flags & WM_NEGATIVE_X) {
            wmPtr->x = DisplayWidth(winPtr->display, winPtr->screenNum)
                - pos->x - (winPtr->changes.width
                        + wmPtr->borderWidth);
        } else {
            wmPtr->x = pos->x;
        }
        if (wmPtr->flags & WM_NEGATIVE_Y) {
            wmPtr->y = pos->y;
        } else {
            wmPtr->y = yScreen - pos->y -
                       (winPtr->changes.height + wmPtr->borderHeight);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateConfigureNotify --
 *
 *      Generate a ConfigureNotify event from the current geometry
 *      information for the specified toplevel window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sends an X event.
 *
 *----------------------------------------------------------------------
 */

static void
GenerateConfigureNotify(winPtr)
    TkWindow *winPtr;
{
    XEvent event;

#ifdef VERBOSE
    printf("GenerateConfigureNotify winPtr %x\n", winPtr);
#endif

    /*
     * Generate a ConfigureNotify event.
     */

    event.type = ConfigureNotify;
    event.xconfigure.serial = winPtr->display->request;
    event.xconfigure.send_event = False;
    event.xconfigure.display = winPtr->display;
    event.xconfigure.event = winPtr->window;
    event.xconfigure.window = winPtr->window;
    event.xconfigure.border_width = winPtr->changes.border_width;
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;
    event.xconfigure.x = winPtr->changes.x;
    event.xconfigure.y = winPtr->changes.y;
    event.xconfigure.width = winPtr->changes.width;
    event.xconfigure.height = winPtr->changes.height;
    event.xconfigure.above = None;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * InstallColormaps --
 *
 *	Installs the colormaps associated with the toplevel which is
 *	currently active.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	May change the system palette and generate damage.
 *
 *----------------------------------------------------------------------
 */

static MRESULT
InstallColormaps(hwnd, message, isForemost)
    HWND hwnd;			/* Toplevel wrapper window whose colormaps
        			 * should be installed. */
    ULONG message;		/* Either WM_REALIZEPALETTE or
        			 * WM_SETFOCUS */
    int isForemost;		/* 1 if window is foremost, else 0 */
{
    int i;
    HPS hps, winPS;
    HWND winHwnd;
    HPAL oldPalette;
    ULONG colorsChanged;
    TkWindow *winPtr = GetTopLevel(hwnd);
    WmInfo *wmPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("InstallColormap hwnd %x msg %s fore %d\n", hwnd,
           message == WM_SETFOCUS ? "WM_SETFOCUS" : "WM_REALIZEPALETTE",
           isForemost);
    fflush(stdout);
#endif
            
    if (winPtr == NULL) {
        return (MRESULT) 0;
    }

    wmPtr = winPtr->wmInfoPtr;
/*****
    if (message == WM_SETFOCUS) {

        winPtr->dispPtr->foregroundWmPtr = wmPtr;

        if (wmPtr->cmapCount > 0) {
            winPtr = wmPtr->cmapList[0];
        }

        tsdPtr->systemPalette = TkOS2GetPalette(winPtr->atts.colormap);
        hps = WinGetPS(hwnd);
        oldPalette = GpiSelectPalette(hps, tsdPtr->systemPalette);
        if (WinRealizePalette(hwnd, hps, &colorsChanged) > 0) {
            RefreshColormap(winPtr->atts.colormap);
        } else if (wmPtr->cmapCount > 1) {
            GpiSelectPalette(hps, oldPalette);
            WinRealizePalette(hwnd, hps, &colorsChanged);
            WinReleasePS(hps);
            WinSendMsg(hwnd, WM_REALIZEPALETTE, (MPARAM)0L, (MPARAM)0L);
            return TRUE;
        }
        WinReleasePS(hps);

    } else {


        if (!isForemost) {
            if (wmPtr->cmapCount > 0) {
                winPtr = wmPtr->cmapList[0];
            }
            i = 1;
        } else {
            if (wmPtr->cmapCount <= 1) {
                return TRUE;
            }
            winPtr = wmPtr->cmapList[1];
            i = 2;
        }
        hps = WinGetPS(hwnd);
        oldPalette = GpiSelectPalette(hps,
                                      TkOS2GetPalette(winPtr->atts.colormap));
        if (WinRealizePalette(hwnd, hps, &colorsChanged) > 0) {
            RefreshColormap(winPtr->atts.colormap, winPtr->dispPtr);
        }
        for (; i < wmPtr->cmapCount; i++) {
            winPtr = wmPtr->cmapList[i];
            winHwnd = TkOS2GetHWND(winPtr->window);
            winPS = WinGetPS(winHwnd);
            GpiSelectPalette(winPS, TkOS2GetPalette(winPtr->atts.colormap));
            if (WinRealizePalette(winHwnd, winPS, &colorsChanged)) {
                RefreshColormap(winPtr->atts.colormap, winPtr->dispPtr);
            }
            WinReleasePS(winPS);
        }
        WinReleasePS(hps);
    }

    return TRUE;
*****/

    hps = WinGetPS(hwnd);

    /*
     * The application should call WinRealizePalette if it has a palette,
     * or pass on to the default window procedure if it doesn't.
     * If the return value from WinRealizePalette is greater than 0, the
     * application should invalidate its window to cause a repaint using
     * the newly-realized palette.
     */

    /*
     * Install all of the palettes.
     */

    if (wmPtr->cmapCount > 0) {
        winPtr = wmPtr->cmapList[0];
    }
    i = 1;

    oldPalette = GpiSelectPalette(hps, TkOS2GetPalette(winPtr->atts.colormap));
#ifdef VERBOSE
    printf("    GpiSelectPalette returns %d\n", oldPalette);
#endif
    if ( WinRealizePalette(hwnd, hps, &colorsChanged) > 0 ) {
#ifdef VERBOSE
        printf("    WinRealizePalette %x %x returns %d\n", hwnd, hps,
               colorsChanged);
#endif
        /* Invalidates window to cause repaint with newly realized palette */
        RefreshColormap(winPtr->atts.colormap, winPtr->dispPtr);
    }
    for (; i < wmPtr->cmapCount; i++) {
        winPtr = wmPtr->cmapList[i];
#ifdef VERBOSE
        printf("    colormap for %s\n", winPtr->pathName);
        fflush(stdout);
#endif
        winHwnd = TkOS2GetHWND(winPtr->window);
        winPS = WinGetPS(winHwnd);
        GpiSelectPalette(winPS, TkOS2GetPalette(winPtr->atts.colormap));
        if ( WinRealizePalette(winHwnd, winPS, &colorsChanged) > 0 ) {
#ifdef VERBOSE
            printf("    WinRealizePalette %x %x returns %d\n", winHwnd, winPS,
                   colorsChanged);
            fflush(stdout);
#endif
            /* Invalidate window to cause repaint with newly realized palette */
/*
            rc = WinInvalidateRect(winHwnd, NULL, FALSE);
#ifdef VERBOSE
            if (rc == TRUE) {
                printf("    WinInvalidateRect %x successfull\n", winHwnd);
            } else {
                printf("    WinInvalidateRect %x ERROR %x\n", winHwnd,
                       WinGetLastError(TclOS2GetHAB()));
            }
#endif
*/
        }
        WinReleasePS(winPS);
    }

    WinReleasePS(hps);
    return (MRESULT) TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * RefreshColormap --
 *
 *      This function is called to force all of the windows that use
 *      a given colormap to redraw themselves.  The quickest way to
 *      do this is to iterate over the toplevels, looking in the
 *      cmapList for matches.  This will quickly eliminate subtrees
 *      that don't use a given colormap.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Causes damage events to be generated.
 *
 *----------------------------------------------------------------------
 */

static void
RefreshColormap(colormap, dispPtr)
    Colormap colormap;
    TkDisplay *dispPtr;
{
    WmInfo *wmPtr;
    int i;

#ifdef VERBOSE
    printf("RefreshColormap\n");
#endif

    for (wmPtr = dispPtr->firstWmPtr; wmPtr != NULL; wmPtr = wmPtr->nextPtr) {
        if (wmPtr->cmapCount > 0) {
            for (i = 0; i < wmPtr->cmapCount; i++) {
                if ((wmPtr->cmapList[i]->atts.colormap == colormap)
                        && Tk_IsMapped(wmPtr->cmapList[i])) {
                    InvalidateSubTree(wmPtr->cmapList[i], colormap);
                }
            }
        } else if ((wmPtr->winPtr->atts.colormap == colormap)
                && Tk_IsMapped(wmPtr->winPtr)) {
            InvalidateSubTree(wmPtr->winPtr, colormap);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InvalidateSubTree --
 *
 *      This function recursively generates damage for a window and
 *      all of its mapped children that belong to the same toplevel and
 *      are using the specified colormap.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates damage for the specified subtree.
 *
 *----------------------------------------------------------------------
 */

static void
InvalidateSubTree(winPtr, colormap)
    TkWindow *winPtr;
    Colormap colormap;
{

    TkWindow *childPtr;

    /*
     * Generate damage for the current window if it is using the
     * specified colormap.
     */

#ifdef VERBOSE
    printf("InvalidateSubTree win %x, cmap %x\n", winPtr, colormap);
#endif

    if (winPtr->atts.colormap == colormap) {
        WinInvalidateRect(Tk_GetHWND(winPtr->window), NULL, FALSE);
    }
 
     for (childPtr = winPtr->childList; childPtr != NULL;
             childPtr = childPtr->nextPtr) {
         /*
          * We can stop the descent when we hit an unmapped or
          * toplevel window.
          */
 
         if (!Tk_IsTopLevel(childPtr) && Tk_IsMapped(childPtr)) {
#ifdef VERBOSE
             printf("    recurse from %x to %x\n", winPtr, childPtr);
#endif
             InvalidateSubTree(childPtr, colormap);
         }
     }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetSystemPalette --
 *
 *	Retrieves the currently installed foreground palette.
 *
 * Results:
 *	Returns the global foreground palette, if there is one.
 *	Otherwise, returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

HPAL
TkOS2GetSystemPalette()
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    return tsdPtr->systemPalette;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMinSize --
 *
 *      This procedure computes the current minWidth and minHeight
 *      values for a window, taking into account the possibility
 *      that they may be defaulted.
 *
 * Results:
 *      The values at *minWidthPtr and *minHeightPtr are filled
 *      in with the minimum allowable dimensions of wmPtr's window,
 *      in grid units.  If the requested minimum is smaller than the
 *      system required minimum, then this procedure computes the
 *      smallest size that will satisfy both the system and the
 *      grid constraints.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMinSize(wmPtr, minWidthPtr, minHeightPtr)
    WmInfo *wmPtr;              /* Window manager information for the
                                 * window. */
    int *minWidthPtr;           /* Where to store the current minimum
                                 * width of the window. */
    int *minHeightPtr;          /* Where to store the current minimum
                                 * height of the window. */
{
    int tmp, base;
    TkWindow *winPtr = wmPtr->winPtr;

#ifdef VERBOSE
    printf("GetMinSize %x defMW %d borderW %d defMH %d borderH %d\n",
           wmPtr->wrapper, wmPtr->defMinWidth, wmPtr->borderWidth,
           wmPtr->defMinHeight, wmPtr->borderHeight);
#endif

    /*
     * Compute the minimum width by taking the default client size
     * and rounding it up to the nearest grid unit.  Return the greater
     * of the default minimum and the specified minimum.
     */

    tmp = wmPtr->defMinWidth - wmPtr->borderWidth;
    if (tmp < 0) {
        tmp = 0;
    }
    if (wmPtr->gridWin != NULL) {
        base = winPtr->reqWidth - (wmPtr->reqGridWidth * wmPtr->widthInc);
        if (base < 0) {
            base = 0;
        }
        tmp = ((tmp - base) + wmPtr->widthInc - 1)/wmPtr->widthInc;
    }
    if (tmp < wmPtr->minWidth) {
        tmp = wmPtr->minWidth;
    }
    *minWidthPtr = tmp;

    /*
     * Compute the minimum height in a similar fashion.
     */

    tmp = wmPtr->defMinHeight - wmPtr->borderHeight;
    if (tmp < 0) {
        tmp = 0;
    }
    if (wmPtr->gridWin != NULL) {
        base = winPtr->reqHeight - (wmPtr->reqGridHeight * wmPtr->heightInc);
        if (base < 0) {
            base = 0;
        }
        tmp = ((tmp - base) + wmPtr->heightInc - 1)/wmPtr->heightInc;
    }
    if (tmp < wmPtr->minHeight) {
        tmp = wmPtr->minHeight;
    }
    *minHeightPtr = tmp;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMaxSize --
 *
 *      This procedure computes the current maxWidth and maxHeight
 *      values for a window, taking into account the possibility
 *      that they may be defaulted.
 *
 * Results:
 *      The values at *maxWidthPtr and *maxHeightPtr are filled
 *      in with the maximum allowable dimensions of wmPtr's window,
 *      in grid units.  If no maximum has been specified for the
 *      window, then this procedure computes the largest sizes that
 *      will fit on the screen.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMaxSize(wmPtr, maxWidthPtr, maxHeightPtr)
    WmInfo *wmPtr;              /* Window manager information for the
                                 * window. */
    int *maxWidthPtr;           /* Where to store the current maximum
                                 * width of the window. */
    int *maxHeightPtr;          /* Where to store the current maximum
                                 * height of the window. */
{
    int tmp;

#ifdef VERBOSE
    printf("GetMaxSize\n");
#endif

    if (wmPtr->maxWidth > 0) {
        *maxWidthPtr = wmPtr->maxWidth;
    } else {
        /*
         * Must compute a default width.  Fill up the display, leaving a
         * bit of extra space for the window manager's borders.
         */

        tmp = wmPtr->defMaxWidth - wmPtr->borderWidth;
        if (wmPtr->gridWin != NULL) {
            /*
             * Gridding is turned on;  convert from pixels to grid units.
             */

            tmp = wmPtr->reqGridWidth
                    + (tmp - wmPtr->winPtr->reqWidth)/wmPtr->widthInc;
        }
        *maxWidthPtr = tmp;
    }
    if (wmPtr->maxHeight > 0) {
        *maxHeightPtr = wmPtr->maxHeight;
    } else {
        tmp = wmPtr->defMaxHeight - wmPtr->borderHeight;
        if (wmPtr->gridWin != NULL) {
            tmp = wmPtr->reqGridHeight
                    + (tmp - wmPtr->winPtr->reqHeight)/wmPtr->heightInc;
        }
        *maxHeightPtr = tmp;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelProc --
 *
 *      Callback from OS/2 PM whenever an event occurs on a top level
 *      (plug-in) window.
 *
 * Results:
 *      Standard PM return value.
 *
 * Side effects:
 *      Default window behavior.
 *
 *----------------------------------------------------------------------
 */

static MRESULT EXPENTRY
TopLevelProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
#ifdef VERBOSE
    printf("TopLevelProc hwnd %x, msg %x, param1 %x, param2 %x\n", hwnd,
           message, param1, param2);
#endif
    if (message == WM_WINDOWPOSCHANGED) {
        SWP *pos = (SWP *) PVOIDFROMMP(param1);
        TkWindow *winPtr = (TkWindow *) Tk_HWNDToWindow(pos->hwnd);
#ifdef VERBOSE
        printf("Tk_HWNDToWindow (%x) winPtr %x (%d,%d) %dx%d\n", pos->hwnd,
               winPtr, pos->x, pos->y, pos->cx, pos->cy);
#endif

        if (winPtr == NULL) {
            return 0;
        }

        /*
         * Update the shape of the contained window.
         */

        if (pos->fl & SWP_SIZE) {
            winPtr->changes.width = pos->cx;
            winPtr->changes.height = pos->cy;
        }
        if (pos->fl & SWP_MOVE) {
            winPtr->changes.x = pos->x;
            winPtr->changes.y = TkOS2TranslateY(hwnd, pos->y, pos->cy);
        }

        GenerateConfigureNotify(winPtr);

        Tcl_ServiceAll();
        return 0;
    }
    return TkOS2ChildProc(hwnd, message, param1, param2);
}

/*
 *----------------------------------------------------------------------
 *
 * WmProc --
 *
 *      Callback from OS/2 PM whenever an event occurs on the decorative
 *      frame.
 *
 * Results:
 *      Standard PM return value.
 *
 * Side effects:
 *      Default window behavior.
 *
 *----------------------------------------------------------------------
 */

static MRESULT EXPENTRY
WmProc(hwnd, message, param1, param2)
    HWND hwnd;
    ULONG message;
    MPARAM param1;
    MPARAM param2;
{
    static int inMoveSize = 0;
    static int oldMode; /* This static is set upon entering move/size mode
                         * and is used to reset the service mode after
                         * leaving move/size mode.  Note that this mechanism
                         * assumes move/size is only one level deep. */
    MRESULT result;
    TkWindow *winPtr = NULL;
#ifdef VERBOSE
    printf("WmProc hwnd %x, msg %x, param1 %x, param2 %x\n", hwnd,
           message, param1, param2);
    switch (message) {
        case WM_ACTIVATE: printf("   WM_ACTIVATE Wm %x\n", hwnd); break;
        case WM_ADJUSTFRAMEPOS:
            printf("   WM_ADJUSTFRAMEPOS Wm %x (%d,%d) %dx%d\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
        	   ((PSWP)param1)->cy);
            break;
        case WM_ADJUSTWINDOWPOS:
            printf("   WM_ADJUSTWINDOWPOS Wm %x (%d,%d) %dx%d\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
        	   ((PSWP)param1)->cy);
            break;
        case WM_BUTTON1DOWN: printf("   WM_BUTTON1DOWN Wm %x\n", hwnd); break;
        case WM_BUTTON1UP: printf("   WM_BUTTON1UP Wm %x\n", hwnd); break;
        case WM_BUTTON1DBLCLK: printf("   WM_BUTTON1DBLCLK Wm %x\n", hwnd); break;
        case WM_BUTTON2DOWN: printf("   WM_BUTTON2DOWN Wm %x\n", hwnd); break;
        case WM_BUTTON2UP: printf("   WM_BUTTON2UP Wm %x\n", hwnd); break;
        case WM_BUTTON2DBLCLK: printf("   WM_BUTTON2DBLCLK Wm %x\n", hwnd); break;
        case WM_BUTTON3DOWN: printf("   WM_BUTTON3DOWN Wm %x\n", hwnd); break;
        case WM_BUTTON3UP: printf("   WM_BUTTON3UP Wm %x\n", hwnd); break;
        case WM_BUTTON3DBLCLK: printf("   WM_BUTTON3DBLCLK Wm %x\n", hwnd); break;
        case WM_CALCFRAMERECT: printf("   WM_CALCFRAMERECT Wm %x\n", hwnd); break;
        case WM_CALCVALIDRECTS: printf("   WM_CALCVALIDRECTS Wm %x\n", hwnd); break;
        case WM_CHAR: printf("   WM_CHAR Wm %x\n", hwnd); break;
        case WM_CLOSE: printf("   WM_CLOSE Wm %x\n", hwnd); break;
        case WM_COMMAND: printf("   WM_COMMAND Wm %x cmd %d s %d p %d\n",
                                hwnd, SHORT1FROMMP(param1),
                                SHORT1FROMMP(param2), SHORT2FROMMP(param2));
             break;
        case WM_CREATE: printf("   WM_CREATE Wm %x\n", hwnd); break;
        case WM_ERASEBACKGROUND: printf("   WM_ERASEBACKGROUND Wm %x\n", hwnd); break;
        case WM_FOCUSCHANGE: printf("   WM_FOCUSCHANGE Wm %x\n", hwnd); break;
        case WM_FORMATFRAME: printf("   WM_FORMATFRAME Wm %x\n", hwnd); break;
        case WM_HSCROLL: printf("   WM_HSCROLL Wm %x\n", hwnd); break;
        case WM_MINMAXFRAME: printf("   WM_MINMAXFRAME Wm %x\n", hwnd); break;
        case WM_MOUSEMOVE: printf("   WM_MOUSEMOVE Wm %x\n", hwnd); break;
        case WM_MOVE: printf("   WM_MOVE Wm %x\n", hwnd); break;
        case WM_OWNERPOSCHANGE: printf("   WM_OWNERPOSCHANGE Wm %x\n", hwnd); break;
        case WM_PAINT: printf("   WM_PAINT Wm %x\n", hwnd); break;
        case WM_QUERYBORDERSIZE: printf("   WM_QUERYBORDERSIZE Wm %x\n", hwnd); break;
        case WM_QUERYDLGCODE: printf("   WM_QUERYDLGCODE Wm %x\n", hwnd); break;
        case WM_QUERYFRAMECTLCOUNT: printf("   WM_QUERYFRAMECTLCOUNT Wm %x\n", hwnd); break;
        case WM_QUERYFOCUSCHAIN: printf("   WM_QUERYFOCUSCHAIN Wm %x\n", hwnd); break;
        case WM_QUERYICON: printf("   WM_QUERYICON Wm %x\n", hwnd); break;
        case WM_QUERYTRACKINFO: printf("   WM_QUERYTRACKINFO Wm %x\n", hwnd); break;
        case WM_REALIZEPALETTE: printf("   WM_REALIZEPALETTE Wm %x\n", hwnd); break;
        case WM_SETFOCUS: printf("   WM_SETFOCUS Wm %x\n", hwnd); break;
        case WM_SETSELECTION: printf("   WM_SETSELECTION Wm %x\n", hwnd); break;
        case WM_TRANSLATEACCEL: printf("   WM_TRANSLATEACCEL Wm %x\n", hwnd); break;
        case WM_UPDATEFRAME: printf("   WM_UPDATEFRAME Wm %x\n", hwnd); break;
        case WM_VSCROLL: printf("   WM_VSCROLL Wm %x\n", hwnd); break;
        case WM_WINDOWPOSCHANGED:
            printf("   WM_WINDOWPOSCHANGED Wm %x (%d,%d) %dx%d\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
        	   ((PSWP)param1)->cy);
            break;
    }
#endif

    if (TkOS2HandleMenuEvent(&hwnd, &message, &param1, &param2, &result)) {
        goto done;
    }

    switch (message) {
#ifdef VERBOSE
        case WM_TRANSLATEACCEL: {
            PQMSG pqmsg = PVOIDFROMMP(param1);
            USHORT flags = (USHORT) SHORT1FROMMP(pqmsg->mp1);
            USHORT charcode = (USHORT) SHORT1FROMMP(pqmsg->mp2);
            USHORT vkeycode = (USHORT) SHORT2FROMMP(pqmsg->mp2);
            printf("WM_TRANSLATEACCEL Wm vk %x char %x fl %x\n", vkeycode,
                   charcode, flags);
            break;
        }
#endif

        case WM_SETFOCUS:
#ifdef VERBOSE
            printf("WM_SETFOCUS hwnd %x focus %d\n", hwnd, param1);
#endif
            if (SHORT1FROMMP(param2) == FALSE) {
                /* Losing focus */
                result = (MRESULT)0;
                goto done;
            }
            result = oldFrameProc(hwnd, message, param1, param2);
            goto done;
            break;

        case WM_FOCUSCHANGE:
#ifdef VERBOSE
            printf("WM_FOCUSCHANGE hwnd %x focus %x focuswin %x fl %x\n", hwnd,
                   SHORT1FROMMP(param2), param1, SHORT2FROMMP(param2));
#endif
            if (SHORT1FROMMP(param2) == TRUE &&
                !(SHORT2FROMMP(param2) & FC_NOSETACTIVE) &&
                !(SHORT2FROMMP(param2) & FC_NOBRINGTOTOP) &&
                !(SHORT2FROMMP(param2) & FC_NOBRINGTOPFIRSTWINDOW)) {
                WinSetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
            }
            result = oldFrameProc(hwnd, message, param1, param2);
            goto done;

        case WM_ERASEBACKGROUND:
            result = (MRESULT)0;
            goto done;

        case WM_ADJUSTFRAMEPOS:
        case WM_QUERYTRACKINFO: {
            TRACKINFO track;
            TRACKINFO *trackPtr;
            if (message == WM_ADJUSTFRAMEPOS) {
                trackPtr = &track;
                trackPtr->fs = TF_MOVE;
            } else {
                trackPtr = (TRACKINFO *) PVOIDFROMMP(param2);
                trackPtr->fs = SHORT1FROMMP(param1);
#ifdef VERBOSE
                printf("QUERYTRACKINFO TF %x\n", trackPtr->fs);
#endif
            }
            inMoveSize = 1;
            oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
            SetLimits(hwnd, trackPtr);
            inMoveSize = 0;
            Tcl_SetServiceMode(oldMode);
            if (message == WM_ADJUSTFRAMEPOS) {
                /* use restraint info to modify SWP if necessary */
                PSWP swpPtr = (PSWP) PVOIDFROMMP(param1);
                LONG x11y =  yScreen - swpPtr->y - swpPtr->cy;
#ifdef VERBOSE
                printf("ADJUSTFRAMEPOS x11y %d (%d - %d - %d)\n", x11y, yScreen,
                       swpPtr->y, swpPtr->cy);
#endif
                if (swpPtr->cx > trackPtr->ptlMaxTrackSize.x) {
#ifdef VERBOSE
                    printf("ADJUSTFRAMEPOS cx %d -> %d\n", swpPtr->cx,
                           trackPtr->ptlMaxTrackSize.x);
#endif
                    swpPtr->cx = trackPtr->ptlMaxTrackSize.x;
                }
                if (swpPtr->cy > trackPtr->ptlMaxTrackSize.y) {
                    /* Remember we have reversed y-coordinates! */
#ifdef VERBOSE
                    printf("ADJUSTFRAMEPOS cy %d -> %d\n", swpPtr->cy,
                           trackPtr->ptlMaxTrackSize.y);
#endif
                    swpPtr->cy = trackPtr->ptlMaxTrackSize.y;
                }
                if (swpPtr->cx < trackPtr->ptlMinTrackSize.x) {
#ifdef VERBOSE
                    printf("ADJUSTFRAMEPOS cx %d -> %d\n", swpPtr->cx,
                           trackPtr->ptlMinTrackSize.x);
#endif
                    swpPtr->cx = trackPtr->ptlMinTrackSize.x;
                }
                if (swpPtr->cy < trackPtr->ptlMinTrackSize.y) {
                    /* Remember we have reversed y-coordinates! */
#ifdef VERBOSE
                    printf("ADJUSTFRAMEPOS cy %d -> %d\n", swpPtr->cy,
                           trackPtr->ptlMinTrackSize.y);
#endif
                    swpPtr->cy = trackPtr->ptlMinTrackSize.y;
                }
                /* Recompute PM y coordinate */
#ifdef VERBOSE
                printf("ADJUSTFRAMEPOS y %d -> %d / %d -> %d (%d - %d - %d)\n",
                       swpPtr->y, yScreen - x11y - swpPtr->cy,
                       trackPtr->ptlMinTrackSize.y, yScreen - x11y - swpPtr->cy,
                       yScreen, x11y, swpPtr->cy);
#endif
                swpPtr->y = yScreen - x11y - swpPtr->cy;
                trackPtr->rclTrack.yBottom = swpPtr->y;
                result = oldFrameProc(hwnd, message, param1, param2);
                goto done;
            } else {
                result = (MRESULT)1; /* continue sizing/moving */
                goto done;
            }
        }

        case WM_REALIZEPALETTE:
            if (hwnd == WinQueryFocus(HWND_DESKTOP)) {
                /* Only do when we're gaining the focus */
                InstallColormaps(hwnd, WM_REALIZEPALETTE, TRUE);
            } else {
                /* Only do when we're NOT gaining the focus */
                InstallColormaps(hwnd, WM_REALIZEPALETTE, FALSE);
            }
        rc = WinInvalidateRect(hwnd, NULL, FALSE);
#ifdef VERBOSE
        if (rc == TRUE) {
            printf("    WinInvalidateRect %x successfull\n", hwnd);
        } else {
            printf("    WinInvalidateRect %x ERROR %x\n", hwnd,
                   WinGetLastError(TclOS2GetHAB()));
        }
#endif
            result = 0;
            goto done;

        case WM_WINDOWPOSCHANGED:
            ConfigureTopLevel((PSWP) PVOIDFROMMP(param1));
            result = oldFrameProc(hwnd, message, param1, param2);
            goto done;

/*
        SMART: "WM_NCHITTEST
                Subclass the frame window and frame controls and process the
                WM_HITTEST message for each control, returning the
                appropriately mapped hittest code."
                result = (MRESULT)HT_NORMAL;
*/
/***** REMOVED 20010907
        case WM_HITTEST: {
            winPtr = GetTopLevel(hwnd);
            if (winPtr && (TkGrabState(winPtr) == TK_GRAB_EXCLUDED)) {
***** REMOVED 20010907 */
                /*
                 * This window is outside the grab hierarchy, so don't let any
                 * of the normal non-client processing occur.  Note that this
                 * implementation is not strictly correct because the grab
                 * might change between now and when the event would have been
                 * processed by Tk, but it's close enough.
                 */

/***** REMOVED 20010907
#ifdef VERBOSE
                printf("WmProc WM_HITTEST hwnd %x HT_DISCARD\n", hwnd);
#endif
                result = (MRESULT)HT_DISCARD;
                goto done;
            }
#ifdef VERBOSE
            printf("WmProc WM_HITTEST hwnd %x oldFrameProc (0x%x)\n", hwnd,
                   result);
#endif
            result = oldFrameProc(hwnd, message, param1, param2);
            goto done;
        }
***** REMOVED 20010907 */

        /*
         * The default frame window procedure sends the WM_CLOSE to the
         * client (child) if it exists, so we have to handle it there.
         * Also hand off mouse/button to default procedure.
         * This all ensures that click-to-activate etc. work as expected.
         */
        case WM_CLOSE:
        case WM_BUTTON1DOWN:
        case WM_BUTTON1UP:
        case WM_BUTTON1DBLCLK:
        case WM_BUTTON2DOWN:
        case WM_BUTTON2UP:
        case WM_BUTTON2DBLCLK:
        case WM_BUTTON3DOWN:
        case WM_BUTTON3UP:
        case WM_BUTTON3DBLCLK:
        case WM_MOUSEMOVE:
            result = oldFrameProc(hwnd, message, param1, param2);
            goto done;

        default:
            break;
    }

    winPtr = GetTopLevel(hwnd);
    if (winPtr && winPtr->window) {
        HWND child = Tk_GetHWND(winPtr->window);
        if ((message == WM_SETFOCUS) && (SHORT1FROMMP(param2) == TRUE)) {
            WinSetFocus(HWND_DESKTOP, child);
            result = (MRESULT)0;
#ifdef VERBOSE
            printf("WmProc not calling oldFrameProc after WinSetFocus\n");
#endif
            result = oldFrameProc(hwnd, message, param1, param2);
/*
*/
        } else if (!Tk_TranslateOS2Event(child, message, param1, param2,
                &result)) {
#ifdef VERBOSE
            printf("WmProc calling oldFrameProc after translation 0\n");
#endif
            result = oldFrameProc(hwnd, message, param1, param2);
        }
    } else {
#ifdef VERBOSE
        printf("WmProc calling oldFrameProc in else\n");
#endif
        result = oldFrameProc(hwnd, message, param1, param2);
    }

    done:
#ifdef VERBOSE
    switch (message) {
        case WM_ADJUSTFRAMEPOS:
            printf("   WM_ADJUSTFRAMEPOS Wm %x now (%d,%d) %dx%d\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
                   ((PSWP)param1)->cy);
            break;
        case WM_ADJUSTWINDOWPOS:
            printf("   WM_ADJUSTWINDOWPOS Wm %x now (%d,%d) %dx%d\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
                   ((PSWP)param1)->cy);
            break;
        case WM_WINDOWPOSCHANGED:
            printf("   WM_WINDOWPOSCHANGED Wm %x now (%d,%d) %dx%d\n", hwnd,
                   ((PSWP)param1)->x, ((PSWP)param1)->y, ((PSWP)param1)->cx,
                   ((PSWP)param1)->cy);
            break;
    }
#endif
    Tcl_ServiceAll();
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMakeMenuWindow --
 *
 *      Configure the window to be either a pull-down (or pop-up)
 *      menu, or as a toplevel (torn-off) menu or palette.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Changes the style bit used to create a new toplevel.
 *
 *----------------------------------------------------------------------
 */

void
TkpMakeMenuWindow(tkwin, transient)
    Tk_Window tkwin;            /* New window. */
    int transient;              /* 1 means menu is only posted briefly as
                                 * a popup or pulldown or cascade.  0 means
                                 * menu is always visible, e.g. as a torn-off
                                 * menu.  Determines whether save_under and
                                 * override_redirect should be set. */
{
    XSetWindowAttributes atts;
#ifdef VERBOSE
    printf("TkpMakeMenuWindow tkwin %x transient %d\n", tkwin, transient);
#endif

    if (transient) {
        atts.override_redirect = True;
        atts.save_under = True;
    } else {
        atts.override_redirect = False;
        atts.save_under = False;
    }

    if ((atts.override_redirect != Tk_Attributes(tkwin)->override_redirect)
            || (atts.save_under != Tk_Attributes(tkwin)->save_under)) {
        Tk_ChangeWindowAttributes(tkwin,
                CWOverrideRedirect|CWSaveUnder, &atts);
    }

}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2GetWrapperWindow --
 *
 *      Gets the OS/2 HWND for a given window.
 *
 * Results:
 *      Returns the wrapper window for a Tk window.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

HWND
TkOS2GetWrapperWindow(
    Tk_Window tkwin)            /* The window we need the wrapper from */
{
    TkWindow *winPtr = (TkWindow *)tkwin;
#ifdef VERBOSE
    printf("TkOS2GetWrapperWindow tkwin %x => hwnd %x\n", tkwin,
           winPtr->wmInfoPtr->wrapper);
#endif
    return (winPtr->wmInfoPtr->wrapper);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmFocusToplevel --
 *
 *      This is a utility procedure invoked by focus-management code. It
 *      exists because of the extra wrapper windows that exist under
 *      Unix; its job is to map from wrapper windows to the
 *      corresponding toplevel windows.  On PCs and Macs there are no
 *      wrapper windows so no mapping is necessary;  this procedure just
 *      determines whether a window is a toplevel or not.
 *
 * Results:
 *      If winPtr is a toplevel window, returns the pointer to the
 *      window; otherwise returns NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

TkWindow *
TkWmFocusToplevel(winPtr)
    TkWindow *winPtr;           /* Window that received a focus-related
                                 * event. */
{
#ifdef VERBOSE
    printf("TkWmFocusToplevel winPtr %x\n", winPtr);
#endif
    if (!(winPtr->flags & TK_TOP_LEVEL)) {
        return NULL;
    }
    return winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetWrapperWindow --
 *
 *      This is a utility procedure invoked by focus-management code. It
 *      maps to the wrapper for a top-level, which is just the same
 *      as the top-level on Macs and PCs.
 *
 * Results:
 *      If winPtr is a toplevel window, returns the pointer to the
 *      window; otherwise returns NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

TkWindow *
TkpGetWrapperWindow(
    TkWindow *winPtr)           /* Window that received a focus-related
                                 * event. */
{
#ifdef VERBOSE
    printf("TkpGetWrapperWindow winPtr %x\n", winPtr);
#endif
    if (!(winPtr->flags & TK_TOP_LEVEL)) {
        return NULL;
    }
    return winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ActivateWindow --
 *
 *      This function is called when an ActivateEvent is processed.
 *
 * Results:
 *      Returns 1 to indicate that the event was handled, else 0.
 *
 * Side effects:
 *      May activate the toplevel window associated with the event.
 *
 *----------------------------------------------------------------------
 */

static int
ActivateWindow(
    Tcl_Event *evPtr,           /* Pointer to ActivateEvent. */
    int flags)                  /* Notifier event mask. */
{
    TkWindow *winPtr;

    if (! (flags & TCL_WINDOW_EVENTS)) {
        return 0;
    }

    winPtr = ((ActivateEvent *) evPtr)->winPtr;

    /*
     * Ensure that the window is not excluded by a grab.
     */

    if (winPtr && (TkGrabState(winPtr) != TK_GRAB_EXCLUDED)) {
        WinSetFocus(HWND_DESKTOP, Tk_GetHWND(winPtr->window));
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2SetForegroundWindow --
 *
 *      This function is a wrapper for WinSetWindowPos SWP_ACTIVATE,
 *      calling it on the wrapper window because it has no affect on
 *      child windows.
 *
 * Results:
 *      none
 *
 * Side effects:
 *      May activate the toplevel window.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2SetForegroundWindow(winPtr)
    TkWindow *winPtr;
{
    register WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (wmPtr->wrapper != NULLHANDLE) {
        WinSetWindowPos(wmPtr->wrapper, HWND_TOP, 0L, 0L, 0L, 0L, SWP_ACTIVATE);
    } else {
        WinSetWindowPos(Tk_GetHWND(winPtr->window), HWND_TOP, 0L, 0L, 0L, 0L,
                        SWP_ACTIVATE);
    }
}

/* 
 * tkOS2Color.c --
 *
 *	Functions to map color names to system color values.
 *
 * Copyright (c) 1994 Software Research Associates, Inc.
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkOS2Int.h"
#include "tkColor.h"

/*
 * The thread specific data structure is define in a header file to be able
 * to access it in tkOS2Menu.c too.
 */
#include "tkOS2Color.h"

/*
 * The following structure is used to keep track of each color that is
 * allocated by this module.
 */

typedef struct OS2Color {
    TkColor info;               /* Generic color information. */
    int index;                  /* Index for WinQuerySysColor(), -1 if color
                                 * is not a "live" system color. */
} OS2Color;

/*
 * The sysColors array contains the names and index values for the
 * OS/2 PM indirect system color names.  In use, all of the names will
 * have the string "System" prepended, but we omit it in the table to
 * save space.
 */

typedef struct {
    char *name;
    int index;
} SystemColorEntry;

static SystemColorEntry sysColors[] = {
    { "3dDarkShadow",		SYSCLR_BUTTONDARK },
    { "3dLight",		SYSCLR_BUTTONLIGHT },
    { "ActiveBorder",		SYSCLR_ACTIVETITLETEXTBGND },
    { "ActiveCaption",		SYSCLR_ACTIVETITLE },
    { "AppWorkspace",		SYSCLR_APPWORKSPACE },
    { "Background",		SYSCLR_BACKGROUND },
    { "ButtonFace",		SYSCLR_BUTTONMIDDLE },
    { "ButtonHighlight",	SYSCLR_BUTTONLIGHT },
    { "ButtonShadow",		SYSCLR_BUTTONDARK },
    { "ButtonText",		SYSCLR_MENUTEXT },
    { "CaptionText",		SYSCLR_TITLETEXT },
    { "DisabledText",		SYSCLR_MENUDISABLEDTEXT },
    { "GrayText",		SYSCLR_MENUDISABLEDTEXT },
    { "Highlight",		SYSCLR_HILITEBACKGROUND },
    { "HighlightText",		SYSCLR_HILITEFOREGROUND },
    { "InactiveBorder",		SYSCLR_INACTIVEBORDER },
    { "InactiveCaption",	SYSCLR_INACTIVETITLE },
    { "InactiveCaptionText",	SYSCLR_INACTIVETITLETEXTBGND },
    { "InfoBackground",		SYSCLR_HILITEBACKGROUND },
    { "InfoText",		SYSCLR_HILITEFOREGROUND },
    { "Menu",			SYSCLR_MENU },
    { "MenuText",		SYSCLR_MENUTEXT },
    { "Scrollbar",		SYSCLR_SCROLLBAR },
    { "Window",			SYSCLR_WINDOW },
    { "WindowFrame",		SYSCLR_WINDOWFRAME },
    { "WindowText",		SYSCLR_WINDOWTEXT },
    { NULL,				0 }
};

#if 0
/* Number of colors that have been set */
typedef struct ThreadSpecificData {
    int initialized;			/* Set to 0 at allocation */
    int ncolors;		        /* Nr. of colors that have been set */
    LONG *logColorTable;		/* Table of colors that have been set */
    Tcl_HashTable logColorRefCounts;	/* Hash table for reference counts of
					 * logical color table entries */
    LONG nextColor;                     /* Next free index in color table */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;
#endif
Tcl_ThreadDataKey tkOS2ColorDataKey;

LONG nextColor = 0;

/*
 * Forward declarations for functions defined later in this file.
 */

static int            InitColorTable _ANSI_ARGS_((Display *display));
static void           FreeColorTable _ANSI_ARGS_((ClientData clientData));
static int            FindSystemColor _ANSI_ARGS_((const char *name,
                          XColor *colorPtr, int *indexPtr));



/*
 *----------------------------------------------------------------------
 *
 * InitColorTable --
 *
 *      This routine allocates space for the global color table, if
 *      necessary.
 *
 * Results:
 *      Returns non-zero on success.
 *
 * Side effects:
 *      Sets up an exit handler for freeing the allocated space.
 *
 *----------------------------------------------------------------------
 */

static int
InitColorTable(display)
    Display *display;
{
    int refCount;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&tkOS2ColorDataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("InitColorTable\n");
    fflush(stdout);
#endif

    if (tsdPtr->initialized == 0) {
        /* Determine necessary size for color table */
        tsdPtr->logColorTable = (LONG *)
	   ckalloc((DefaultScreenOfDisplay(display))->root_visual->map_entries);
        if (!tsdPtr->logColorTable) {
#ifdef VERBOSE
            printf("InitColorTable couldn't allocate logColorTable (%d)\n",
	           (DefaultScreenOfDisplay(display))->root_visual->map_entries);
            fflush(stdout);
#endif
            return 0;
        }
#ifdef VERBOSE
        printf("InitColorTable allocated logColorTable (%d entries)\n",
	       (DefaultScreenOfDisplay(display))->root_visual->map_entries);
            fflush(stdout);
#endif
        if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
            /*
             * Palette support
             */
            HPS screenPS = WinGetScreenPS(HWND_DESKTOP);

            /* First get number of entries */
            refCount = GpiQueryPaletteInfo(0L, screenPS, 0L, 0L, 0L,
                                           tsdPtr->logColorTable);
#ifdef VERBOSE
            printf("GpiQueryPaletteInfo says we need %d entries\n", refCount);
            fflush(stdout);
#endif
            if (refCount >
                  (DefaultScreenOfDisplay(display))->root_visual->map_entries) {
                refCount =
                    (DefaultScreenOfDisplay(display))->root_visual->map_entries;
            }
            if (GpiQueryPaletteInfo(0L, screenPS, 0L, 0L, refCount,
                                    tsdPtr->logColorTable) == PAL_ERROR) {
#ifdef VERBOSE
                printf("GpiQueryPaletteInfo PAL_ERROR: %x\n",
                       WinGetLastError(TclOS2GetHAB()));
                fflush(stdout);
#endif
                WinReleasePS(screenPS);
                return 0;
            }
            WinReleasePS(screenPS);
        } else {
            /*
             * By default, presentation spaces have a logical color table
             * consisting of the 16 default CLR_* values.
	     * nextColor contains the value of the next undefined entry.
             */
            rc = GpiQueryLogColorTable(globalPS, 0L, 0L, nextColor,
                                       tsdPtr->logColorTable);
#ifdef VERBOSE
            if (rc==QLCT_ERROR) {
                printf("Init: GpiQueryLogColorTable ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                if (rc==QLCT_RGB) {
                    printf("Init: GpiQueryLogColorTable in RGB mode\n");
                } else {
                    printf("Init: GpiQueryLogColorTable OK (%d elem)\n", rc);
                }
            }
            fflush(stdout);
#endif
            /* Recreate the color table in RGB mode */
	    rc = GpiCreateLogColorTable(globalPS, 0L, LCOLF_RGB, 0L,
	                                nextColor, tsdPtr->logColorTable);
#ifdef VERBOSE
            if (rc==FALSE) {
                printf("  GpiCreateLogColorTable ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf("  GpiCreateLogColorTable OK (%d elem)\n", nextColor);
            }
            fflush(stdout);
#endif
        }
        Tcl_CreateExitHandler(FreeColorTable, (ClientData) NULL);
	tsdPtr->initialized = 1;
	return 1;
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeColorTable --
 *
 *      This routine frees the memory space of the global color table,
 *      if necessary.
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
FreeColorTable(clientData)
    ClientData clientData;	/* not used */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&tkOS2ColorDataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("FreeColorTable\n");
    fflush(stdout);
#endif
    if (tsdPtr->initialized != 0) {
        Tcl_DeleteExitHandler(FreeColorTable, (ClientData) NULL);
	ckfree((char *)tsdPtr->logColorTable);
	tsdPtr->initialized = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FindSystemColor --
 *
 *      This routine finds the color entry that corresponds to the
 *      specified color.
 *
 * Results:
 *      Returns non-zero on success.  The RGB values of the XColor
 *      will be initialized to the proper values on success.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
FindSystemColor(name, colorPtr, indexPtr)
    const char *name;           /* Color name. */
    XColor *colorPtr;           /* Where to store results. */
    int *indexPtr;              /* Out parameter to store color index. */
{
    int l, u, r, i;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&tkOS2ColorDataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("FindSystemColor\n");
    fflush(stdout);
#endif

    /*
     * Count the number of elements in the color array if we haven't
     * done so yet.
     */

    if (tsdPtr->ncolors == 0) {
        SystemColorEntry *ePtr;

        for (ePtr = sysColors; ePtr->name != NULL; ePtr++) {
            tsdPtr->ncolors++;
        }
    }

    /*
     * Perform a binary search on the sorted array of colors.
     */

    l = 0;
    u = tsdPtr->ncolors - 1;
    while (l <= u) {
        i = (l + u) / 2;
        r = strcasecmp(name, sysColors[i].name);
        if (r == 0) {
            break;
        } else if (r < 0) {
            u = i-1;
        } else {
            l = i+1;
        }
    }
    if (l > u) {
        return 0;
    }
    *indexPtr = sysColors[i].index;
    colorPtr->pixel = WinQuerySysColor(HWND_DESKTOP, sysColors[i].index, 0);
    /*
     * x257 is (value<<8 + value) to get the properly bit shifted
     * and padded value.  [Bug: 4919]
     */
    colorPtr->red = GetRValue(colorPtr->pixel) * 257;
    colorPtr->green = GetGValue(colorPtr->pixel) * 257;
    colorPtr->blue = GetBValue(colorPtr->pixel) * 257;
#ifdef VERBOSE
    printf("    SystemColor %s %d (%x): %x (%d,%d,%d)\n", sysColors[i].name,
           sysColors[i].index, sysColors[i].index, colorPtr->pixel,
           colorPtr->red, colorPtr->green, colorPtr->blue);
            fflush(stdout);
#endif
    colorPtr->flags = DoRed|DoGreen|DoBlue;
    colorPtr->pad = 0;
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColor --
 *
 *      Allocate a new TkColor for the color with the given name.
 *
 * Results:
 *      Returns a newly allocated TkColor, or NULL on failure.
 *
 * Side effects:
 *      May invalidate the colormap cache associated with tkwin upon
 *      allocating a new colormap entry.  Allocates a new TkColor
 *      structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColor(tkwin, name)
    Tk_Window tkwin;            /* Window in which color will be used. */
    Tk_Uid name;                /* Name of color to allocated (in form
                                 * suitable for passing to XParseColor). */
{
    OS2Color *os2ColPtr;
    XColor color;
    int index = -1;             /* -1 indicates that this is not an indirect
                                 * sytem color. */

#ifdef VERBOSE
    printf("TkpGetColor tkwin %x name %s\n", tkwin, name);
            fflush(stdout);
#endif

    /*
     * Check to see if it is a system color or an X color string.  If the
     * color is found, allocate a new OS2Color and store the XColor and the
     * system color index.
     */

    if (((strncasecmp(name, "system", 6) == 0)
            && FindSystemColor(name+6, &color, &index))
            || XParseColor(Tk_Display(tkwin), Tk_Colormap(tkwin), name,
                    &color)) {
        os2ColPtr = (OS2Color *) ckalloc(sizeof(OS2Color));
        if (os2ColPtr == (OS2Color *)NULL) {
            return (TkColor *) NULL;
        }
        os2ColPtr->info.color = color;
        os2ColPtr->index = index;

        if (index != -1) {
#ifdef VERBOSE
            printf("TkpGetColor systemcolor %d\n", index);
            fflush(stdout);
#endif
            (&os2ColPtr->info.color)->pixel = WinQuerySysColor(HWND_DESKTOP,
                                                               index, 0L);
        }
        XAllocColor(Tk_Display(tkwin), Tk_Colormap(tkwin),
                    &os2ColPtr->info.color);
#ifdef VERBOSE
        printf("TkpGetColor returns %x\n", (&os2ColPtr->info.color)->pixel);
        fflush(stdout);
#endif
        return (TkColor *) os2ColPtr;
    }
#ifdef VERBOSE
    printf("TkpGetColor returns NULL\n");
    fflush(stdout);
#endif
    return (TkColor *) NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColorByValue --
 *
 *      Given a desired set of red-green-blue intensities for a color,
 *      locate a pixel value to use to draw that color in a given
 *      window.
 *
 * Results:
 *      The return value is a pointer to an TkColor structure that
 *      indicates the closest red, blue, and green intensities available
 *      to those specified in colorPtr, and also specifies a pixel
 *      value to use to draw in that color.
 *
 * Side effects:
 *      May invalidate the colormap cache for the specified window.
 *      Allocates a new TkColor structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColorByValue(tkwin, colorPtr)
    Tk_Window tkwin;            /* Window in which color will be used. */
    XColor *colorPtr;           /* Red, green, and blue fields indicate
                                 * desired color. */
{
    OS2Color *tkColPtr = (OS2Color *) ckalloc(sizeof(OS2Color));
#ifdef VERBOSE
    printf("TkpGetColorByValue\n");
    fflush(stdout);
#endif
    if (tkColPtr == NULL) return (TkColor *) NULL;

    tkColPtr->info.color.red = colorPtr->red;
    tkColPtr->info.color.green = colorPtr->green;
    tkColPtr->info.color.blue = colorPtr->blue;
    tkColPtr->info.color.pixel = 0;
    tkColPtr->index = -1;
    XAllocColor(Tk_Display(tkwin), Tk_Colormap(tkwin), &tkColPtr->info.color);
    return (TkColor *) tkColPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeColor --
 *
 *      Release the specified color back to the system.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Invalidates the colormap cache for the colormap associated with
 *      the given color.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeColor(tkColPtr)
    TkColor *tkColPtr;          /* Color to be released.  Must have been
                                 * allocated by TkpGetColor or
                                 * TkpGetColorByValue. */
{
    Screen *screen = tkColPtr->screen;
#ifdef VERBOSE
    printf("TkpFreeColor\n");
    fflush(stdout);
#endif

    XFreeColors(DisplayOfScreen(screen), tkColPtr->colormap,
            &tkColPtr->color.pixel, 1, 0L);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2IndexOfColor --
 *
 *      Given a color, return the system color index that was used
 *      to create the color.
 *
 * Results:
 *      If the color was allocated using a system indirect color name,
 *      then the corresponding WinQuerySysColor() index is returned.
 *      Otherwise, -1 is returned.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TkOS2IndexOfColor(colorPtr)
    XColor *colorPtr;
{
    register OS2Color *os2ColPtr = (OS2Color *) colorPtr;
#ifdef VERBOSE
    printf("TkOS2IndexOfColor\n");
    fflush(stdout);
#endif
    if (os2ColPtr->info.magic == COLOR_MAGIC) {
        return os2ColPtr->index;
    }
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * XAllocColor --
 *
 *	Find the closest available color to the specified XColor.
 *
 * Results:
 *	Updates the color argument and returns 1 on success.  Otherwise
 *	returns 0.
 *
 * Side effects:
 *	Allocates a new color in the palette.
 *
 *----------------------------------------------------------------------
 */

int
XAllocColor(display, colormap, color)
    Display* display;
    Colormap colormap;
    XColor* color;
{
    TkOS2Colormap *cmap = (TkOS2Colormap *) colormap;
    int new, refCount;
    Tcl_HashEntry *entryPtr;
    RGB entry;
    HPAL oldPal;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&tkOS2ColorDataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("XAllocColor\n");
    fflush(stdout);
#endif

    if (tsdPtr->initialized == 0) {
        InitColorTable(display);
    }
    color->pixel &= 0xffffff;

    /* We lose significance when converting to PM, 256 values per color */
    entry.bRed = (color->red) >> 8;
    entry.bGreen = (color->green) >> 8;
    entry.bBlue = (color->blue) >> 8;

#ifdef VERBOSE
    printf("XAllocColor %d %d %d (PM: %d %d %d) pixel %x cmap %x\n", color->red,
           color->green, color->blue, entry.bRed, entry.bGreen, entry.bBlue,
           color->pixel, cmap);
            fflush(stdout);
#endif

    if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
	/*
	 * Palette support
	 */
	ULONG newPixel;
        ULONG *palInfo;
        HPS hps;
        ULONG index, i;

        hps = WinGetScreenPS(HWND_DESKTOP);

#ifdef VERBOSE
        printf("    Palette Manager; cmap->size %d\n", cmap->size);
            fflush(stdout);
#endif

	/*
	 * Find the nearest existing palette entry.
	 */
	
	newPixel = RGB(entry.bRed, entry.bGreen, entry.bBlue);
	oldPal= GpiSelectPalette(hps, cmap->palette);
	if (oldPal == PAL_ERROR) {
#ifdef VERBOSE
            printf("GpiSelectPalette PAL_ERROR: %x\n",
                   WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
#endif
            WinReleasePS(hps);
            return 0;
	}
        palInfo= (ULONG *) ckalloc(sizeof(ULONG) * (cmap->size+1));

	if (GpiQueryPaletteInfo(cmap->palette, hps, 0L, 0L, cmap->size,
	                        palInfo) == PAL_ERROR) {
#ifdef VERBOSE
            printf("GpiQueryPaletteInfo size %d PAL_ERROR: %x\n", cmap->size,
                   WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
#endif
	    GpiSelectPalette(hps, oldPal);
            WinReleasePS(hps);
            ckfree((char *) palInfo);
            return 0;
	}

        /*
         * If this is not a duplicate, allocate a new entry.
         */

        index = -1;
        for (i=0; i<cmap->size; i++) {
#ifdef VERBOSE
            printf("    comparing palInfo[%d] (%x) to %x\n", i, palInfo[i],
                   newPixel);
            fflush(stdout);
#endif
            if (palInfo[i] == newPixel) {
                index = i;
                break;
            }
        }

        if (index == -1) {
#ifdef VERBOSE
            printf("    color not found in existing palette\n");
            fflush(stdout);
#endif

            /*
             * Fails if the palette is full.
             */
            if (cmap->size == aDevCaps[CAPS_COLOR_INDEX]) {
#ifdef VERBOSE
                printf("    no more entries in palette (%d)\n", cmap->size);
                fflush(stdout);
#endif
                GpiSelectPalette(hps, oldPal);
                WinReleasePS(hps);
                ckfree((char *) palInfo);
                return 0;
            }

index = cmap->size;
            cmap->size++;
            palInfo[cmap->size-1]= newPixel;
#ifdef VERBOSE
            printf("    adding pixel %x at %d\n", newPixel, cmap->size-1);
            fflush(stdout);
#endif
            GpiSetPaletteEntries(cmap->palette, LCOLF_CONSECRGB, 0L, cmap->size,
                                 palInfo);
        }

        ckfree((char *) palInfo);

        /*
         * Assign the _index_ in the palette as the pixel, for later use in
         * GpiSetColor et al. ()
         */
        color->pixel = index;
#ifdef VERBOSE
        printf("Using index %d (0x%x) as color->pixel\n", index, index);
        fflush(stdout);
#endif
	entryPtr = Tcl_CreateHashEntry(&cmap->refCounts,
		(char *)color->pixel, &new);
	if (new) {
#ifdef VERBOSE
            printf("Created new HashEntry: %d\n", color->pixel);
            fflush(stdout);
#endif
	    refCount = 1;
	} else {
	    refCount = ((int) Tcl_GetHashValue(entryPtr)) + 1;
#ifdef VERBOSE
            printf("Incremented HashEntry %d to %d\n", color->pixel, refCount);
            fflush(stdout);
#endif
	}
	Tcl_SetHashValue(entryPtr, (ClientData)refCount);

	WinReleasePS(hps);

    } else {
       LONG i, index = GPI_ALTERROR;

        color->pixel = GpiQueryNearestColor(globalPS, 0L,
                                RGB(entry.bRed, entry.bGreen, entry.bBlue));
/*
        color->pixel = RGB(entry.bRed, entry.bGreen, entry.bBlue);
*/
#ifdef VERBOSE
       printf("    no Palette Mgr, nr.colors %d (%s), nearest %x\n",
              aDevCaps[CAPS_COLORS],
              aDevCaps[CAPS_COLOR_TABLE_SUPPORT] ? "loadable color table" :
              "no loadable color table", color->pixel);
            fflush(stdout);
#endif
        color->red = (GetRValue(color->pixel) << 8);
        color->green = (GetGValue(color->pixel) << 8);
        color->blue = (GetBValue(color->pixel) << 8);

	/* Loadable color table support? */
	if (aDevCaps[CAPS_COLOR_TABLE_SUPPORT]) {
	    /* Color table support */

	    /*
             * See if this color is already in the color table.
             */
            for (i=0; i<cmap->size; i++) {
#ifdef VERBOSE
                printf("    comparing logColorTable[%d] (%x) to %x\n", i,
                       tsdPtr->logColorTable[i], color->pixel);
                fflush(stdout);
#endif
                if (tsdPtr->logColorTable[i] == color->pixel) {
                    index = i;
                    break;
                }
            }
            
	    /*
	     * If the color isn't in the table yet and loadable color table
	     * support, add this color to the table, else just use what's
	     * available.
	     */
#ifdef VERBOSE
            printf("   index %d\n", index);
            printf("   nextColor (%d) <= aDevCaps[CAPS_COLOR_INDEX] (%d): %s\n",
	           nextColor, aDevCaps[CAPS_COLOR_INDEX],
	           nextColor <= aDevCaps[CAPS_COLOR_INDEX] ? "TRUE" : "FALSE");
            fflush(stdout);
#endif
	    if (index == GPI_ALTERROR) {
	        if (nextColor > aDevCaps[CAPS_COLOR_INDEX]) {
                    return 0;
                }
                index = nextColor;

	        rc = GpiCreateLogColorTable(globalPS, 0L, LCOLF_RGB,
	                                    nextColor, 1, &color->pixel);
                if (rc==TRUE) {
#ifdef VERBOSE
                    printf("    GpiCreateLogColorTable %x at %d OK\n",
		           color->pixel, nextColor);
                    fflush(stdout);
#endif
                    nextColor++;
		    /* Update "cache" color table */
                    rc = GpiQueryLogColorTable(globalPS, 0L, 0L, nextColor,
		                               tsdPtr->logColorTable);
                }
	    }

	    entryPtr = Tcl_CreateHashEntry(&cmap->refCounts,
		    (char *)color->pixel, &new);
	    if (new) {
#ifdef VERBOSE
                printf("Created new HashEntry %d (0x%x) in %x\n", color->pixel,
                       color->pixel, &cmap->refCounts);
                fflush(stdout);
#endif
	        refCount = 1;
	    } else {
	        refCount = ((int) Tcl_GetHashValue(entryPtr)) + 1;
#ifdef VERBOSE
                printf("Incremented HashEntry %d (0x%x) to %d in %x\n",
                       color->pixel, color->pixel, refCount, &cmap->refCounts);
                fflush(stdout);
#endif
	    }
	    Tcl_SetHashValue(entryPtr, (ClientData)refCount);
        } else {
	
	    /*
	     * Determine what color will actually be used on non-colormap
	     * systems.
	     */

	    color->pixel = GpiQueryNearestColor(globalPS, 0L,
		    RGB(entry.bRed, entry.bGreen, entry.bBlue));
	    color->red = (GetRValue(color->pixel) * 257);
	    color->green = (GetGValue(color->pixel) * 257);
	    color->blue = (GetBValue(color->pixel) * 257);
#ifdef VERBOSE
            if (color->pixel==GPI_ALTERROR) {
                printf("GpiQueryNearestColor ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
            } else {
                printf(" Using nearest color %x (%d,%d,%d) for %x (%d,%d,%d)\n",
                       color->pixel, GetRValue(color->pixel),
                       GetGValue(color->pixel), GetBValue(color->pixel),
                       RGB(entry.bRed, entry.bGreen, entry.bBlue),
                       color->red, color->green, color->blue);
            }
            fflush(stdout);
#endif
            color->pixel = index;
        }
    }
#ifdef VERBOSE
    printf("color->pixel now %d/%x (%s)\n", color->pixel, color->pixel,
           color->pixel == CLR_BACKGROUND ? "CLR_BACKGROUND" :
           (color->pixel == CLR_BLUE ? "CLR_BLUE" :
           (color->pixel == CLR_RED ? "CLR_RED" :
           (color->pixel == CLR_PINK ? "CLR_PINK" :
           (color->pixel == CLR_GREEN ? "CLR_GREEN" :
           (color->pixel == CLR_CYAN ? "CLR_CYAN" :
           (color->pixel == CLR_YELLOW ? "CLR_YELLOW" :
           (color->pixel == CLR_NEUTRAL ? "CLR_NEUTRAL" :
           (color->pixel == CLR_DARKGRAY ? "CLR_DARKGRAY" :
           (color->pixel == CLR_DARKBLUE ? "CLR_DARKBLUE" :
           (color->pixel == CLR_DARKRED ? "CLR_DARKRED" :
           (color->pixel == CLR_DARKPINK ? "CLR_DARKPINK" :
           (color->pixel == CLR_DARKGREEN ? "CLR_DARKGREEN" :
           (color->pixel == CLR_DARKCYAN ? "CLR_DARKCYAN" :
           (color->pixel == CLR_BROWN ? "CLR_BROWN" :
           (color->pixel == CLR_PALEGRAY ? "CLR_PALEGRAY" : "UNKNOWN"
	   ))))))))))))))));
            fflush(stdout);
#endif

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeColors --
 *
 *	Deallocate a block of colors.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes entries for the current palette and compacts the
 *	remaining set.
 *
 *----------------------------------------------------------------------
 */

void
XFreeColors(display, colormap, pixels, npixels, planes)
    Display* display;
    Colormap colormap;
    unsigned long* pixels;
    int npixels;
    unsigned long planes;
{
    TkOS2Colormap *cmap = (TkOS2Colormap *) colormap;
    ULONG delColor;
    ULONG refCount;
    int i, old, new;
    ULONG *entries;
    Tcl_HashEntry *entryPtr;

#ifdef VERBOSE
    printf("XFreeColors\n");
    fflush(stdout);
#endif

    /*
     * This is really slow for large values of npixels.
     */
    for (i = 0; i < npixels; i++) {
#ifdef VERBOSE
        printf("    pixel %d: %x\n", i, pixels[i]);
            fflush(stdout);
#endif
        entryPtr = Tcl_FindHashEntry(&cmap->refCounts, (char *) pixels[i]);
        if (!entryPtr) {
#ifdef VERBOSE
            printf("    panicking...\n");
            fflush(stdout);
#endif
            panic("Tried to free a color that isn't allocated.");
        }
        refCount = (int) Tcl_GetHashValue(entryPtr) - 1;
        if (refCount > 0) {
            Tcl_SetHashValue(entryPtr, (ClientData)refCount);
#ifdef VERBOSE
            printf("    decremented HashEntry %d to %d\n", pixels[i], refCount);
            fflush(stdout);
#endif
	    continue;
	}
#ifdef VERBOSE
        printf("    refCount 0\n");
            fflush(stdout);
#endif
        delColor = pixels[i] & 0x00ffffff;
        entries = (ULONG *) ckalloc(sizeof(ULONG) * cmap->size);
        if (!entries) {
            return;
        }

        if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
            /* hps value ignored for specific values of palette */
            if (GpiQueryPaletteInfo(cmap->palette, NULLHANDLE, 0L, 0L,
                                    cmap->size, entries) == PAL_ERROR) {
               ckfree((char *)entries);
               return;
            }
        } else {
            if (GpiQueryLogColorTable(globalPS, 0L, 0L, cmap->size, entries)
                <= 0) {
               ckfree((char *)entries);
               return;
            }
        }

        /* Copy all entries except the one to delete */
        for (old= new= 0; old<cmap->size; old++) {
            if (old != delColor) {
#ifdef VERBOSE
                printf("    copying %d\n", entries[old]);
            fflush(stdout);
#endif
                entries[new] = entries[old];
                new++;
            }
        }
	cmap->size--;
        if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
            GpiSetPaletteEntries(cmap->palette, LCOLF_CONSECRGB, 0, cmap->size,
	                         entries);
        } else {
            GpiCreateLogColorTable(globalPS, 0L, LCOLF_RGB, 0, cmap->size,
                                   entries);
        }
        ckfree((char *) entries);
        Tcl_DeleteHashEntry(entryPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateColormap --
 *
 *	Allocate a new colormap.
 *
 * Results:
 *	Returns a newly allocated colormap.
 *
 * Side effects:
 *	Allocates an empty palette and color list.
 *
 *----------------------------------------------------------------------
 */

Colormap
XCreateColormap(display, w, visual, alloc)
    Display* display;
    Window w;
    Visual* visual;
    int alloc;
{
    TkOS2Colormap *cmap = (TkOS2Colormap *) ckalloc(sizeof(TkOS2Colormap));
    ULONG *entryPtr;
    Tcl_HashEntry *hashPtr;
    ULONG logPalette[256];
    ULONG lRetCount = PAL_ERROR;
    ULONG i;
    int new = 1;

#ifdef VERBOSE
    printf("XCreateColormap (%d colors),
    visual id %d class %d, bits %d, map entries %d, masks R%d G%d B%d\n",
           aDevCaps[CAPS_COLOR_INDEX]+1, visual->visualid, visual->class,
           visual->bits_per_rgb, visual->map_entries, visual->red_mask,
           visual->green_mask, visual->blue_mask);
    printf("    CAPS_COLORS %d\n", aDevCaps[CAPS_COLORS]);
    fflush(stdout);
#endif

    /*
     * Create a palette when we have palette management, with default system
     * entries.
     * Otherwise store the presentation space handle of the window, since color
     * tables are PS-specific.
     */

    if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {

	lRetCount = GpiQueryPaletteInfo(NULLHANDLE, globalPS, 0L, 0L, 256L,
	                                logPalette);
#ifdef VERBOSE
        if (lRetCount == PAL_ERROR) {
            printf("    GpiQueryPaletteInfo PAL_ERROR %x\n",
	           WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
            ckfree((char *)cmap);
	    return (Colormap)NULL;
        } else {
            printf("    GpiQueryPaletteInfo: %x\n", cmap->palette);
            fflush(stdout);
        }
#endif

        cmap->palette = GpiCreatePalette(TclOS2GetHAB(), 0L, LCOLF_CONSECRGB,
                                         lRetCount, logPalette);
#ifdef VERBOSE
        if (cmap->palette == GPI_ERROR) {
            printf("    GpiCreatePalette GPI_ERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
            ckfree((char *)cmap);
	    return (Colormap)NULL;
        } else {
            printf("    GpiCreatePalette: %x\n", cmap->palette);
            fflush(stdout);
        }
#endif
    } else {
        cmap->palette = (HPAL)NULLHANDLE;
    }

    cmap->size = 16;
    cmap->stale = 0;
    Tcl_InitHashTable(&cmap->refCounts, TCL_ONE_WORD_KEYS);

    if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
        /* Add hash entries for each of the static colors. */
        for (i = 0; i < lRetCount; i++) {
            entryPtr = logPalette + i;
            hashPtr = Tcl_CreateHashEntry(&cmap->refCounts,
	        (char*) *entryPtr, &new);
            Tcl_SetHashValue(hashPtr, (ClientData)1);
        }
    }

    return (Colormap)cmap;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeColormap --
 *
 *	Frees the resources associated with the given colormap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes the palette associated with the colormap.  Note that
 *	the palette must not be selected into a device context when
 *	this occurs.
 *
 *----------------------------------------------------------------------
 */

void
XFreeColormap(display, colormap)
    Display* display;
    Colormap colormap;
{
    TkOS2Colormap *cmap = (TkOS2Colormap *) colormap;

#ifdef VERBOSE
    printf("XFreeColormap\n");
    fflush(stdout);
#endif

    if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
        /* Palette management */
        if (!GpiDeletePalette(cmap->palette)) {
            /* Try to free memory anyway */
            ckfree((char *) cmap);
	    panic("Unable to free colormap, palette is still selected.");
        }
    }
    Tcl_DeleteHashTable(&cmap->refCounts);
    ckfree((char *) cmap);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2SelectPalette --
 *
 *	This function sets up the specified device context with a
 *	given palette.  If the palette is stale, it realizes it in
 *	the background unless the palette is the current global
 *	palette.
 *
 * Results:
 *	Returns the previous palette selected into the device context.
 *
 * Side effects:
 *	May change the system palette.
 *
 *----------------------------------------------------------------------
 */

HPAL
TkOS2SelectPalette(hps, hwnd, colormap)
    HPS hps;
    HWND hwnd;
    Colormap colormap;
{
    TkOS2Colormap *cmap = (TkOS2Colormap *) colormap;
    HPAL oldPalette;
    ULONG mapped, changed;

#ifdef VERBOSE
    printf("TkOS2SelectPalette (cmap %x, palette %x), nextColor %d\n", cmap,
            cmap != 0 ? cmap->palette : 0, nextColor);
    fflush(stdout);
#endif

    if (aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) {
        oldPalette = GpiSelectPalette(hps, cmap->palette);
#ifdef VERBOSE
        if (oldPalette == PAL_ERROR) {
            printf("GpiSelectPalette PAL_ERROR: %x\n",
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSelectPalette: %x\n", oldPalette);
        }
            fflush(stdout);
#endif
        mapped = WinRealizePalette(hwnd, hps, &changed);
#ifdef VERBOSE
        if (mapped == PAL_ERROR) {
            printf("WinRealizePalette PAL_ERROR: %x\n",
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("WinRealizePalette: %x\n", mapped);
        }
            fflush(stdout);
#endif
/*
*/
        return oldPalette;
    } else {
        ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
                Tcl_GetThreadData(&tkOS2ColorDataKey, sizeof(ThreadSpecificData));

        /* Retrieve the "global" color table and create it in this PS */
        rc = GpiCreateLogColorTable(hps, 0L, LCOLF_RGB, 0, nextColor,
                                    tsdPtr->logColorTable);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("    GpiCreateLogColorTable (%d entries) ERROR %x\n",
                   nextColor, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("    GpiCreateLogColorTable (%d entries) OK\n", nextColor);
        }
            fflush(stdout);
#endif
        return (HPAL)0;
    }
}

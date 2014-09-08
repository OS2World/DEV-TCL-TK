/* 
 * tkOS2Init.c --
 *
 *	This file contains OS/2-specific interpreter initialization
 *	functions.
 *
 * Copyright (c) 1996-2000 Illya Vaes
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkOS2Int.h"

/*
 * Read-only global variables necessary in modules in the DLL
 */
HAB		tkHab;			/* Anchor block (instance handle) */
HMQ		tkHmq;			/* Handle to message queue */
LONG		aDevCaps[CAPS_DEVICE_POLYSET_POINTS+1]; /* Device Capabilities*/
PFNWP		oldFrameProc = WinDefWindowProc;/* subclassed frame procedure */
LONG		xScreen;		/* System Value Screen width */
LONG		yScreen;		/* System Value Screen height */
LONG		titleBar;		/* System Value Title Bar */
LONG		xBorder;		/* System Value X nominal border */
LONG		yBorder;		/* System Value Y nominal border */
LONG		xSizeBorder;		/* System Value X Sizing border */
LONG		ySizeBorder;		/* System Value Y Sizing border */
LONG		xDlgBorder;		/* System Value X dialog-frame border */
LONG		yDlgBorder;		/* System Value Y dialog-frame border */
HDC		hScreenDC;		/* Device Context for screen */
HPS		globalPS = NULLHANDLE;  /* Global PS for fonts */
TkOS2Font       logfonts[255];		/* List of logical fonts */
LONG            nextLogicalFont = 5;	/* Next free Font ID */
HBITMAP         globalBitmap;           /* Bitmap for global PS */
#ifdef IGNOREPMRES
    LONG        overrideResolution= 72; /* If IGNOREPMRES is defined */
#endif
LONG		rc;			/* For checking return values */

/*
 * The Init script (common to OS/2, Windows and Unix platforms) is
 * defined in tkInitScript.h
 */

#include "tkInitScript.h"


/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Performs OS/2-specific interpreter initialization related to the
 *      tk_library variable.
 *
 * Results:
 *	A standard Tcl completion code (TCL_OK or TCL_ERROR).  Also
 *      leaves information in interp->result.
 *
 * Side effects:
 *	Sets "tk_library" Tcl variable, runs "tk.tcl" script.
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(interp)
    Tcl_Interp *interp;
{
    return Tcl_Eval(interp, initScript);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetAppName --
 *
 *      Retrieves the name of the current application from a platform
 *      specific location.  For OS/2, the application name is the
 *      root of the tail of the path contained in the tcl variable argv0.
 *
 * Results:
 *      Returns the application name in the given Tcl_DString.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(interp, namePtr)
    Tcl_Interp *interp;
    Tcl_DString *namePtr;       /* A previously initialized Tcl_DString. */
{
    int argc;
    char **argv = NULL, *name, *p;

    name = Tcl_GetVar(interp, "argv0", TCL_GLOBAL_ONLY);
    if (name != NULL) {
        Tcl_SplitPath(name, &argc, &argv);
        if (argc > 0) {
            name = argv[argc-1];
            p = strrchr(name, '.');
            if (p != NULL) {
                *p = '\0';
            }
        } else {
            name = NULL;
        }
    }
    if ((name == NULL) || (*name == 0)) {
        name = "tk";
    }
    Tcl_DStringAppend(namePtr, name, -1);
    if (argv != NULL) {
        ckfree((char *)argv);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *      This routines is called from Tk_Main to display warning
 *      messages that occur during startup.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Displays a message box.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayWarning(msg, title)
    char *msg;                  /* Message to be displayed. */
    char *title;                /* Title of warning. */
{
#ifdef VERBOSE
    printf("WARNING \"%s\": %s\n", title, msg);
    fflush(stdout);
#endif
    WinMessageBox(HWND_DESKTOP, NULLHANDLE, msg, title, 0L,
                  MB_OK | MB_ICONEXCLAMATION | MB_APPLMODAL | MB_MOVEABLE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2InitPM --
 *
 *	Performs OS/2 Presentation Manager intialisation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills the global variables tkHab and tkHmq.
 *
 *----------------------------------------------------------------------
 */

HAB
TkOS2InitPM (void)
{
    BOOL rc;
    LONG lStart, lCount;
    DEVOPENSTRUC doStruc= {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
    SIZEL sizel = {0,0};
    BITMAPINFOHEADER2 bmpInfo;
#ifdef IGNOREPMRES
    char *tkPmPixRes;
#endif
#ifdef VERBOSE
    LONG *aBitmapFormats;
#endif
    PPIB pibPtr;
    PTIB tibPtr;
    FATTRS fattrs;
    FONTMETRICS fontMetrics;
    LONG match;

    /*
     * Warp ourselves to PM; only of interest for CLI that really want
     * to use PM services etc. and using the Tk DLL.
     */
    rc = DosGetInfoBlocks(&tibPtr, &pibPtr);
    pibPtr->pib_ultype = 3;
    /* Initialize PM */
    TclOS2SetUsePm(1);
    if (!TclOS2PMInitialize()) {
        return NULLHANDLE;
    }
    tkHab = TclOS2GetHAB();
    if (tkHab == NULLHANDLE) {
        return NULLHANDLE;
    }
    tkHmq = TclOS2GetHMQ(tkHab);
    if (tkHmq == NULLHANDLE) {
        return NULLHANDLE;
    }
#ifdef VERBOSE
    printf("tkHab %x, tkHmq %x\n", tkHab, tkHmq);
#endif

    /* Determine system values */
    xScreen = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN);
    yScreen = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    titleBar = WinQuerySysValue(HWND_DESKTOP, SV_CYTITLEBAR);
    xBorder = WinQuerySysValue(HWND_DESKTOP, SV_CXBORDER);
    yBorder = WinQuerySysValue(HWND_DESKTOP, SV_CYBORDER);
    xSizeBorder = WinQuerySysValue(HWND_DESKTOP, SV_CXSIZEBORDER);
    ySizeBorder = WinQuerySysValue(HWND_DESKTOP, SV_CYSIZEBORDER);
    xDlgBorder = WinQuerySysValue(HWND_DESKTOP, SV_CXDLGFRAME);
    yDlgBorder = WinQuerySysValue(HWND_DESKTOP, SV_CYDLGFRAME);
#ifdef VERBOSE
    printf("xScreen %d, yScreen %d, titleBar %d, xBorder %d, yBorder %d,
           xSizeBorder %d, ySizeBorder %d, xDlgBorder %d, yDlgBorder %d\n",
           xScreen, yScreen, titleBar, xBorder, yBorder, xSizeBorder,
           ySizeBorder, xDlgBorder, yDlgBorder);
#endif

    /* Get device characteristics from PM */
    hScreenDC= DevOpenDC(tkHab, OD_MEMORY, (PSZ)"*", 0, (PDEVOPENDATA)&doStruc,
                         NULLHANDLE);
    lStart= CAPS_FAMILY; lCount= CAPS_DEVICE_POLYSET_POINTS+1;
    rc= DevQueryCaps(hScreenDC, lStart, lCount, aDevCaps);
#ifdef IGNOREPMRES
    s = getenv("TK_PM_PIXRES");
    if (s) {
        overrideResolution = atol(s);
    }
#endif
    globalPS = GpiCreatePS(tkHab, hScreenDC, &sizel, PU_PELS | GPIA_ASSOC);
#ifdef VERBOSE
    printf("globalPS %x\n", globalPS);
    printf("%d bitmap formats: ", aDevCaps[CAPS_BITMAP_FORMATS]);
    fflush(stdout);
    aBitmapFormats = (PLONG) ckalloc(2 * aDevCaps[CAPS_BITMAP_FORMATS]
                                     * sizeof(LONG));
    if (aBitmapFormats != NULL) {
        rc = GpiQueryDeviceBitmapFormats(globalPS,
                                         2 * aDevCaps[CAPS_BITMAP_FORMATS],
                                         aBitmapFormats);
        if (rc == TRUE) {
            for (lCount=0; lCount < 2*aDevCaps[CAPS_BITMAP_FORMATS]; lCount++) {
                printf("(%d,", aBitmapFormats[lCount]);
                lCount++;
                printf("%d) ", aBitmapFormats[lCount]);
            }
        } else {
            printf("\nGpiQueryDeviceBitmapFormats ERROR %d\n", rc);
            fflush(stdout);
        } 
        ckfree((char *)aBitmapFormats);
    } else {
        printf("\naBitmapFormats NULL\n");
        fflush(stdout);
    }
    printf("\n");
    fflush(stdout);
    printf("  CAPS_FAMILY %x, CAPS_IO_CAPS %x, CAPS_TECHNOLOGY %x\n",
           aDevCaps[CAPS_FAMILY], aDevCaps[CAPS_IO_CAPS],
           aDevCaps[CAPS_TECHNOLOGY]);
    printf("  CAPS_DRIVER_VERSION %x, CAPS_WIDTH %d, CAPS_HEIGHT %d\n",
           aDevCaps[CAPS_DRIVER_VERSION], aDevCaps[CAPS_WIDTH],
           aDevCaps[CAPS_HEIGHT]);
    printf("  CAPS_WIDTH_IN_CHARS %d, CAPS_HEIGHT_IN_CHARS %d\n",
           aDevCaps[CAPS_WIDTH_IN_CHARS], aDevCaps[CAPS_HEIGHT_IN_CHARS]);
    printf("  CAPS_HORIZONTAL_RESOLUTION %d, CAPS_VERTICAL_RESOLUTION %d\n",
           aDevCaps[CAPS_HORIZONTAL_RESOLUTION],
           aDevCaps[CAPS_VERTICAL_RESOLUTION]);
    printf("  => (hor) 1cm = %d pixels, 1in = %d pixels\n",
           aDevCaps[CAPS_HORIZONTAL_RESOLUTION] / 100,
           aDevCaps[CAPS_HORIZONTAL_RESOLUTION] / 39);
    printf("  CAPS_CHAR_WIDTH %d, CAPS_CHAR_HEIGHT %d\n",
           aDevCaps[CAPS_CHAR_WIDTH], aDevCaps[CAPS_CHAR_HEIGHT]);
    printf("  CAPS_SMALL_CHAR_WIDTH %d, CAPS_SMALL_CHAR_HEIGHT %d\n",
           aDevCaps[CAPS_SMALL_CHAR_WIDTH], aDevCaps[CAPS_SMALL_CHAR_HEIGHT]);
    printf("  CAPS_COLORS %d, CAPS_COLOR_PLANES %d, CAPS_COLOR_BITCOUNT %d\n",
           aDevCaps[CAPS_COLORS], aDevCaps[CAPS_COLOR_PLANES],
           aDevCaps[CAPS_COLOR_BITCOUNT]);
    printf("  CAPS_COLOR_TABLE_SUPPORT %x, CAPS_MOUSE_BUTTONS %d\n",
           aDevCaps[CAPS_COLOR_TABLE_SUPPORT], aDevCaps[CAPS_MOUSE_BUTTONS]);
    printf("  CAPS_FOREGROUND_MIX_SUPPORT %x, CAPS_BACKGROUND_MIX_SUPPORT %x\n",
           aDevCaps[CAPS_FOREGROUND_MIX_SUPPORT],
	   aDevCaps[CAPS_BACKGROUND_MIX_SUPPORT]);
    printf("  CAPS_VIO_LOADABLE_FONTS %d, CAPS_WINDOW_BYTE_ALIGNMENT %x\n",
           aDevCaps[CAPS_VIO_LOADABLE_FONTS],
           aDevCaps[CAPS_WINDOW_BYTE_ALIGNMENT]);
    printf("  CAPS_BITMAP_FORMATS %d, CAPS_RASTER_CAPS %x\n",
           aDevCaps[CAPS_BITMAP_FORMATS], aDevCaps[CAPS_RASTER_CAPS]);
    printf("  CAPS_MARKER_WIDTH %d, CAPS_MARKER_HEIGHT %d\n",
           aDevCaps[CAPS_MARKER_WIDTH], aDevCaps[CAPS_MARKER_HEIGHT]);
    printf("  CAPS_DEVICE_FONTS %d, CAPS_GRAPHICS_SUBSET %x\n",
           aDevCaps[CAPS_DEVICE_FONTS], aDevCaps[CAPS_GRAPHICS_SUBSET]);
    printf("  CAPS_GRAPHICS_VERSION %x, CAPS_GRAPHICS_VECTOR_SUBSET %x\n",
           aDevCaps[CAPS_GRAPHICS_VERSION],
	   aDevCaps[CAPS_GRAPHICS_VECTOR_SUBSET]);
    printf("  CAPS_DEVICE_WINDOWING %x, CAPS_ADDITIONAL_GRAPHICS %x\n",
           aDevCaps[CAPS_DEVICE_WINDOWING], aDevCaps[CAPS_ADDITIONAL_GRAPHICS]);
    printf("  (CAPS_ADDITIONAL_GRAPHICS & CAPS_COSMETIC_WIDELINE_SUPPORT %d)\n",
           aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_COSMETIC_WIDELINE_SUPPORT);
    printf("  CAPS_PHYS_COLORS %d, CAPS_COLOR_INDEX %d\n",
           aDevCaps[CAPS_PHYS_COLORS], aDevCaps[CAPS_COLOR_INDEX]);
    printf("  CAPS_GRAPHICS_CHAR_WIDTH %d, CAPS_GRAPHICS_CHAR_HEIGHT %d\n",
           aDevCaps[CAPS_GRAPHICS_CHAR_WIDTH],
           aDevCaps[CAPS_GRAPHICS_CHAR_HEIGHT]);
    printf("  CAPS_HORIZONTAL_FONT_RES %d, CAPS_VERTICAL_FONT_RES %d\n",
           aDevCaps[CAPS_HORIZONTAL_FONT_RES],
           aDevCaps[CAPS_VERTICAL_FONT_RES]);
    printf("  CAPS_DEVICE_FONT_SIM %x, CAPS_LINEWIDTH_THICK %d\n",
           aDevCaps[CAPS_DEVICE_FONT_SIM], aDevCaps[CAPS_LINEWIDTH_THICK]);
    printf("  CAPS_DEVICE_POLYSET_POINTS %x\n",
           aDevCaps[CAPS_DEVICE_POLYSET_POINTS]);
#endif

    if (globalPS == GPI_ERROR) {
#ifdef VERBOSE
        printf("globalPS ERROR %x\n", WinGetLastError(tkHab));
#endif
        return NULLHANDLE;
    }
    GpiSetCharMode(globalPS, CM_MODE2);
    bmpInfo.cbFix = 16L;
    bmpInfo.cx = xScreen;
    bmpInfo.cy = yScreen;
    bmpInfo.cPlanes = 1;
    bmpInfo.cBitCount = aDevCaps[CAPS_COLOR_BITCOUNT];
    globalBitmap = GpiCreateBitmap(globalPS, &bmpInfo, 0L, NULL, NULL);
#ifdef VERBOSE
    if (globalBitmap!=GPI_ERROR) {
        printf("GpiCreateBitmap globalBitmap OK (%x)\n", globalBitmap);
    } else {
        printf("GpiCreateBitmap globalBitmap GPI_ERROR, error %x\n",
               WinGetLastError(tkHab));
    }
#endif
    rc = GpiSetBitmap(globalPS, globalBitmap);
#ifdef VERBOSE
    if (rc!=GPI_ALTERROR) {
        printf("GpiSetBitmap globalBitmap OK\n");
    } else {
        printf("GpiSetBitmap globalBitmap GPI_ALTERROR, error %x\n",
               WinGetLastError(tkHab));
    }
#endif

    /* Preselect system fonts in globalPS */
    fattrs.usRecordLength = sizeof(fattrs);
    fattrs.fsSelection = 0;
    fattrs.lMatch = 0;
    fattrs.idRegistry = 0;
    fattrs.usCodePage = 0;
    fattrs.fsType = 0;
    fattrs.fsFontUse = FATTR_FONTUSE_NOMIX;
    fattrs.lMaxBaselineExt = 20;
    fattrs.lAveCharWidth = 9;
    strcpy(fattrs.szFacename, "System Monospaced");
    match = GpiCreateLogFont(globalPS, NULL, 1L, &fattrs);
#ifdef VERBOSE
    printf("GpiCreateLogFont System Monospaced returns %d\n", match);
#endif
    memcpy((void *)&logfonts[1].fattrs, (void *)&fattrs, sizeof(fattrs));
    rc = GpiQueryFontMetrics(globalPS, sizeof(FONTMETRICS), &fontMetrics);
    if (rc == TRUE) {
        logfonts[1].fattrs.fsSelection = fontMetrics.fsSelection;
        logfonts[1].fattrs.lMatch = fontMetrics.lMatch;
        strncpy(logfonts[1].fattrs.szFacename,fontMetrics.szFacename,FACESIZE);
        logfonts[1].fattrs.idRegistry = fontMetrics.idRegistry;
        logfonts[1].fattrs.usCodePage = fontMetrics.usCodePage;
        logfonts[1].fattrs.lMaxBaselineExt = fontMetrics.lMaxBaselineExt;
        logfonts[1].fattrs.lAveCharWidth = fontMetrics.lAveCharWidth;
        logfonts[1].fattrs.fsType = fontMetrics.fsType;
        if (fontMetrics.fsDefn & FM_DEFN_OUTLINE) {
            logfonts[1].outline = TRUE;
        } else {
            logfonts[1].outline = FALSE;
        }
        logfonts[1].setShear = FALSE;
        logfonts[1].shear.x = 0;
        logfonts[1].shear.y = 1;
        logfonts[1].deciPoints = fontMetrics.lEmHeight * 10;
    }
    fattrs.lMaxBaselineExt = 20;
    fattrs.lAveCharWidth = 8;
    strcpy(fattrs.szFacename, "System Proportional");
    match = GpiCreateLogFont(globalPS, NULL, 2L, &fattrs);
#ifdef VERBOSE
    printf("GpiCreateLogFont System Proportional returns %d\n", match);
#endif
    memcpy((void *)&logfonts[2].fattrs, (void *)&fattrs, sizeof(fattrs));
    if (rc == TRUE) {
        logfonts[2].fattrs.fsSelection = fontMetrics.fsSelection;
        logfonts[2].fattrs.lMatch = fontMetrics.lMatch;
        strncpy(logfonts[2].fattrs.szFacename,fontMetrics.szFacename,FACESIZE);
        logfonts[2].fattrs.idRegistry = fontMetrics.idRegistry;
        logfonts[2].fattrs.usCodePage = fontMetrics.usCodePage;
        logfonts[2].fattrs.lMaxBaselineExt = fontMetrics.lMaxBaselineExt;
        logfonts[2].fattrs.lAveCharWidth = fontMetrics.lAveCharWidth;
        logfonts[2].fattrs.fsType = fontMetrics.fsType;
        if (fontMetrics.fsDefn & FM_DEFN_OUTLINE) {
            logfonts[2].outline = TRUE;
        } else {
            logfonts[2].outline = FALSE;
        }
        logfonts[2].setShear = FALSE;
        logfonts[2].shear.x = 0;
        logfonts[2].shear.y = 1;
        logfonts[2].deciPoints = fontMetrics.lEmHeight * 10;
    }
    fattrs.lMaxBaselineExt = 16;
    fattrs.lAveCharWidth = 6;
    strcpy(fattrs.szFacename, "WarpSans");
    match = GpiCreateLogFont(globalPS, NULL, 3L, &fattrs);
#ifdef VERBOSE
    printf("GpiCreateLogFont WarpSans returns %d\n", match);
#endif
    memcpy((void *)&logfonts[3].fattrs, (void *)&fattrs, sizeof(fattrs));
    if (rc == TRUE) {
        logfonts[3].fattrs.fsSelection = fontMetrics.fsSelection;
        logfonts[3].fattrs.lMatch = fontMetrics.lMatch;
        strncpy(logfonts[3].fattrs.szFacename,fontMetrics.szFacename,FACESIZE);
        logfonts[3].fattrs.idRegistry = fontMetrics.idRegistry;
        logfonts[3].fattrs.usCodePage = fontMetrics.usCodePage;
        logfonts[3].fattrs.lMaxBaselineExt = fontMetrics.lMaxBaselineExt;
        logfonts[3].fattrs.lAveCharWidth = fontMetrics.lAveCharWidth;
        logfonts[3].fattrs.fsType = fontMetrics.fsType;
        if (fontMetrics.fsDefn & FM_DEFN_OUTLINE) {
            logfonts[3].outline = TRUE;
        } else {
            logfonts[3].outline = FALSE;
        }
        logfonts[3].setShear = FALSE;
        logfonts[3].shear.x = 0;
        logfonts[3].shear.y = 1;
        logfonts[3].deciPoints = fontMetrics.lEmHeight * 10;
    }
    fattrs.lMaxBaselineExt = 16;
    fattrs.lAveCharWidth = 7;
    strcpy(fattrs.szFacename, "WarpSans Bold");
    match = GpiCreateLogFont(globalPS, NULL, 4L, &fattrs);
#ifdef VERBOSE
    printf("GpiCreateLogFont WarpSans Bold returns %d\n", match);
    fflush(stdout);
#endif
    memcpy((void *)&logfonts[4].fattrs, (void *)&fattrs, sizeof(fattrs));
    if (rc == TRUE) {
        logfonts[4].fattrs.fsSelection = fontMetrics.fsSelection;
        logfonts[4].fattrs.lMatch = fontMetrics.lMatch;
        strncpy(logfonts[4].fattrs.szFacename,fontMetrics.szFacename,FACESIZE);
        logfonts[4].fattrs.idRegistry = fontMetrics.idRegistry;
        logfonts[4].fattrs.usCodePage = fontMetrics.usCodePage;
        logfonts[4].fattrs.lMaxBaselineExt = fontMetrics.lMaxBaselineExt;
        logfonts[4].fattrs.lAveCharWidth = fontMetrics.lAveCharWidth;
        logfonts[4].fattrs.fsType = fontMetrics.fsType;
        if (fontMetrics.fsDefn & FM_DEFN_OUTLINE) {
            logfonts[4].outline = TRUE;
        } else {
            logfonts[4].outline = FALSE;
        }
        logfonts[4].setShear = FALSE;
        logfonts[4].shear.x = 0;
        logfonts[4].shear.y = 1;
        logfonts[4].deciPoints = fontMetrics.lEmHeight * 10;
    }

    /* Determine color table if no palette support but color table support */
    if (!(aDevCaps[CAPS_ADDITIONAL_GRAPHICS] & CAPS_PALETTE_MANAGER) &&
        aDevCaps[CAPS_COLOR_TABLE_SUPPORT]) {
        LONG aClrData[4];

        nextColor = 16;	/* Assume VGA color table */
        rc = GpiQueryColorData(globalPS, 4, aClrData);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiQueryColorData ERROR %x\n", WinGetLastError(tkHab));
        } else {
            printf("GpiQueryColorData: format %x, loind %d, hiind %d, options %x\n",
                    aClrData[QCD_LCT_FORMAT], aClrData[QCD_LCT_LOINDEX],
                    aClrData[QCD_LCT_HIINDEX], aClrData[QCD_LCT_OPTIONS]); 
        }
#endif
        nextColor = aClrData[QCD_LCT_HIINDEX] + 1;
    }

    return tkHab;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2ExitPM --
 *
 *	Performs OS/2 Presentation Manager sign-off routines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resets global variables tkHab and tkHmq.
 *
 *----------------------------------------------------------------------
 */

void
TkOS2ExitPM (void)
{
    GpiSetBitmap(globalPS, NULLHANDLE);
    GpiDestroyPS(globalPS);
    DevCloseDC(hScreenDC);
    if (tkHab != NULLHANDLE) {
        if (tkHmq != NULLHANDLE) {
            WinDestroyMsgQueue(tkHmq);
        }
        WinTerminate(tkHab);
    }
    tkHmq= tkHab= 0;
}

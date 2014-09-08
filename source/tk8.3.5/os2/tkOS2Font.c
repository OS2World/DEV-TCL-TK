/* 
 * tkOS2Font.c --
 *
 *	Contains the OS/2 implementation of the platform-independant
 *	font package interface.
 *
 * Copyright (c) 1994 Software Research Associates, Inc. 
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"
#include "tkFont.h"

/*
 * The following structure represents a font family.  It is assumed that
 * all screen fonts constructed from the same "font family" share certain
 * properties; all screen fonts with the same "font family" point to a
 * shared instance of this structure.  The most important shared property
 * is the character existence metrics, used to determine if a screen font
 * can display a given Unicode character.
 *
 * Under OS/2, a "font family" is uniquely identified by its face name.
 */

#define FONTMAP_SHIFT       10

#define FONTMAP_PAGES           (1 << (sizeof(Tcl_UniChar)*8 - FONTMAP_SHIFT))
#define FONTMAP_BITSPERPAGE     (1 << FONTMAP_SHIFT)

typedef struct FontFamily {
    struct FontFamily *nextPtr; /* Next in list of all known font families. */
    int refCount;               /* How many SubFonts are referring to this
                                 * FontFamily.  When the refCount drops to
                                 * zero, this FontFamily may be freed. */
    /*
     * Key.
     */

    Tk_Uid faceName;            /* Face name key for this FontFamily. */

    /*
     * Derived properties.
     */

    Tcl_Encoding encoding;      /* Encoding for this font family. */
    int isSymbolFont;           /* Non-zero if this is a symbol font. */
    int isWideFont;             /* 1 if this is a double-byte font, 0
                                 * otherwise. */
    char *fontMap[FONTMAP_PAGES];
                                /* Two-level sparse table used to determine
                                 * quickly if the specified character exists.
                                 * As characters are encountered, more pages
                                 * in this table are dynamically added.  The
                                 * contents of each page is a bitmask
                                 * consisting of FONTMAP_BITSPERPAGE bits,
                                 * representing whether this font can be used
                                 * to display the given character at the
                                 * corresponding bit position.  The high bits
                                 * of the character are used to pick which
                                 * page of the table is used. */
} FontFamily;

/*
 * The following structure encapsulates an individual screen font.  A font
 * object is made up of however many SubFonts are necessary to display a
 * stream of multilingual characters.
 */

typedef struct SubFont {
    char **fontMap;             /* Pointer to font map from the FontFamily,
                                 * cached here to save a dereference. */
    LONG hFont;                 /* The specific screen font that will be
                                 * used when displaying/measuring chars
                                 * belonging to the FontFamily. */
    HPS hps;                    /* The HPS in which the font was set. */
    FontFamily *familyPtr;      /* The FontFamily for this SubFont. */
} SubFont;

/*
 * The following structure represents Windows' implementation of a font
 * object.
 */

#define SUBFONT_SPACE           3
#define BASE_CHARS              128

typedef struct OS2Font {
    TkFont font;                /* Stuff used by generic font package.  Must
                                 * be first in structure. */
    SubFont staticSubFonts[SUBFONT_SPACE];
                                /* Builtin space for a limited number of
                                 * SubFonts. */
    int numSubFonts;            /* Length of following array. */
    SubFont *subFontArray;      /* Array of SubFonts that have been loaded
                                 * in order to draw/measure all the characters
                                 * encountered by this font so far.  All fonts
                                 * start off with one SubFont initialized by
                                 * AllocFont() from the original set of font
                                 * attributes.  Usually points to
                                 * staticSubFonts, but may point to malloced
                                 * space if there are lots of SubFonts. */

    HWND hwnd;                  /* Toplevel window of application that owns
                                 * this font, used for getting HPS. */
    int pixelSize;              /* Original pixel size used when font was
                                 * constructed. */
    LONG widths[BASE_CHARS];    /* Widths of first 128 chars in the base
                                 * font, for handling common case.  The base
                                 * font is always used to draw characters
                                 * between 0x0000 and 0x007f. */
} OS2Font;

/*
 * The following structure is used as to map between the Tcl strings
 * that represent the system fonts and the numbers used by Windows and OS/2.
 */

#define OS2_SYSTEM_FONT           0
#define OS2_SYSTEM_MONO_FONT      1
#define OS2_SYSTEM_PROP_FONT      2
#define OS2_SYSTEM_SANS_FONT      3
#define OS2_SYSTEM_SANS_BOLD_FONT 4

static TkStateMap systemMap[] = {
    {OS2_SYSTEM_FONT,           "System"},
    {OS2_SYSTEM_MONO_FONT,      "System Monospaced"},
    {OS2_SYSTEM_PROP_FONT,      "System Proportional"},
    {OS2_SYSTEM_SANS_FONT,      "WarpSans"},
    {OS2_SYSTEM_SANS_BOLD_FONT, "WarpSans Bold"},
    {OS2_SYSTEM_MONO_FONT,      "ansifixed"},
    {OS2_SYSTEM_PROP_FONT,      "ansi"},
    {OS2_SYSTEM_FONT,           "device"},
    {OS2_SYSTEM_MONO_FONT,      "oemfixed"},
    {OS2_SYSTEM_MONO_FONT,      "systemfixed"},
    {OS2_SYSTEM_FONT,           "system"},
    {-1,                         NULL}
};

typedef struct ThreadSpecificData {
    FontFamily *fontFamilyList; /* The list of font families that are
                                 * currently loaded.  As screen fonts
                                 * are loaded, this list grows to hold
                                 * information about what characters
                                 * exist in each font family.  */
    Tcl_HashTable uidTable;
#if 0
    TkOS2Font logfonts[255];    /* List of logical fonts */
    LONG nextLogicalFont;       /* First free logical font ID */
    HPS globalPS;               /* Global PS for Fonts (Gpi*Char*) */
    HBITMAP globalBitmap;       /* Bitmap for global PS */
#ifdef IGNOREPMRES
    LONG overrideResolution= 72;        /* If IGNOREPMRES is defined */
#endif
#endif
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Information cached about the system at startup time.
 */

static Tcl_Encoding unicodeEncoding;
static Tcl_Encoding systemEncoding;

/*
 * Procedures used only in this file.
 */

static TkFont *         AllocFont _ANSI_ARGS_((TkFont *tkFontPtr,
                            Tk_Window tkwin, LONG hFont));
static FontFamily *     AllocFontFamily(HPS hps, LONG hFont, int base);
static SubFont *        CanUseFallback(HPS hps, OS2Font *fontPtr,
                            char *fallbackName, int ch);
static SubFont *        CanUseFallbackWithAliases(HPS hps, OS2Font *fontPtr,
                            char *faceName, int ch, Tcl_DString *nameTriedPtr);
static int              FamilyExists(HPS hps, CONST char *faceName);
static char *           FamilyOrAliasExists(HPS hps, CONST char *faceName);
static SubFont *        FindSubFontForChar(OS2Font *fontPtr, int ch);
static void             FontMapInsert(SubFont *subFontPtr, int ch);
static void             FontMapLoadPage(SubFont *subFontPtr, int row);
static int              FontMapLookup(SubFont *subFontPtr, int ch);
static void             FreeFontFamily(FontFamily *familyPtr);
static LONG             GetScreenFont(HPS hps, CONST TkFontAttributes *faPtr,
                            CONST char *faceName, int pixelSize);
static void             InitFont(Tk_Window tkwin, LONG hFont,
                            int overstrike, OS2Font *tkFontPtr);
static void             InitSubFont(HPS hps, LONG hFont, int base,
                            SubFont *subFontPtr);
static void             MultiFontTextOut(HPS hps, OS2Font *fontPtr,
                            CONST char *source, int numBytes, int x, int y);
static void             ReleaseFont(OS2Font *fontPtr);
static void             ReleaseSubFont(SubFont *subFontPtr);
static int              SeenName(CONST char *name, Tcl_DString *dsPtr);

/*
 * Code pages used in this file, 1004 is Windows compatible, 65400 must be
 * used if the font contains special glyphs, ie. Symbol.
 */

#define CP_LATIN1 850L
#define CP_1004   1004L
#define CP_65400  65400L

/*
 * Determine desired point size in pixels with device resolution.
 * Font resolution is returned by PM in pels per inch, device resolution
 * is in dots per inch. 72 decipoints in an inch.
 * Add 36 for correct rounding.
 * aDevCaps[CAPS_VERTICAL_FONT_RES] is vertical font resolution in pels per
 * inch
 */
#ifdef IGNOREPMRES
    /*
     * Requested by Ilya Zakharevich:
     * Shrink 120 to the value of overrideResolution to facilitate 'better'
     * sizing for those displays which report a resolution of 120dpi but have
     * actual resolution close to 96dpi (VGA upto ?800x600?).
     * This is obviously dependent on both resolution and screen size,
     * as higher resolutions usually use 120dpi fonts, regardless of any
     * screen size.
     */
    #define PIXTOPOINT(pixels) ( \
        (aDevCaps[CAPS_VERTICAL_FONT_RES] == 120) \
        ? (((pixels) * 72 + 36) / overrideResolution) \
        : (((pixels) * 72 + 36) / aDevCaps[CAPS_VERTICAL_FONT_RES]) \
    )
    #define POINTTOPIX(points) ( \
        (aDevCaps[CAPS_VERTICAL_FONT_RES] == 120) \
        ? (((points) * overrideResolution + 36) / 72) \
        : (((points) * aDevCaps[CAPS_VERTICAL_FONT_RES] + 36) / 72) \
    )
    #define PTOP(p) ( \
        (aDevCaps[CAPS_VERTICAL_FONT_RES] == 120) \
        ? (((p) * overrideResolution + 60) / 120 ) \
        : (p) \
    )
    #define FIX_RES(res) (if (res==120) {res = overrideResolution})
#else
#if 0
    #define PIXTOPOINT(pixels) \
        (((pixels) * 72 + 36) / aDevCaps[CAPS_VERTICAL_FONT_RES])
    #define POINTTOPIX(points) \
        (((points) * aDevCaps[CAPS_VERTICAL_FONT_RES] + 36) / 72)
#endif
    #define PIXTOPOINT(pixels) \
        ((int) ((pixels * 72.0 / 25.4 \
         * ((aDevCaps[CAPS_WIDTH]*1000) / aDevCaps[CAPS_HORIZONTAL_RESOLUTION])\
         / aDevCaps[CAPS_WIDTH]) + 0.5))
    #define POINTTOPIX(size) \
        ((int) ((size * 25.4 / 72.0 \
         * (aDevCaps[CAPS_WIDTH]) \
         / ((aDevCaps[CAPS_WIDTH]*1000)/aDevCaps[CAPS_HORIZONTAL_RESOLUTION]))\
         + 0.5))
    #define PTOP(p)  (p)
    #define FIX_RES(res)
#endif

/*
 * Quotes from GPI Guide and Reference:
 * "Font Data structures and Attributes"
 *  [...]
 *  The value of Em height represents the font point size in world coordinates
 *  and is the same as the character cell height. For an outline font, this can
 *  be set by the character cell height attribute.
 *  [...]
 *  The maximum baseline extent for a font is the sum of the maximum ascender
 *  and the maximum descender. Maximum baseline extent is not equal to cell
 *  height for outline fonts, but is for image fonts."
 * "FATTRS Data structure
 *  [...]
 *  The maximum baseline extent is the vertical space occupied by the
 *  characters in the font. If you are setting the font-use indicator
 *  FATTR_FONTUSE_OUTLINE, you should set the maximum baseline extent to 0.
 *  Outline fonts take an equivalent value from the character cell attribute
 *  that is current when text is written to an output device.
 *  The maximum baseline extent is required to select an image font and must be
 *  specified in world coordinates.
 *  [...]
 *  The maximum baseline extent in the FATTRS data structure is used for
 *  programming, unlike the maximum baseline extent in the FONTMETRICS data
 *  structure, which is only a measurement as recommended by the font's
 *  designer."
 */


/*
 *-------------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *      This procedure is called when an application is created.  It
 *      initializes all the structures that are used by the
 *      platform-dependent code on a per application basis.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *
 *      None.
 *
 *-------------------------------------------------------------------------
 */

void
TkpFontPkgInit(
    TkMainInfo *mainPtr)        /* The application being created. */
{
#ifdef VERBOSE
    printf("TkpFontPkgInit\n");
    fflush(stdout);
#endif
    unicodeEncoding = Tcl_GetEncoding(NULL, "unicode");
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *      Map a platform-specific native font name to a TkFont.
 *
 * Results:
 *      The return value is a pointer to a TkFont that represents the
 *      native font.  If a native font by the given name could not be
 *      found, the return value is NULL.
 *
 *      Every call to this procedure returns a new TkFont structure,
 *      even if the name has already been seen before.  The caller should
 *      call TkpDeleteFont() when the font is no longer needed.
 *
 *      The caller is responsible for initializing the memory associated
 *      with the generic TkFont when this function returns and releasing
 *      the contents of the generic TkFont before calling TkpDeleteFont().
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

TkFont *
TkpGetNativeFont(
    Tk_Window tkwin,           /* For display where font will be used. */
    CONST char *name)          /* Platform-specific font name. */
{
    LONG hFont;
    OS2Font *fontPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

#ifdef VERBOSE
    printf("TkpGetNativeFont [%s], tkwin %x\n", name, tkwin);
    fflush(stdout);
    printf("    tkwin->mainPtr %x\n", ((TkWindow*) tkwin)->mainPtr);
    fflush(stdout);
    printf("    tkwin->mainPtr->winPtr %x\n",
           ((TkWindow*) tkwin)->mainPtr->winPtr);
    fflush(stdout);
#endif

    hFont = TkFindStateNum(NULL, NULL, systemMap, name);
    if (hFont < 0) {
#ifdef VERBOSE
        printf("    => hFont < 0\n");
#endif
        return NULL;
    }
#ifdef VERBOSE
    printf("    => hFont %d\n", hFont);
#endif

    tkwin = (Tk_Window) ((TkWindow *) tkwin)->mainPtr->winPtr;
    fontPtr = (OS2Font *) ckalloc(sizeof(OS2Font));
    if (fontPtr == (OS2Font *)NULL) {
#ifdef VERBOSE
        printf("    => can't allocate fontPtr\n");
        fflush(stdout);
#endif
        return (TkFont *) NULL;
    }
#ifdef VERBOSE
    printf("fontPtr %x\n", fontPtr);
    fflush(stdout);
#endif
    InitFont(tkwin, hFont, 0, fontPtr);

    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFromAttributes --
 *
 *      Given a desired set of attributes for a font, find a font with
 *      the closest matching attributes.
 *
 * Results:
 *      The return value is a pointer to a TkFont that represents the
 *      font with the desired attributes.  If a font with the desired
 *      attributes could not be constructed, some other font will be
 *      substituted automatically.  NULL is never returned.
 *
 *      Every call to this procedure returns a new TkFont structure,
 *      even if the specified attributes have already been seen before.
 *      The caller should call TkpDeleteFont() to free the platform-
 *      specific data when the font is no longer needed.
 *
 *      The caller is responsible for initializing the memory associated
 *      with the generic TkFont when this function returns and releasing
 *      the contents of the generic TkFont before calling TkpDeleteFont().
 *
 * Side effects:
 *      Memory allocated.
 *
 *---------------------------------------------------------------------------
 */

TkFont *
TkpGetFontFromAttributes(
    TkFont *tkFontPtr,          /* If non-NULL, store the information in
                                 * this existing TkFont structure, rather than
                                 * allocating a new structure to hold the
                                 * font; the existing contents of the font
                                 * will be released.  If NULL, a new TkFont
                                 * structure is allocated. */
    Tk_Window tkwin,            /* For display where font will be used. */
    CONST TkFontAttributes *faPtr)
                                /* Set of attributes to match. */
{
    int i, j;
    HPS hps;
    HWND hwnd;
    Window window;
    OS2Font *fontPtr;
    char ***fontFallbacks;
    char *faceName, *fallback, *actualName;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    LONG hFont = nextLogicalFont;

#ifdef VERBOSE
    printf("TkpGetFontFromAttributes [%s] %d %c %c %c %c\n",
           faPtr->family, faPtr->size,
           faPtr->weight == TK_FW_NORMAL ? 'N' :
                            (faPtr->weight == TK_FW_BOLD ? 'B' : '?'),
           faPtr->slant == TK_FS_ROMAN ? 'R' :
                            (faPtr->slant == TK_FS_ITALIC ? 'I' :
                             (faPtr->slant == TK_FS_OBLIQUE ? 'O' : '?')),
           faPtr->underline ? '_' : ' ',
           faPtr->overstrike ? '-' : ' ');
    fflush(stdout);
#endif

    if (hFont > MAX_LID) {
        /* We can't simultaneously use more than MAX_LID fonts */
        hFont = MAX_LID;
        goto fontFallback;
    }
    tkwin   = (Tk_Window) ((TkWindow *) tkwin)->mainPtr->winPtr;
    window = Tk_WindowId(((TkWindow *) tkwin)->mainPtr->winPtr);
    hwnd = (window == None) ? HWND_DESKTOP : TkOS2GetHWND(window);
    hps = WinGetPS(hwnd);
#ifdef VERBOSE
    printf("    hwnd %x hps %x\n", hwnd, hps);
    fflush(stdout);
#endif

    /*
     * Algorithm to get the closest font name to the one requested.
     *
     * try fontname
     * try all aliases for fontname
     * foreach fallback for fontname
     *      try the fallback
     *      try all aliases for the fallback
     */

    faceName = faPtr->family;
    if (faceName != NULL) {
        actualName = FamilyOrAliasExists(hps, faceName);
        if (actualName != NULL) {
            faceName = actualName;
            goto found;
        }
fontFallback:
        fontFallbacks = TkFontGetFallbacks();
        for (i = 0; fontFallbacks[i] != NULL; i++) {
            for (j = 0; (fallback = fontFallbacks[i][j]) != NULL; j++) {
                if (strcasecmp(faceName, fallback) == 0) {
                    break;
                }
            }
            if (fallback != NULL) {
                for (j = 0; (fallback = fontFallbacks[i][j]) != NULL; j++) {
                    actualName = FamilyOrAliasExists(hps, fallback);
                    if (actualName != NULL) {
                        faceName = actualName;
                        goto found;
                    }
                }
            }
        }
    }

    found:
    hFont = GetScreenFont(hps, faPtr, faceName,
                          TkFontGetPixels(tkwin, faPtr->size));

    if (tkFontPtr == NULL) {
        fontPtr = (OS2Font *) ckalloc(sizeof(OS2Font));
    } else {
        fontPtr = (OS2Font *) tkFontPtr;
        ReleaseFont(fontPtr);
    }
    InitFont(tkwin, hFont, faPtr->overstrike, fontPtr);
    WinReleasePS(hps);

    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *      Called to release a font allocated by TkpGetNativeFont() or
 *      TkpGetFontFromAttributes().  The caller should have already
 *      released the fields of the TkFont that are used exclusively by
 *      the generic TkFont code.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TkFont is deallocated.
 *
 *---------------------------------------------------------------------------
 */

void
TkpDeleteFont(
    TkFont *tkFontPtr)          /* Token of font to be deleted. */
{
    OS2Font *fontPtr;
#ifdef VERBOSE
    printf("TkpDeleteFont\n");
    fflush(stdout);
#endif

    fontPtr = (OS2Font *) tkFontPtr;
    ReleaseFont(fontPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFamilies --
 *
 *      Return information about the font families that are available
 *      on the display of the given window.
 *
 * Results:
 *      interp->result is modified to hold a list of all the available
 *      font families.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

void
TkpGetFontFamilies(
    Tcl_Interp *interp,         /* Interp to hold result. */
    Tk_Window tkwin)            /* For display to query. */
{
    Window window;
    HWND hwnd;
    HPS hps;
    PFONTMETRICS os2fonts;
    LONG reqFonts, remFonts;
    int i;
    char *familyName;
    Tcl_DString familyString;
    Tcl_Obj *strPtr;
#ifdef VERBOSE
    printf("TkpGetFontFamilies\n");
    fflush(stdout);
#endif

    window = Tk_WindowId(tkwin);
    hwnd = (window == (Window) NULL) ? HWND_DESKTOP : TkOS2GetHWND(window);
    hps = WinGetPS(hwnd);

    /* Determine total number of fonts */
    reqFonts = 0L;
    remFonts = GpiQueryFonts(hps, QF_PUBLIC, NULL, &reqFonts,
                             (LONG) sizeof(FONTMETRICS), NULL);
#ifdef VERBOSE
    printf("TkpGetFontFamilies, nr.of fonts: %d\n", remFonts);
#endif

    /* Allocate space for the fonts */
    os2fonts = (PFONTMETRICS) ckalloc(remFonts * sizeof(FONTMETRICS));
    if (os2fonts == NULL) {
        return;
    }

    /* Retrieve the fonts */
    reqFonts = remFonts;
    remFonts = GpiQueryFonts(hps, QF_PUBLIC, NULL, &reqFonts,
                             (LONG) sizeof(FONTMETRICS), os2fonts);
#ifdef VERBOSE
    printf("    got %d (%d remaining)\n", reqFonts, remFonts);
#endif
    for (i=0; i<reqFonts; i++) {
#ifdef VERBOSE
        printf("m%d Em%d %ddpt lMBE%d %dx%d %s %s face[%s]%s fam[%s]%s\n",
              os2fonts[i].lMatch, os2fonts[i].lEmHeight,
              os2fonts[i].sNominalPointSize, os2fonts[i].lMaxBaselineExt,
              os2fonts[i].sXDeviceRes, os2fonts[i].sYDeviceRes,
              (os2fonts[i].fsType & FM_TYPE_FIXED) ? "fix" : "prop",
              (os2fonts[i].fsDefn & FM_DEFN_OUTLINE) ? "outl" : "bmp",
              os2fonts[i].szFacename,
              (os2fonts[i].fsType & FM_TYPE_FACETRUNC) ? " (trunc)" : "",
              os2fonts[i].szFamilyname,
              (os2fonts[i].fsType & FM_TYPE_FAMTRUNC) ? " (trunc)" : "");
#endif
	if (os2fonts[i].fsType & FM_TYPE_FAMTRUNC) {
            char fullName[MAX_FLEN];
	    rc = WinQueryAtomName(WinQuerySystemAtomTable(),
                                  os2fonts[i].FamilyNameAtom, (PSZ)&fullName,
                                  MAX_FLEN);
            if (rc != 0) {
#ifdef VERBOSE
                printf("WinQueryAtomName OK: %s\n", fullName);
#endif
                if (rc >= 256) {
                    fullName[255] = '\0';
                }
                familyName = fullName;
            } else {
#ifdef VERBOSE
                printf("WinQueryAtomName ERROR %d\n",
                       WinGetLastError(TclOS2GetHAB()));
#endif
                familyName = os2fonts[i].szFamilyname;
            }
	} else {
            familyName = os2fonts[i].szFamilyname;
	}

        Tcl_ExternalToUtfDString(systemEncoding, familyName, -1, &familyString);
        strPtr = Tcl_NewStringObj(Tcl_DStringValue(&familyString),
                Tcl_DStringLength(&familyString));
        Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp), strPtr);
        Tcl_DStringFree(&familyString);
    }
    ckfree((char *)os2fonts);
    WinReleasePS(hps);
}

/*
 *-------------------------------------------------------------------------
 *
 * TkpGetSubFonts --
 *
 *      A function used by the testing package for querying the actual
 *      screen fonts that make up a font object.
 *
 * Results:
 *      Modifies interp's result object to hold a list containing the
 *      names of the screen fonts that make up the given font object.
 *
 * Side effects:
 *      None.
 *
 *-------------------------------------------------------------------------
 */

void
TkpGetSubFonts(
    Tcl_Interp *interp,         /* Interp to hold result. */
    Tk_Font tkfont)             /* Font object to query. */
{
    int i;
    OS2Font *fontPtr;
    FontFamily *familyPtr;
    Tcl_Obj *resultPtr, *strPtr;
#ifdef VERBOSE
    printf("TkpGetSubFonts\n");
    fflush(stdout);
#endif

    resultPtr = Tcl_GetObjResult(interp);
    fontPtr = (OS2Font *) tkfont;
    for (i = 0; i < fontPtr->numSubFonts; i++) {
        familyPtr = fontPtr->subFontArray[i].familyPtr;
        strPtr = Tcl_NewStringObj(familyPtr->faceName, -1);
        Tcl_ListObjAppendElement(NULL, resultPtr, strPtr);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 *  Tk_MeasureChars --
 *
 *      Determine the number of bytes from the string that will fit
 *      in the given horizontal span.  The measurement is done under the
 *      assumption that Tk_DrawChars() will be used to actually display
 *      the characters.
 *
 * Results:
 *      The return value is the number of bytes from source that
 *      fit into the span that extends from 0 to maxLength.  *lengthPtr is
 *      filled with the x-coordinate of the right edge of the last
 *      character that did fit.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */
int
Tk_MeasureChars(
    Tk_Font tkfont,             /* Font in which characters will be drawn. */
    CONST char *source,         /* UTF-8 string to be displayed.  Need not be
                                 * '\0' terminated. */
    int numBytes,               /* Maximum number of bytes to consider
                                 * from source string. */
    int maxLength,              /* If >= 0, maxLength specifies the longest
                                 * permissible line length in pixels; don't
                                 * consider any character that would cross
                                 * this x-position.  If < 0, then line length
                                 * is unbounded and the flags argument is
                                 * ignored. */
    int flags,                  /* Various flag bits OR-ed together:
                                 * TK_PARTIAL_OK means include the last char
                                 * which only partially fit on this line.
                                 * TK_WHOLE_WORDS means stop on a word
                                 * boundary, if possible.
                                 * TK_AT_LEAST_ONE means return at least one
                                 * character even if no characters fit. */
    int *lengthPtr)             /* Filled with x-location just after the
                                 * terminating character. */
{
    HPS hps;
    LONG oldFont;
    OS2Font *fontPtr;
    int curX = 0, curByte = 0;
    SubFont *lastSubFontPtr;
    POINTL aSize[TXTBOX_COUNT];
    int tmpNumBytes;
    char *str;
#ifdef VERBOSE
    printf("Tk_MeasureChars\n");
    fflush(stdout);
#endif

    fontPtr = (OS2Font *) tkfont;

    hps = WinGetPS(fontPtr->hwnd);
    lastSubFontPtr = &fontPtr->subFontArray[0];
#ifdef VERBOSE
    printf("Tk_MeasureChars [%s] (%d) maxLength %d fontID %d [%s]%d\n", source,
           numBytes, maxLength, lastSubFontPtr->hFont,
           logfonts[lastSubFontPtr->hFont].fattrs.szFacename,
           logfonts[lastSubFontPtr->hFont].deciPoints);
#endif
    oldFont = TkOS2SelectFont(hps, lastSubFontPtr->hFont);

    if (numBytes == 0) {
        curX = 0;
        curByte = 0;
    } else if (maxLength < 0) {
        Tcl_UniChar ch;
        FontFamily *familyPtr;
        Tcl_DString runString;
        SubFont *thisSubFontPtr;
        CONST char *p, *end, *next;

        /*
         * A three step process:
         * 1. Find a contiguous range of characters that can all be
         *    represented by a single screen font.
         * 2. Convert those chars to the encoding of that font.
         * 3. Measure converted chars.
         */

        curX = 0;
        end = source + numBytes;
        for (p = source; p < end; ) {
            next = p + Tcl_UtfToUniChar(p, &ch);
            thisSubFontPtr = FindSubFontForChar(fontPtr, ch);
            if (thisSubFontPtr != lastSubFontPtr) {
                familyPtr = lastSubFontPtr->familyPtr;
                Tcl_UtfToExternalDString(familyPtr->encoding, source,
                        p - source, &runString);
                curX += TkOS2QueryTextWidth(hps, Tcl_DStringValue(&runString),
                                            Tcl_DStringLength(&runString));
#ifdef VERBOSE
                printf("maxLength <= 0; curX %d\n", curX);
#endif
                Tcl_DStringFree(&runString);
                lastSubFontPtr = thisSubFontPtr;
                source = p;

                /* Select font */
                /* oldFont = */ TkOS2SelectFont(hps, lastSubFontPtr->hFont);
            }

            p = next;
        }
        familyPtr = lastSubFontPtr->familyPtr;
        Tcl_UtfToExternalDString(familyPtr->encoding, source, p - source,
                &runString);
        curX += TkOS2QueryTextWidth(hps, Tcl_DStringValue(&runString),
                                    Tcl_DStringLength(&runString));
#ifdef VERBOSE
        printf("maxLength <= 0; curX %d\n", curX);
#endif
        Tcl_DStringFree(&runString);
        curByte = numBytes;
    } else {
        Tcl_UniChar ch;
        char buf[16];
        FontFamily *familyPtr;
        SubFont *thisSubFontPtr;
        CONST char *term, *end, *p, *next;
        int newX, termX, sawNonSpace, dstWrote;

        /*
         * How many chars will fit in the space allotted?
         * This first version may be inefficient because it measures
         * every character individually.
         */

        next = source + Tcl_UtfToUniChar(source, &ch);
        newX = curX = termX = 0;

        term = source;
        end = source + numBytes;

        sawNonSpace = (ch > 255) || !isspace(ch);
        for (p = source; ; ) {
            if (ch < BASE_CHARS) {
                newX += fontPtr->widths[ch];
            } else {
                thisSubFontPtr = FindSubFontForChar(fontPtr, ch);
                if (thisSubFontPtr != lastSubFontPtr) {
                    /* oldFont = */ TkOS2SelectFont(hps, thisSubFontPtr->hFont);
                    lastSubFontPtr = thisSubFontPtr;
                }
                familyPtr = lastSubFontPtr->familyPtr;
                Tcl_UtfToExternal(NULL, familyPtr->encoding, p, next - p,
                        0, NULL, buf, sizeof(buf), NULL, &dstWrote, NULL);
                newX += TkOS2QueryTextWidth(hps, buf, dstWrote);
            }
            if (newX > maxLength) {
                break;
            }
            curX = newX;
            p = next;
            if (p >= end) {
                term = end;
                termX = curX;
                break;
            }

            next += Tcl_UtfToUniChar(next, &ch);
            if ((ch < 256) && isspace(ch)) {
                if (sawNonSpace) {
                    term = p;
                    termX = curX;
                    sawNonSpace = 0;
                }
            } else {
                sawNonSpace = 1;
            }
        }

        /*
         * P points to the first character that doesn't fit in the desired
         * span.  Use the flags to figure out what to return.
         */

        if ((flags & TK_PARTIAL_OK) && (p < end) && (curX < maxLength)) {
            /*
             * Include the first character that didn't quite fit in the desired
             * span.  The width returned will include the width of that extra
             * character.
             */

            curX = newX;
            p += Tcl_UtfToUniChar(p, &ch);
        }
        if ((flags & TK_AT_LEAST_ONE) && (term == source) && (p < end)) {
            term = p;
            termX = curX;
            if (term == source) {
                term += Tcl_UtfToUniChar(term, &ch);
                termX = newX;
            }
        } else if ((p >= end) || !(flags & TK_WHOLE_WORDS)) {
            term = p;
            termX = curX;
        }

        curX = termX;
        curByte = term - source;
    }

    GpiSetCharSet(hps, oldFont);
    WinReleasePS(hps);

#ifdef VERBOSE
    printf("Tk_MeasureChars [%s] (%d) maxLength %d returns x %d (curByte %d)\n",
           source, numBytes, maxLength, curX, curByte);
#endif
    *lengthPtr = curX;
    return curByte;
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *      Draw a string of characters on the screen.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Information gets drawn on the screen.
 *
 *---------------------------------------------------------------------------
 */

void
Tk_DrawChars(
    Display *display,           /* Display on which to draw. */
    Drawable drawable,          /* Window or pixmap in which to draw. */
    GC gc,                      /* Graphics context for drawing characters. */
    Tk_Font tkfont,             /* Font in which characters will be drawn;
                                 * must be the same as font used in GC. */
    CONST char *source,         /* UTF-8 string to be displayed.  Need not be
                                 * '\0' terminated.  All Tk meta-characters
                                 * (tabs, control characters, and newlines)
                                 * should be stripped out of the string that
                                 * is passed to this function.  If they are
                                 * not stripped out, they will be displayed as
                                 * regular printing characters. */
    int numBytes,               /* Number of bytes in string. */
    int x, int y)               /* Coordinates at which to place origin of
                                 * string when drawing. */
{
    HPS hps;
    LONG oldFont = 0L;
    LONG oldHorAlign, oldVerAlign;
    LONG oldBackMix;
    LONG oldColor, oldBackColor = 0L;
    POINTL oldRefPoint;
    LONG oldPattern;
    HBITMAP oldBitmap;
    POINTL aPoints[3]; /* Lower-left, upper-right, lower-left source */
    CHARBUNDLE cBundle;
    LONG windowHeight;
    int l, tmpNumChars;
    char *str;
    POINTL refPoint;
    TkOS2PSState state;
    OS2Font *fontPtr;
#ifdef VERBOSE
    printf("Tk_DrawChars\n");
    fflush(stdout);
#endif

    fontPtr = (OS2Font *) gc->font;
    display->request++;

    if (drawable == None) {
        return;
    }

#ifdef VERBOSE
    printf("Tk_DrawChars [%s] (%d) at (%d,%d) font [%d], GC %x (fs %s, s %x)\n",
           source, numBytes, x, y, (&fontPtr->subFontArray[0])->hFont, gc,
           gc->fill_style == FillStippled ? "FillStippled" :
           (gc->fill_style == FillOpaqueStippled ? "FillOpaqueStippled" :
           (gc->fill_style == FillSolid ? "FillSolid" :
           (gc->fill_style == FillTiled ? "FillTiled" : "UNKNOWN"))));
#endif

    hps = TkOS2GetDrawablePS(display, drawable, &state);

    GpiSetMix(hps, tkpOS2MixModes[gc->function]);

    /*
     * Translate the Y coordinates to PM coordinates.
     * X Window System y-coordinate is the position of the baseline like
     * in PM, so we don't have to take the height of the characters into
     * consideration.
     */
    windowHeight = TkOS2WindowHeight((TkOS2Drawable *)drawable);
#ifdef VERBOSE
    printf("    x %d, y %d (PM: %d)\n", x, y, windowHeight - y);
#endif
    y = windowHeight - y;

    if ((gc->fill_style == FillStippled
            || gc->fill_style == FillOpaqueStippled)
            && (gc->stipple != None)) {

        TkOS2Drawable *todPtr = (TkOS2Drawable *)gc->stipple;
        HPS dcMem;
        HPS psMem;
        DEVOPENSTRUC dop = {0L, (PSZ)"DISPLAY", NULL, 0L, 0L, 0L, 0L, 0L, 0L};
        SIZEL sizl = {0,0}; /* use same page size as device */
        HBITMAP bitmap;
        BITMAPINFOHEADER2 bmpInfo;
        RECTL rect;
        POINTL textBox[TXTBOX_COUNT];
        FONTMETRICS fm;

#ifdef VERBOSE
       printf("Tk_DrawChars stippled \"%s\" (%x) at %d,%d fg %d bg %d bmp %x\n",
              source, gc->stipple, x, y, gc->foreground, gc->background,
              todPtr->bitmap.handle);
#endif

        if (todPtr->type != TOD_BITMAP) {
            panic("unexpected drawable type in stipple");
        }

        /*
         * Select stipple pattern into destination PS.
         * gc->ts_x_origin and y_origin are relative to origin of the
         * destination drawable, while PatternRefPoint is in world coords.
         */

        dcMem = DevOpenDC(TclOS2GetHAB(), OD_MEMORY, (PSZ)"*", 5L,
                          (PDEVOPENDATA)&dop, NULLHANDLE);
        if (dcMem == DEV_ERROR) {
#ifdef VERBOSE
            printf("DevOpenDC ERROR %x in Tk_DrawChars\n",
                   WinGetLastError(TclOS2GetHAB()));
#endif
            return;
        }
#ifdef VERBOSE
        printf("DevOpenDC in Tk_DrawChars returns %x\n", dcMem);
#endif
        psMem = GpiCreatePS(TclOS2GetHAB(), dcMem, &sizl,
                            PU_PELS | GPIT_NORMAL | GPIA_ASSOC);
        if (psMem == GPI_ERROR) {
#ifdef VERBOSE
            printf("GpiCreatePS ERROR %x in Tk_DrawChars\n",
                   WinGetLastError(TclOS2GetHAB()));
#endif
            DevCloseDC(dcMem);
            return;
        }
#ifdef VERBOSE
        printf("GpiCreatePS in Tk_DrawChars returns %x\n", psMem);
#endif

        /*
         * Compute the bounding box and create a compatible bitmap.
         */

        rc = WinQueryWindowRect(((TkOS2Drawable *)drawable)->bitmap.parent,
	                        &rect);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("WinQueryWindowRect ERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("WinQueryWindowRect OK %d,%d->%d,%d\n", rect.xLeft,
                   rect.yBottom, rect.xRight, rect.yTop);
        }
#endif
        bmpInfo.cbFix = 16L;
        /*
        bmpInfo.cx = rect.xRight - rect.xLeft;
        bmpInfo.cy = rect.yTop - rect.yBottom;
        */
        bmpInfo.cx = xScreen;
        bmpInfo.cy = yScreen;
        bmpInfo.cPlanes = 1;
        bmpInfo.cBitCount= display->screens[display->default_screen].root_depth;
        bitmap = GpiCreateBitmap(psMem, &bmpInfo, 0L, NULL, NULL);
#ifdef VERBOSE
        if (bitmap == GPI_ERROR) {
            printf("GpiCreateBitmap (%d,%d) GPI_ERROR %x\n", bmpInfo.cx,
                   bmpInfo.cy, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiCreateBitmap (%d,%d) returned %x\n", bmpInfo.cx,
                   bmpInfo.cy, bitmap);
        }
#endif
        oldBitmap = GpiSetBitmap(psMem, bitmap);
#ifdef VERBOSE
        if (bitmap == HBM_ERROR) {
            printf("GpiSetBitmap (%x) HBM_ERROR %x\n", bitmap,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSetBitmap %x returned %x\n", bitmap, oldBitmap);
        }
#endif

        refPoint.x = gc->ts_x_origin;
        refPoint.y = windowHeight - gc->ts_y_origin;

#ifdef VERBOSE
        printf("gc->ts_x_origin=%d (->%d), gc->ts_y_origin=%d (->%d)\n",
               gc->ts_x_origin, refPoint.x, gc->ts_y_origin, refPoint.y);
#endif
        /* The bitmap mustn't be selected in the HPS */
        TkOS2SetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                        refPoint.x, refPoint.y, &oldPattern, &oldRefPoint);

        GpiQueryTextAlignment(psMem, &oldHorAlign, &oldVerAlign);
        GpiSetTextAlignment(psMem, TA_LEFT, TA_BASE);

        GpiQueryAttrs(psMem, PRIM_CHAR, LBB_COLOR, (PBUNDLE)&cBundle);
        cBundle.lColor = gc->foreground;
        rc = GpiSetAttrs(psMem, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiSetAttrs textColor %d ERROR %x\n", cBundle.lColor,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSetAttrs textColor %d OK\n", cBundle.lColor);
        }
#endif

        oldBackMix = GpiQueryBackMix(psMem);
        rc = GpiSetBackMix(psMem, BM_LEAVEALONE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiSetBackMix ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSetBackMix OK\n");
        }
#endif
        oldBackColor = GpiQueryBackColor(psMem);
        GpiSetBackColor(psMem, CLR_FALSE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiSetBackColor CLR_FALSE ERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSetBackColor CLR_FALSE OK\n");
        }
#endif

        rc = GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm);
        aPoints[0].y = -fm.lMaxDescender;
        if (fm.fsDefn & FM_DEFN_OUTLINE) {
            /* Scale the value */
            aPoints[0].y *= fm.lEmHeight;
            aPoints[0].y /= 12;
        }

        aPoints[0].x = x;
        aPoints[0].y += y;
        aPoints[1].x = x;
        aPoints[1].y = y;
        aPoints[2].x = aPoints[0].x;
        aPoints[2].y = aPoints[0].y;
        aPoints[1].x += TkOS2QueryTextWidth(hps, source, numBytes);
        rc = GpiQueryTextBox(psMem, (numBytes < 512 ? numBytes : 512), (PCH)str,
                             TXTBOX_COUNT, textBox);
        aPoints[1].y += textBox[TXTBOX_TOPRIGHT].y;

#ifdef VERBOSE
        printf("aPoints: %d,%d %d,%d <- %d,%d\n", aPoints[0].x, aPoints[0].y,
               aPoints[1].x, aPoints[1].y, aPoints[2].x, aPoints[2].y);
#endif

        /*
         * The following code is tricky because fonts are rendered in multiple
         * colors.  First we draw onto a black background and copy the white
         * bits.  Then we draw onto a white background and copy the black bits.
         * Both the foreground and background bits of the font are ANDed with
         * the stipple pattern as they are copied.
         */

        rc = GpiBitBlt(psMem, (HPS)0, 2, aPoints, ROP_ZERO, BBO_IGNORE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiBitBlt ZERO %d,%d ERROR %x\n", aPoints[0].x,
                   aPoints[0].y, WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiBitBlt ZERO %d,%d OK\n", aPoints[0].x, aPoints[0].y);
        }
#endif

        MultiFontTextOut(psMem, fontPtr, source, numBytes, x, y);

        rc = GpiBitBlt(hps, psMem, 3, aPoints, (LONG)0x00ea, BBO_IGNORE);
#ifdef VERBOSE
        if (rc==GPI_ERROR) {
            printf("GpiBitBlt ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiBitBlt OK\n");
        }
#endif

        rc = GpiBitBlt(psMem, (HPS)0, 2, aPoints, ROP_ONE, BBO_IGNORE);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiBitBlt ONE %d,%d ERROR %x\n", aPoints[0].x, aPoints[0].y,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiBitBlt ONE %d,%d OK\n", aPoints[0].x, aPoints[0].y);
        }
#endif

        MultiFontTextOut(psMem, fontPtr, source, numBytes, x, y);

/*
        rc = GpiBitBlt(hps, psMem, 3, aPoints, (LONG)0x00ba, BBO_IGNORE);
*/
        rc = GpiBitBlt(hps, psMem, 3, aPoints, (LONG)0x008a, BBO_IGNORE);
#ifdef VERBOSE
        if (rc==GPI_ERROR) {
            printf("GpiBitBlt ERROR %x\n", WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiBitBlt OK\n");
        }
#endif
        /*
         * Destroy the temporary bitmap and restore the device context.
         */

        GpiSetBitmap(psMem, oldBitmap);
        GpiDeleteBitmap(bitmap);
        GpiDestroyPS(psMem);
        DevCloseDC(dcMem);

        rc = GpiSetBackMix(hps, oldBackMix);
        /* The bitmap must be reselected in the HPS */
        TkOS2UnsetStipple(hps, todPtr->bitmap.hps, todPtr->bitmap.handle,
                          oldPattern, &oldRefPoint);

    } else {

        GpiQueryTextAlignment(hps, &oldHorAlign, &oldVerAlign);
        GpiSetTextAlignment(hps, TA_LEFT, TA_BASE);

        GpiQueryAttrs(hps, PRIM_CHAR, LBB_COLOR, (PBUNDLE)&cBundle);
        oldColor = cBundle.lColor;
        cBundle.lColor = gc->foreground;
        rc = GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiSetAttrs color %x ERROR %x\n", gc->foreground,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSetAttrs color %x OK\n", gc->foreground);
        }
#endif

        oldBackMix = GpiQueryBackMix(hps);
        GpiSetBackMix(hps, BM_LEAVEALONE);

        MultiFontTextOut(hps, fontPtr, source, numBytes, x, y);

        GpiSetBackMix(hps, oldBackMix);
        GpiSetBackColor(hps, oldBackColor);
        cBundle.lColor = oldColor;
        GpiSetAttrs(hps, PRIM_CHAR, LBB_COLOR, 0L, (PBUNDLE)&cBundle);
        GpiSetTextAlignment(hps, oldHorAlign, oldVerAlign);

    }

    TkOS2ReleaseDrawablePS(drawable, hps, &state);
}

/*
 *-------------------------------------------------------------------------
 *
 * MultiFontTextOut --
 *
 *      Helper function for Tk_DrawChars.  Draws characters, using the
 *      various screen fonts in fontPtr to draw multilingual characters.
 *      Note: No bidirectional support.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Information gets drawn on the screen.
 *      Contents of fontPtr may be modified if more subfonts were loaded
 *      in order to draw all the multilingual characters in the given
 *      string.
 *
 *-------------------------------------------------------------------------
 */

static void
MultiFontTextOut(
    HPS hps,                    /* Presentation space to draw into. */
    OS2Font *fontPtr,           /* Contains set of fonts to use when drawing
                                 * following string. */
    CONST char *source,         /* Potentially multilingual UTF-8 string. */
    int numBytes,               /* Length of string in bytes. */
    int x, int y)               /* Coordinates at which to place origin *
                                 * of string when drawing. */
{
    Tcl_UniChar ch;
    LONG oldFont;
    FontFamily *familyPtr;
    Tcl_DString runString;
    CONST char *p, *end, *next;
    SubFont *lastSubFontPtr, *thisSubFontPtr;
    POINTL refPoint;
#ifdef VERBOSE
    printf("MultiFontTextOut\n");
    fflush(stdout);
#endif

    lastSubFontPtr = &fontPtr->subFontArray[0];
    oldFont = TkOS2SelectFont(hps, lastSubFontPtr->hFont);

    refPoint.y = y;
    refPoint.x = x;
    end = source + numBytes;
    for (p = source; p < end; ) {
        next = p + Tcl_UtfToUniChar(p, &ch);
        thisSubFontPtr = FindSubFontForChar(fontPtr, ch);
        if (thisSubFontPtr != lastSubFontPtr) {
            if (p > source) {
                familyPtr = lastSubFontPtr->familyPtr;
                Tcl_UtfToExternalDString(familyPtr->encoding, source,
                        p - source, &runString);
                TkOS2CharString(hps, Tcl_DStringValue(&runString),
                                Tcl_DStringLength(&runString), &refPoint);
                refPoint.x += TkOS2QueryTextWidth(hps,
                                                 Tcl_DStringValue(&runString),
                                                 Tcl_DStringLength(&runString));
                Tcl_DStringFree(&runString);
            }
            lastSubFontPtr = thisSubFontPtr;
            source = p;
            TkOS2SelectFont(hps, lastSubFontPtr->hFont);
        }
        p = next;
    }
    if (p > source) {
        familyPtr = lastSubFontPtr->familyPtr;
        Tcl_UtfToExternalDString(familyPtr->encoding, source, p - source,
                &runString);
        TkOS2CharString(hps, Tcl_DStringValue(&runString),
                        Tcl_DStringLength(&runString), &refPoint);
        Tcl_DStringFree(&runString);
    }
    GpiSetCharSet(hps, oldFont);
}

/*
 *---------------------------------------------------------------------------
 *
 * InitFont --
 *
 *      Helper for TkpGetNativeFont() and TkpGetFontFromAttributes().
 *      Initializes the memory for a new OS2Font that wraps the
 *      platform-specific data.
 *
 *      The caller is responsible for initializing the fields of the
 *      OS2Font that are used exclusively by the generic TkFont code, and
 *      for releasing those fields before calling TkpDeleteFont().
 *
 * Results:
 *      Fills the OS2Font structure.
 *
 * Side effects:
 *      Memory allocated.
 *
 *---------------------------------------------------------------------------
 */

static void
InitFont(
    Tk_Window tkwin,            /* Main window of interp in which font will
                                 * be used, for getting HPS. */
    LONG hFont,                 /* OS/2 handle (ID) for font. */
    int overstrike,             /* The overstrike attribute of logfont used
                                 * to allocate this font.  For some reason,
                                 * the TEXTMETRICs may contain incorrect info
                                 * in the tmStruckOut field.
                                 * DOES THIS HOLD ON OS/2 THEN??? */
    OS2Font *fontPtr)           /* Filled with information constructed from
                                 * the above arguments. */
{
    HPS hps;
    HWND hwnd;
    LONG oldFont;
    FONTMETRICS fm;
    Window window;
    TkFontMetrics *fmPtr;
    Tcl_Encoding encoding;
    Tcl_DString faceString;
    TkFontAttributes *faPtr;
    int curChar;
    POINTL aSize[TXTBOX_COUNT];
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("InitFont\n");
    fflush(stdout);
#endif

    window  = Tk_WindowId(tkwin);
    hwnd    = (window == None) ? HWND_DESKTOP : TkOS2GetHWND(window);
    hps     = WinGetPS(hwnd);
#ifdef VERBOSE
    printf("InitFont window %x hwnd %x hps %x\n", window, hwnd, hps);
    fflush(stdout);
#endif
    oldFont = TkOS2SelectFont(hps, hFont);

    rc = GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm);
#ifdef VERBOSE
    if (rc == TRUE) {
        printf("GpiQueryFontMetrics hps %x OK\n", hps);
        fflush(stdout);
    } else {
        printf("GpiQueryFontMetrics hps %x ERROR %x\n", hps,
               WinGetLastError(TclOS2GetHAB()));
        fflush(stdout);
    }
#endif

    if (fm.fsType & FM_TYPE_FACETRUNC) {
        char fullName[MAX_FLEN];
        rc = WinQueryAtomName(WinQuerySystemAtomTable(),
                              fm.FaceNameAtom, (PSZ)&fullName,
                              MAX_FLEN);
        if (rc != 0) {
#ifdef VERBOSE
            printf("WinQueryAtomName OK: %s\n", fullName);
#endif
            if (rc >= MAX_FLEN) {
                fullName[MAX_FLEN] = '\0';
            }
            Tcl_ExternalToUtfDString(systemEncoding, fullName, -1, &faceString);
        } else {
            Tcl_ExternalToUtfDString(systemEncoding, fm.szFacename, -1,
                                     &faceString);
        }
    } else {
        Tcl_ExternalToUtfDString(systemEncoding, fm.szFacename, -1,
                                 &faceString);
    }

    fontPtr->font.fid   = (Font) fontPtr;

    faPtr               = &fontPtr->font.fa;
    faPtr->family       = Tk_GetUid(Tcl_DStringValue(&faceString));
    faPtr->size         = TkFontGetPoints(tkwin, -fm.lEmHeight);
    faPtr->weight       = (fm.usWeightClass > 5) ? TK_FW_BOLD : TK_FW_NORMAL;
    faPtr->slant        = (fm.fsSelection & FM_SEL_ITALIC) ? TK_FS_ITALIC
                                                           : TK_FS_ROMAN;
    faPtr->underline    = (fm.fsSelection & FM_SEL_UNDERSCORE) ? 1 : 0;
    faPtr->overstrike   = (fm.fsSelection & FM_SEL_STRIKEOUT) ? 1 : 0;
/* Windows port:
    faPtr->overstrike   = overstrike;
*/

    fmPtr               = &fontPtr->font.fm;
    fmPtr->ascent       = fm.lMaxAscender;
    fmPtr->descent      = fm.lMaxDescender;
    fmPtr->maxWidth     = fm.lMaxCharInc;
    fmPtr->fixed        = (fm.fsType & FM_TYPE_FIXED);
    logfonts[hFont].outline = (fm.fsDefn & FM_DEFN_OUTLINE);
    if (logfonts[hFont].outline) {
        /* Scale the values */
#ifdef VERBOSE
        printf("Set scaling %s ascent %d->%d descent %d->%d maxWidth %d->%d\n",
               logfonts[hFont].fattrs.szFacename,
               fmPtr->ascent,
               fmPtr->ascent * logfonts[hFont].deciPoints / 120,
               fmPtr->descent,
               fmPtr->descent * logfonts[hFont].deciPoints / 120,
               fmPtr->maxWidth,
               fmPtr->maxWidth * logfonts[hFont].deciPoints / 120);
#endif
        fmPtr->ascent     *= logfonts[hFont].deciPoints;
        fmPtr->ascent     /= 120;
        fmPtr->descent    *= logfonts[hFont].deciPoints;
        fmPtr->descent    /= 120;
        fmPtr->maxWidth   *= logfonts[hFont].deciPoints;
        fmPtr->maxWidth   /= 120;
    }

    fontPtr->hwnd       = hwnd;
    fontPtr->pixelSize  = fm.lEmHeight;

    fontPtr->numSubFonts        = 1;
    fontPtr->subFontArray       = fontPtr->staticSubFonts;
    InitSubFont(hps, hFont, 1, &fontPtr->subFontArray[0]);

    /* Get widths of first BASE_CHARS characters in current font */
    rc = GpiQueryWidthTable(hps, 0, BASE_CHARS, fontPtr->widths);
#ifdef VERBOSE
    if (rc == TRUE) {
        printf("InitFont: GpiQueryWidthTable OK\n");
    } else {
        printf("InitFont: GpiQueryWidthTable ERROR %x\n",
               WinGetLastError(TclOS2GetHAB()));
    }
#endif
    Tcl_DStringFree(&faceString);

    GpiSetCharSet(hps, oldFont);
    WinReleasePS(hps);
}

/*
 *-------------------------------------------------------------------------
 *
 * ReleaseFont --
 *
 *      Called to release the OS/2-specific contents of a TkFont.
 *      The caller is responsible for freeing the memory used by the
 *      font itself.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed.
 *
 *---------------------------------------------------------------------------
 */

static void
ReleaseFont(
    OS2Font *fontPtr)           /* The font to delete. */
{
    int i;
#ifdef VERBOSE
    printf("ReleaseFont\n");
    fflush(stdout);
#endif

    for (i = 0; i < fontPtr->numSubFonts; i++) {
        ReleaseSubFont(&fontPtr->subFontArray[i]);
    }
    if (fontPtr->subFontArray != fontPtr->staticSubFonts) {
        ckfree((char *) fontPtr->subFontArray);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * InitSubFont --
 *
 *      Wrap a screen font and load the FontFamily that represents
 *      it.  Used to prepare a SubFont so that characters can be mapped
 *      from UTF-8 to the charset of the font.
 *
 * Results:
 *      The subFontPtr is filled with information about the font.
 *
 * Side effects:
 *      None.
 *
 *-------------------------------------------------------------------------
 */

static void
InitSubFont(
    HPS hps,                    /* HPS in which font can be selected. */
    LONG hFont,                 /* The screen font. */
    int base,                   /* Non-zero if this SubFont is being used
                                 * as the base font for a font object. */
    SubFont *subFontPtr)        /* Filled with SubFont constructed from
                                 * above attributes. */
{
#ifdef VERBOSE
    printf("InitSubFont hps %x, hFont %d, base %d\n", hps, hFont, base);
#endif
    subFontPtr->hFont       = hFont;
    subFontPtr->hps         = hps;
    subFontPtr->familyPtr   = AllocFontFamily(hps, hFont, base);
    subFontPtr->fontMap     = subFontPtr->familyPtr->fontMap;
}

/*
 *-------------------------------------------------------------------------
 *
 * ReleaseSubFont --
 *
 *      Called to release the contents of a SubFont.  The caller is
 *      responsible for freeing the memory used by the SubFont itself.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory and resources are freed.
 *
 *---------------------------------------------------------------------------
 */

static void
ReleaseSubFont(
    SubFont *subFontPtr)        /* The SubFont to delete. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("ReleaseSubFont, subFontPtr->hps %x\n", subFontPtr->hps);
    fflush(stdout);
#endif
    
    if (GpiQueryCharSet(subFontPtr->hps) == subFontPtr->hFont) {
        rc = GpiSetCharSet(subFontPtr->hps, LCID_DEFAULT);
#ifdef VERBOSE
        if (rc==TRUE) {
            printf("GpiSetCharSet (%x, default) OK\n", subFontPtr->hps);
        } else {
            printf("GpiSetCharSet (%x, default) ERROR, error %x\n",
                   subFontPtr->hps, WinGetLastError(TclOS2GetHAB()));
        }
#endif
    }
    rc = GpiDeleteSetId(subFontPtr->hps, subFontPtr->hFont);
#ifdef VERBOSE
    if (rc==TRUE) {
        printf("GpiDeleteSetId (%x, id %d) OK\n", subFontPtr->hps,
               subFontPtr->hFont);
    } else {
        printf("GpiDeleteSetId (%x, id %d) ERROR, error %x\n", subFontPtr->hps,
               subFontPtr->hFont, WinGetLastError(TclOS2GetHAB()));
    }
#endif

    if (subFontPtr->hFont == nextLogicalFont - 1) {
        nextLogicalFont--;
    }
    FreeFontFamily(subFontPtr->familyPtr);
}

/*
 *-------------------------------------------------------------------------
 *
 * AllocFontFamily --
 *
 *      Find the FontFamily structure associated with the given font
 *      name.  The information should be stored by the caller in a
 *      SubFont and used when determining if that SubFont supports a
 *      character.
 *
 *      Cannot use the string name used to construct the font as the
 *      key, because the capitalization may not be canonical.  Therefore
 *      use the face name actually retrieved from the font metrics as
 *      the key.
 *
 * Results:
 *      A pointer to a FontFamily.  The reference count in the FontFamily
 *      is automatically incremented.  When the SubFont is released, the
 *      reference count is decremented.  When no SubFont is using this
 *      FontFamily, it may be deleted.
 *
 * Side effects:
 *      A new FontFamily structure will be allocated if this font family
 *      has not been seen.
 *
 *-------------------------------------------------------------------------
 */

static FontFamily *
AllocFontFamily(
    HPS hps,                    /* HPS in which font can be selected. */
    LONG hFont,                 /* Screen font whose FontFamily is to be
                                 * returned. */
    int base)                   /* Non-zero if this font family is to be
                                 * used in the base font of a font object. */
{
    Tk_Uid              faceName;
    FontFamily          *familyPtr;
    Tcl_DString         faceString;
    Tcl_Encoding        encoding;
    FONTMETRICS         fm;
    APIRET              rc;
    ThreadSpecificData  *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("AllocFontFamily\n");
    fflush(stdout);
#endif

    TkOS2SelectFont(hps, hFont);

    rc = GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm);

    if (fm.fsType & FM_TYPE_FACETRUNC) {
        char fullName[MAX_FLEN];
        rc = WinQueryAtomName(WinQuerySystemAtomTable(),
                              fm.FaceNameAtom, (PSZ)&fullName,
                              MAX_FLEN);
        if (rc != 0) {
#ifdef VERBOSE
            printf("WinQueryAtomName OK: %s\n", fullName);
#endif
            if (rc >= MAX_FLEN) {
                fullName[MAX_FLEN] = '\0';
            }
            Tcl_ExternalToUtfDString(systemEncoding, fullName, -1, &faceString);
        } else {
            Tcl_ExternalToUtfDString(systemEncoding, fm.szFacename, -1,
                                     &faceString);
        }
    } else {
        Tcl_ExternalToUtfDString(systemEncoding, fm.szFacename, -1,
                                 &faceString);
    }

    faceName = Tk_GetUid(Tcl_DStringValue(&faceString));
    Tcl_DStringFree(&faceString);

    familyPtr = tsdPtr->fontFamilyList;
    for ( ; familyPtr != NULL; familyPtr = familyPtr->nextPtr) {
        if (familyPtr->faceName == faceName) {
            familyPtr->refCount++;
            return familyPtr;
        }
    }

    familyPtr = (FontFamily *) ckalloc(sizeof(FontFamily));
    memset(familyPtr, 0, sizeof(FontFamily));
    familyPtr->nextPtr = tsdPtr->fontFamilyList;
    tsdPtr->fontFamilyList = familyPtr;

    /*
     * Set key for this FontFamily.
     */

    familyPtr->faceName = faceName;

    /*
     * An initial refCount of 2 means that FontFamily information will
     * persist even when the SubFont that loaded the FontFamily is released.
     * Change it to 1 to cause FontFamilies to be unloaded when not in use.
     */

    familyPtr->refCount = 2;

    encoding = NULL;
    if (familyPtr->isSymbolFont != 0) {
        /*
         * Symbol fonts are handled specially.  For instance, Unicode 0393
         * (GREEK CAPITAL GAMMA) must be mapped to Symbol character 0047
         * (GREEK CAPITAL GAMMA), because the Symbol font doesn't have a
         * GREEK CAPITAL GAMMA at location 0393.  If Tk interpreted the
         * Symbol font using the Unicode encoding, it would decide that
         * the Symbol font has no GREEK CAPITAL GAMMA, because the Symbol
         * encoding (of course) reports that character 0393 doesn't exist.
         *
         * With non-symbol Windows fonts, such as Times New Roman, if the
         * font has a GREEK CAPITAL GAMMA, it will be found in the correct
         * Unicode location (0393); the GREEK CAPITAL GAMMA will not be off
         * hiding at some other location.
         */

        encoding = Tcl_GetEncoding(NULL, faceName);
    }

    familyPtr->encoding = encoding;

    return familyPtr;
}

/*
 *-------------------------------------------------------------------------
 *
 * FreeFontFamily --
 *
 *      Called to free a FontFamily when the SubFont is finished using it.
 *      Frees the contents of the FontFamily and the memory used by the
 *      FontFamily itself.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-------------------------------------------------------------------------
 */

static void
FreeFontFamily(
    FontFamily *familyPtr)      /* The FontFamily to delete. */
{
    int i;
    FontFamily **familyPtrPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("FreeFontFamily\n");
    fflush(stdout);
#endif

    if (familyPtr == NULL) {
        return;
    }
    familyPtr->refCount--;
    if (familyPtr->refCount > 0) {
        return;
    }
    for (i = 0; i < FONTMAP_PAGES; i++) {
        if (familyPtr->fontMap[i] != NULL) {
            ckfree(familyPtr->fontMap[i]);
        }
    }
    if (familyPtr->encoding != unicodeEncoding) {
        Tcl_FreeEncoding(familyPtr->encoding);
    }

    /*
     * Delete from list.
     */

    for (familyPtrPtr = &tsdPtr->fontFamilyList; ; ) {
        if (*familyPtrPtr == familyPtr) {
            *familyPtrPtr = familyPtr->nextPtr;
            break;
        }
        familyPtrPtr = &(*familyPtrPtr)->nextPtr;
    }

    ckfree((char *) familyPtr);
}

/*
 *-------------------------------------------------------------------------
 *
 * FindSubFontForChar --
 *
 *      Determine which screen font is necessary to use to display the
 *      given character.  If the font object does not have a screen font
 *      that can display the character, another screen font may be loaded
 *      into the font object, following a set of preferred fallback rules.
 *
 * Results:
 *      The return value is the SubFont to use to display the given
 *      character.
 *
 * Side effects:
 *      The contents of fontPtr are modified to cache the results
 *      of the lookup and remember any SubFonts that were dynamically
 *      loaded.
 *
 *-------------------------------------------------------------------------
 */

static SubFont *
FindSubFontForChar(
    OS2Font *fontPtr,           /* The font object with which the character
                                 * will be displayed. */
    int ch)                     /* The Unicode character to be displayed. */
{
    HPS hps;
    int i, j, k;
    LONG reqFonts, remFonts;
    PFONTMETRICS os2fonts;
    char **aliases, **anyFallbacks;
    char ***fontFallbacks;
    char *fallbackName;
    SubFont *subFontPtr;
    Tcl_DString ds;
    Tcl_DString faceString;
#ifdef VERBOSE
    printf("FindSubFontForChar\n");
    fflush(stdout);
#endif

    if (ch < BASE_CHARS) {
        return &fontPtr->subFontArray[0];
    }

    for (i = 0; i < fontPtr->numSubFonts; i++) {
        if (FontMapLookup(&fontPtr->subFontArray[i], ch)) {
            return &fontPtr->subFontArray[i];
        }
    }

    /*
     * Keep track of all face names that we check, so we don't check some
     * name multiple times if it can be reached by multiple paths.
     */

    Tcl_DStringInit(&ds);
    hps = WinGetPS(fontPtr->hwnd);

    aliases = TkFontGetAliasList(fontPtr->font.fa.family);

    fontFallbacks = TkFontGetFallbacks();
    for (i = 0; fontFallbacks[i] != NULL; i++) {
        for (j = 0; fontFallbacks[i][j] != NULL; j++) {
            fallbackName = fontFallbacks[i][j];
            if (strcasecmp(fallbackName, fontPtr->font.fa.family) == 0) {
                /*
                 * If the base font has a fallback...
                 */

                goto tryfallbacks;
            } else if (aliases != NULL) {
                /*
                 * Or if an alias for the base font has a fallback...
                 */

                for (k = 0; aliases[k] != NULL; k++) {
                    if (strcasecmp(aliases[k], fallbackName) == 0) {
                        goto tryfallbacks;
                    }
                }
            }
        }
        continue;

        /*
         * ...then see if we can use one of the fallbacks, or an
         * alias for one of the fallbacks.
         */

        tryfallbacks:
        for (j = 0; fontFallbacks[i][j] != NULL; j++) {
            fallbackName = fontFallbacks[i][j];
            subFontPtr = CanUseFallbackWithAliases(hps, fontPtr, fallbackName,
                    ch, &ds);
            if (subFontPtr != NULL) {
                goto end;
            }
        }
    }

    /*
     * See if we can use something from the global fallback list.
     */

    anyFallbacks = TkFontGetGlobalClass();
    for (i = 0; anyFallbacks[i] != NULL; i++) {
        fallbackName = anyFallbacks[i];
        subFontPtr = CanUseFallbackWithAliases(hps, fontPtr, fallbackName,
                ch, &ds);
        if (subFontPtr != NULL) {
            goto end;
        }
    }

    /*
     * Try all face names available in the whole system until we
     * find one that can be used.
     */

    /* Determine total number of fonts */
    reqFonts = 0L;
    remFonts = GpiQueryFonts(hps, QF_PUBLIC, NULL, &reqFonts,
                             (LONG) sizeof(FONTMETRICS), NULL);
#ifdef VERBOSE
    printf("FindSubFontForChar nr.of fonts: %ld\n", remFonts);
#endif
    /* Allocate space for the fonts */
    os2fonts = (PFONTMETRICS) ckalloc(remFonts * sizeof(FONTMETRICS));
    if (os2fonts == NULL) {
        goto end;
    }
    /* Retrieve the fonts */
    reqFonts = remFonts;
    remFonts = GpiQueryFonts(hps, QF_PUBLIC, NULL, &reqFonts,
                             (LONG) sizeof(FONTMETRICS), os2fonts);
#ifdef VERBOSE
    printf("    got %d (%d remaining)\n", reqFonts, remFonts);
#endif
    for (i = 0; i < reqFonts; i++) {
#ifdef VERBOSE
        printf("m%d Em%d %ddpt lMBE%d %dx%d %s %s face[%s]%s fam[%s]%s\n",
              os2fonts[i].lMatch, os2fonts[i].lEmHeight,
              os2fonts[i].sNominalPointSize, os2fonts[i].lMaxBaselineExt,
              os2fonts[i].sXDeviceRes, os2fonts[i].sYDeviceRes,
              (os2fonts[i].fsType & FM_TYPE_FIXED) ? "fix" : "prop",
              (os2fonts[i].fsDefn & FM_DEFN_OUTLINE) ? "outl" : "bmp",
              os2fonts[i].szFacename,
              (os2fonts[i].fsType & FM_TYPE_FACETRUNC) ? " (trunc)" : "",
              os2fonts[i].szFamilyname,
              (os2fonts[i].fsType & FM_TYPE_FAMTRUNC) ? " (trunc)" : "");
#endif
        fallbackName = os2fonts[i].szFacename;
        Tcl_ExternalToUtfDString(systemEncoding, fallbackName, -1, &faceString);
        fallbackName = Tcl_DStringValue(&faceString);

        if (SeenName(fallbackName, &ds) == 0) {
            subFontPtr = CanUseFallback(hps, fontPtr, fallbackName, ch);
            if (subFontPtr != NULL) {
                ckfree((char *)os2fonts);
                Tcl_DStringFree(&faceString);
                goto end;
            }
        }
        Tcl_DStringFree(&faceString);
    }
    ckfree((char *)os2fonts);

    end:
    Tcl_DStringFree(&ds);

    if (subFontPtr == NULL) {
        /*
         * No font can display this character.  We will use the base font
         * and have it display the "unknown" character.
         */

        subFontPtr = &fontPtr->subFontArray[0];
        FontMapInsert(subFontPtr, ch);
    }
    WinReleasePS(hps);
    return subFontPtr;
}

/*
 *-------------------------------------------------------------------------
 *
 * FontMapLookup --
 *
 *      See if the screen font can display the given character.
 *
 * Results:
 *      The return value is 0 if the screen font cannot display the
 *      character, non-zero otherwise.
 *
 * Side effects:
 *      New pages are added to the font mapping cache whenever the
 *      character belongs to a page that hasn't been seen before.
 *      When a page is loaded, information about all the characters on
 *      that page is stored, not just for the single character in
 *      question.
 *
 *-------------------------------------------------------------------------
 */
static int
FontMapLookup(
    SubFont *subFontPtr,        /* Contains font mapping cache to be queried
                                 * and possibly updated. */
    int ch)                     /* Character to be tested. */
{
    int row, bitOffset;
#ifdef VERBOSE
    printf("FontMapLookup\n");
    fflush(stdout);
#endif

    row = ch >> FONTMAP_SHIFT;
    if (subFontPtr->fontMap[row] == NULL) {
        FontMapLoadPage(subFontPtr, row);
    }
    bitOffset = ch & (FONTMAP_BITSPERPAGE - 1);
    return (subFontPtr->fontMap[row][bitOffset >> 3] >> (bitOffset & 7)) & 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * FontMapInsert --
 *
 *      Tell the font mapping cache that the given screen font should be
 *      used to display the specified character.  This is called when no
 *      font on the system can be be found that can display that
 *      character; we lie to the font and tell it that it can display
 *      the character, otherwise we would end up re-searching the entire
 *      fallback hierarchy every time that character was seen.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      New pages are added to the font mapping cache whenever the
 *      character belongs to a page that hasn't been seen before.
 *      When a page is loaded, information about all the characters on
 *      that page is stored, not just for the single character in
 *      question.
 *
 *-------------------------------------------------------------------------
 */

static void
FontMapInsert(
    SubFont *subFontPtr,        /* Contains font mapping cache to be
                                 * updated. */
    int ch)                     /* Character to be added to cache. */
{
    int row, bitOffset;
#ifdef VERBOSE
    printf("FontMapInsert\n");
    fflush(stdout);
#endif

    row = ch >> FONTMAP_SHIFT;
    if (subFontPtr->fontMap[row] == NULL) {
        FontMapLoadPage(subFontPtr, row);
    }
    bitOffset = ch & (FONTMAP_BITSPERPAGE - 1);
    subFontPtr->fontMap[row][bitOffset >> 3] |= 1 << (bitOffset & 7);
}

/*
 *-------------------------------------------------------------------------
 *
 * FontMapLoadPage --
 *
 *      Load information about all the characters on a given page.
 *      This information consists of one bit per character that indicates
 *      whether the associated LONG can (1) or cannot (0) display the
 *      characters on the page.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Mempry allocated.
 *
 *-------------------------------------------------------------------------
 */
static void
FontMapLoadPage(
    SubFont *subFontPtr,        /* Contains font mapping cache to be
                                 * updated. */
    int row)                    /* Index of the page to be loaded into
                                 * the cache. */
{
    Tcl_Encoding encoding;
    char src[TCL_UTF_MAX], buf[16];
    int i, j, bitOffset, end, srcRead, dstWrote;
#ifdef VERBOSE
    printf("FontMapLoadPage\n");
    fflush(stdout);
#endif

    subFontPtr->fontMap[row] = (char *) ckalloc(FONTMAP_BITSPERPAGE / 8);
    memset(subFontPtr->fontMap[row], 0, FONTMAP_BITSPERPAGE / 8);

    encoding = subFontPtr->familyPtr->encoding;

    /*
     * Let's just assume *ALL* the characters are allowed.
     */

    end = (row + 1) << FONTMAP_SHIFT;
    for (i = row << FONTMAP_SHIFT; i < end; i++) {
        if (Tcl_UtfToExternal(NULL, encoding, src, Tcl_UniCharToUtf(i, src),
                TCL_ENCODING_STOPONERROR, NULL, (char *) buf,
                sizeof(buf),
                &srcRead, &dstWrote, NULL) == TCL_OK) {
            bitOffset = i & (FONTMAP_BITSPERPAGE - 1);
            subFontPtr->fontMap[row][bitOffset >> 3] |= 1 << (bitOffset & 7);
        }
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * CanUseFallbackWithAliases --
 *
 *      Helper function for FindSubFontForChar.  Determine if the
 *      specified face name (or an alias of the specified face name)
 *      can be used to construct a screen font that can display the
 *      given character.
 *
 * Results:
 *      See CanUseFallback().
 *
 * Side effects:
 *      If the name and/or one of its aliases was rejected, the
 *      rejected string is recorded in nameTriedPtr so that it won't
 *      be tried again.
 *
 *---------------------------------------------------------------------------
 */

static SubFont *
CanUseFallbackWithAliases(
    HPS hps,                    /* HPS in which font can be selected. */
    OS2Font *fontPtr,           /* The font object that will own the new
                                 * screen font. */
    char *faceName,             /* Desired face name for new screen font. */
    int ch,                     /* The Unicode character that the new
                                 * screen font must be able to display. */
    Tcl_DString *nameTriedPtr)  /* Records face names that have already
                                 * been tried.  It is possible for the same
                                 * face name to be queried multiple times when
                                 * trying to find a suitable screen font. */
{
    int i;
    char **aliases;
    SubFont *subFontPtr;
#ifdef VERBOSE
    printf("CanUseFallbackWithAliases\n");
    fflush(stdout);
#endif

    if (SeenName(faceName, nameTriedPtr) == 0) {
        subFontPtr = CanUseFallback(hps, fontPtr, faceName, ch);
        if (subFontPtr != NULL) {
            return subFontPtr;
        }
    }
    aliases = TkFontGetAliasList(faceName);
    if (aliases != NULL) {
        for (i = 0; aliases[i] != NULL; i++) {
            if (SeenName(aliases[i], nameTriedPtr) == 0) {
                subFontPtr = CanUseFallback(hps, fontPtr, aliases[i], ch);
                if (subFontPtr != NULL) {
                    return subFontPtr;
                }
            }
        }
    }
    return NULL;
}

/*
 *---------------------------------------------------------------------------
 *
 * SeenName --
 *
 *      Used to determine we have already tried and rejected the given
 *      face name when looking for a screen font that can support some
 *      Unicode character.
 *
 * Results:
 *      The return value is 0 if this face name has not already been seen,
 *      non-zero otherwise.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

static int
SeenName(
    CONST char *name,           /* The name to check. */
    Tcl_DString *dsPtr)         /* Contains names that have already been
                                 * seen. */
{
    CONST char *seen, *end;
#ifdef VERBOSE
    printf("SeenName\n");
    fflush(stdout);
#endif

    seen = Tcl_DStringValue(dsPtr);
    end = seen + Tcl_DStringLength(dsPtr);
    while (seen < end) {
        if (strcasecmp(seen, name) == 0) {
            return 1;
        }
        seen += strlen(seen) + 1;
    }
    Tcl_DStringAppend(dsPtr, (char *) name, (int) (strlen(name) + 1));
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * CanUseFallback --
 *
 *      If the specified screen font has not already been loaded into
 *      the font object, determine if it can display the given character.
 *
 * Results:
 *      The return value is a pointer to a newly allocated SubFont, owned
 *      by the font object.  This SubFont can be used to display the given
 *      character.  The SubFont represents the screen font with the base set
 *      of font attributes from the font object, but using the specified
 *      font name.  NULL is returned if the font object already holds
 *      a reference to the specified physical font or if the specified
 *      physical font cannot display the given character.
 *
 * Side effects:
 *      The font object's subFontArray is updated to contain a reference
 *      to the newly allocated SubFont.
 *
 *-------------------------------------------------------------------------
 */

static SubFont *
CanUseFallback(
    HPS hps,                    /* HPS in which font can be selected. */
    OS2Font *fontPtr,           /* The font object that will own the new
                                 * screen font. */
    char *faceName,             /* Desired face name for new screen font. */
    int ch)                     /* The Unicode character that the new
                                 * screen font must be able to display. */
{
    int i;
    LONG hFont;
    SubFont subFont;
#ifdef VERBOSE
    printf("CanUseFallback\n");
    fflush(stdout);
#endif

    if (FamilyExists(hps, faceName) == 0) {
        return NULL;
    }

    /*
     * Skip all fonts we've already used.
     */

    for (i = 0; i < fontPtr->numSubFonts; i++) {
        if (faceName == fontPtr->subFontArray[i].familyPtr->faceName) {
            return NULL;
        }
    }

    /*
     * Load this font and see if it has the desired character.
     */

    hFont = GetScreenFont(hps, &fontPtr->font.fa, faceName, fontPtr->pixelSize);
    InitSubFont(hps, hFont, 0, &subFont);
    if (((ch < 256) && (subFont.familyPtr->isSymbolFont))
            || (FontMapLookup(&subFont, ch) == 0)) {
        /*
         * Don't use a symbol font as a fallback font for characters below
         * 256.
         */

        ReleaseSubFont(&subFont);
        return NULL;
    }

    if (fontPtr->numSubFonts >= SUBFONT_SPACE) {
        SubFont *newPtr;

        newPtr = (SubFont *) ckalloc(sizeof(SubFont)
                * (fontPtr->numSubFonts + 1));
        memcpy((char *) newPtr, fontPtr->subFontArray,
                fontPtr->numSubFonts * sizeof(SubFont));
        if (fontPtr->subFontArray != fontPtr->staticSubFonts) {
            ckfree((char *) fontPtr->subFontArray);
        }
        fontPtr->subFontArray = newPtr;
    }
    fontPtr->subFontArray[fontPtr->numSubFonts] = subFont;
    fontPtr->numSubFonts++;
    return &fontPtr->subFontArray[fontPtr->numSubFonts - 1];
}

/*
 *---------------------------------------------------------------------------
 *
 * GetScreenFont --
 *
 *      Given the name and other attributes, determine a font and ID.
 *      This is where all the alias and fallback substitution bottoms
 *      out.
 *
 * Results:
 *      The screen font that corresponds to the attributes.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

static LONG
GetScreenFont(
    HPS hps,                    /* Presentation space for font */
    CONST TkFontAttributes *faPtr,
                                /* Desired font attributes for new font. */
    CONST char *faceName,       /* Overrides font family specified in font
                                 * attributes. */
    int pixelSize)              /* Overrides size specified in font
                                 * attributes. */
{
    Tcl_DString ds;
    int deciPoints;
    char *fontName;
    LONG match;
    TkFont *tkFont;
    BOOL useIntended = FALSE;
    LONG reqFonts, remFonts;
    int faceLen = 0;
    PFONTMETRICS os2fonts;
    BOOL found = FALSE;
    LONG outline = -1;
    int i, error = 30000, best = -1;
    LONG font = 0;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    LONG hFont = nextLogicalFont;
#ifdef VERBOSE
    printf("GetScreenFont, hps %x faPtr %x faceName [%s], pixelSize %d\n", hps,
           faPtr, faceName, pixelSize);
    fflush(stdout);
#endif

    if (hFont > MAX_LID) {
        /* We can't simultaneously  use more than MAX_LID fonts */
#ifdef VERBOSE
        printf("    => too many font IDs\n");
        fflush(stdout);
#endif
        return 0;
    }

    logfonts[hFont].fattrs.usRecordLength = (USHORT)sizeof(FATTRS);
    if (faPtr->slant != TK_FW_NORMAL) {
        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_BOLD;
#ifdef VERBOSE
        printf("FATTR_SEL_ITALIC\n");
#endif
    }
    /* If the name already contains "Italic" then don't specify that */
    if (faPtr->slant == TK_FS_ITALIC) {
        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_ITALIC;
#ifdef VERBOSE
        printf("FATTR_SEL_ITALIC\n");
#endif
    }
    if (faPtr->underline != 0) {
        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_UNDERSCORE;
#ifdef VERBOSE
        printf("FATTR_SEL_UNDERSCORE\n");
#endif
    }
    if (faPtr->overstrike != 0) {
        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_STRIKEOUT;
#ifdef VERBOSE
        printf("FATTR_SEL_STRIKEOUT\n");
#endif
    }
    logfonts[hFont].fattrs.lMatch           = 0L;
    Tcl_UtfToExternalDString(systemEncoding, faceName, -1, &ds);
    /*
     * We can only store up to FACESIZE characters
     */
    if (Tcl_DStringLength(&ds) >= FACESIZE) {
        Tcl_DStringSetLength(&ds, FACESIZE);
    }
    strcpy(logfonts[hFont].fattrs.szFacename, Tcl_DStringValue(&ds));
    logfonts[hFont].fattrs.idRegistry = 0; /* Unknown */
    logfonts[hFont].fattrs.usCodePage = 0; /* Use present codepage */
    logfonts[hFont].fattrs.lMaxBaselineExt = pixelSize;
    logfonts[hFont].fattrs.lAveCharWidth = 0L;
    logfonts[hFont].fattrs.fsType = 0;
    logfonts[hFont].fattrs.fsFontUse = 0;

    /*
     * Replace the standard X, Mac and Windows family names with the names that
     * OS/2 likes.
     */

    if ((stricmp(logfonts[hFont].fattrs.szFacename, "Times") == 0)
    || (stricmp(logfonts[hFont].fattrs.szFacename, "New York") == 0)) {
        strcpy(logfonts[hFont].fattrs.szFacename, "Times New Roman");
    } else if ((stricmp(logfonts[hFont].fattrs.szFacename,
                "Courier New") == 0)
           || (stricmp(logfonts[hFont].fattrs.szFacename,
               "Monaco") == 0)) {
        strcpy(logfonts[hFont].fattrs.szFacename, "Courier");
    } else if ((stricmp(logfonts[hFont].fattrs.szFacename,
                "Arial") == 0)
           || (stricmp(logfonts[hFont].fattrs.szFacename,
               "Geneva") == 0)) {
        strcpy(logfonts[hFont].fattrs.szFacename, "Helvetica");
    } else {
        /*
         * The following code suggested by Ilya Zakharevich.
         * Its use is to allow font selection "in OS/2-style", like
         * "10.Courier".
         * Ilya's way of supplying attributes of the font is against
         * the documented "pointSize.Fontname[.attr ...]" though,
         * because it gives attributes between the pointsize and the
         * name of the font.
         * I take the "official" stance and also supply the rest of the
         * font Presentation Parameters: underline, strikeout, outline.
         */
        int l, off = 0;
        char *name = Tcl_DStringValue(&ds);

        if (name != NULL && sscanf(name, "%d.%n", &l, &off) && off > 0) {
            int fields;
#ifdef VERBOSE
            printf("    trying Presentation Parameters-notation font\n");
            printf("    d %d, n %d\n", l, off);
            fflush(stdout);
#endif
            logfonts[hFont].fattrs.lMaxBaselineExt = POINTTOPIX(l);
            logfonts[hFont].deciPoints = l * 10;
            /*
            logfonts[hFont].fattrs.lMaxBaselineExt = l;
            logfonts[hFont].deciPoints = PIXTOPOINT(l * 10);
            */
            name += off;
            useIntended = TRUE;
            /* Get the fontname out */
            fields = sscanf(name, "%[^.]%n",
                            (char *)&logfonts[hFont].fattrs.szFacename, &off);
#ifdef VERBOSE
            printf("    sscanf returns %d, off %d\n", fields, off);
            fflush(stdout);
#endif
            if (fields==1 && strlen(name)==off) {
                /* Fontname is last part */
                l = strlen(name);
                if (l > FACESIZE - 1) {
                    l = FACESIZE - 1;
                }
                strncpy(logfonts[hFont].fattrs.szFacename, name, l);
#ifdef VERBOSE
                printf("    font [%s] last part\n", name);
                fflush(stdout);
#endif
            } else {
#ifdef VERBOSE
                printf("    decomposing [%s]\n", name);
                fflush(stdout);
#endif
                /* There are attributes after the fontname */
                name += off;
                while (TRUE) {
                    if (strnicmp(name, ".bold", 5) == 0) {
                        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_BOLD;
#ifdef VERBOSE
                        printf("    .bold -> FATTR_SEL_BOLD\n");
                        fflush(stdout);
#endif
                        name += 5;
                    } else if (strnicmp(name, ".italic", 7) == 0) {
                        logfonts[hFont].fattrs.fsSelection
                                         |= FATTR_SEL_ITALIC;
#ifdef VERBOSE
                        printf("    .italic -> FATTR_SEL_ITALIC\n");
                        fflush(stdout);
#endif
                        name += 7;
                    } else if (strnicmp(name, ".underline", 10) == 0) {
                        logfonts[hFont].fattrs.fsSelection
                                         |= FATTR_SEL_UNDERSCORE;
#ifdef VERBOSE
                        printf("    .underline -> FATTR_SEL_UNDERSCORE\n");
                        fflush(stdout);
#endif
                        name += 10;
                    } else if (strnicmp(name, ".strikeout", 10) == 0) {
                        logfonts[hFont].fattrs.fsSelection
                                         |= FATTR_SEL_STRIKEOUT;
#ifdef VERBOSE
                        printf("    .strikeout -> FATTR_SEL_STRIKEOUT\n");
                        fflush(stdout);
#endif
                        name += 10;
                    } else if (strnicmp(name, ".outline", 8) == 0) {
                        logfonts[hFont].fattrs.fsSelection
                                         |= FATTR_SEL_OUTLINE;
#ifdef VERBOSE
                        printf("    .outline -> FATTR_SEL_OUTLINE\n");
                        fflush(stdout);
#endif
                        name += 8;
                    } else if (*name == '.') {
                        name++;
                        break;
                    } else {
                        break;
                    }
                }
            }
        } else if (name != NULL) {
#ifdef VERBOSE
            printf("    non Presentation Parameters-notation font\n");
            fflush(stdout);
#endif
        }
    }
    Tcl_DStringFree(&ds);
    /*
     * If we have to use an outline font (instead of a bitmap font) that we
     * ask for with an unqualified name (eg. Courier) and specifying we want
     * a bold or italic font, then we can get the situation where we select
     * the normal font because it comes before the bold version (eg. Courier
     * outline has match 46 on my system, while Courier Bold has 47), and
     * selecting the non-bold version with FATTR_SEL_BOLD doesn't give us the
     * bold version. Hoping for standard specifications in normal fonts, I'll
     * add " Bold" and/or " Italic" here if they're not already in the name.
     */
#ifdef VERBOSE
    printf("    general part\n");
    fflush(stdout);
#endif
    if ((logfonts[hFont].fattrs.fsSelection & FATTR_SEL_BOLD) &&
        strstr(logfonts[hFont].fattrs.szFacename, "Bold") == NULL) {
        strncat(logfonts[hFont].fattrs.szFacename, " Bold",
                FACESIZE - 1 - strlen(logfonts[hFont].fattrs.szFacename));
    }
    if ((logfonts[hFont].fattrs.fsSelection & FATTR_SEL_ITALIC) &&
        strstr(logfonts[hFont].fattrs.szFacename, "Italic") == NULL) {
        strncat(logfonts[hFont].fattrs.szFacename, " Italic",
                FACESIZE - 1 - strlen(logfonts[hFont].fattrs.szFacename));
    }
#ifdef VERBOSE
    printf("  trying font [%s]\n", logfonts[hFont].fattrs.szFacename);
#endif

    /* Name has now been filled in with a correct or sane value */
    /* Determine number of fonts */
    reqFonts = 0L;
    remFonts = GpiQueryFonts(hps, QF_PUBLIC,
                             logfonts[hFont].fattrs.szFacename,
                             &reqFonts, (LONG) sizeof(FONTMETRICS), NULL);
#ifdef VERBOSE
    if (remFonts == GPI_ALTERROR) {
        printf("GpiQueryFonts hps %x face [%s] ERROR %x\n", hps,
               logfonts[hFont].fattrs.szFacename,
               WinGetLastError(TclOS2GetHAB()));
        return 0;
    }
    printf("    nr.of fonts: %d\n", remFonts);
#endif
    reqFonts = remFonts;
    if (reqFonts) {
        /* Allocate space for the fonts */
        os2fonts = (PFONTMETRICS) ckalloc(remFonts * sizeof(FONTMETRICS));
        if (os2fonts == NULL) {
            return 0;
        }
        /* Get the fonts that apply */
        remFonts = GpiQueryFonts(hps, QF_PUBLIC,
                                 logfonts[hFont].fattrs.szFacename, &reqFonts,
                                 (LONG) sizeof(FONTMETRICS), os2fonts);
#ifdef VERBOSE
        if (remFonts == GPI_ALTERROR) {
            printf("    GpiQueryFonts %s ERROR %x\n",
                   logfonts[hFont].fattrs.szFacename,
            WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("    nr.of fonts [%s]: %d (%d remaining)\n",
                   logfonts[hFont].fattrs.szFacename,
                   reqFonts, remFonts);
        }
#endif
    } else {
        os2fonts = NULL;
    }
    /*
     * Determine the one that has the right size, preferring a bitmap font over
     * a scalable (outline) one if it exists.
     */
    for (i=0; i<reqFonts && !found; i++) {
        /*
         * Note: scalable fonts appear to always return lEmHeight 16, so first
         * check for outline, then "point size" to not match on size 16.
         */
#ifdef VERBOSE
        printf("    trying %s font %s (%ddp, lMaxBaselineExt %d), match %d\n",
               (os2fonts[i].fsDefn & FM_DEFN_OUTLINE) ? "outline" : "fixed",
               os2fonts[i].szFacename, os2fonts[i].sNominalPointSize,
               os2fonts[i].lMaxBaselineExt, os2fonts[i].lMatch);
#endif
        if (os2fonts[i].fsDefn & FM_DEFN_OUTLINE) {
            /* Remember we found an outline font */
            outline = i;
#ifdef VERBOSE
            printf("    found outline font %s, match %d\n",
                   os2fonts[i].szFacename, os2fonts[i].lMatch);
#endif
        } else {
            /* Bitmap font, check size, type, resolution */
            int cerror = 0, err1;

            /*
             * Note: FONTMETRICS.fsSelection can contain FM_SEL_ISO9241_TESTED,
             * FATTRS.fsSelection cannot.
             */
#ifdef VERBOSE
        printf("m%d Em%d %ddpt lMBE%d xR%d yR%d %s %s face[%s]%s fam[%s]%s\n",
              os2fonts[i].lMatch, os2fonts[i].lEmHeight,
              os2fonts[i].sNominalPointSize, os2fonts[i].lMaxBaselineExt,
              os2fonts[i].sXDeviceRes, os2fonts[i].sYDeviceRes,
              (os2fonts[i].fsType & FM_TYPE_FIXED) ? "fix" : "prop",
              (os2fonts[i].fsDefn & FM_DEFN_OUTLINE) ? "outl" : "bmp",
              os2fonts[i].szFacename,
              (os2fonts[i].fsType & FM_TYPE_FACETRUNC) ? " (trunc)" : "",
              os2fonts[i].szFamilyname,
              (os2fonts[i].fsType & FM_TYPE_FAMTRUNC) ? " (trunc)" : "");
#endif
/*
            if (
                ((useIntended ? os2fonts[i].sNominalPointSize :
                                (os2fonts[i].lMaxBaselineExt * 10)) ==
                       logfonts[hFont].fattrs.lMaxBaselineExt * 10)
*/
            /* If we need a transformable font, we *need* an outline */
/*
                &&
        (!(logfonts[hFont].fattrs.fsFontUse & FATTR_FONTUSE_TRANSFORMABLE))
                &&
                (os2fonts[i].sXDeviceRes == aDevCaps[CAPS_HORIZONTAL_FONT_RES])
                &&
                (os2fonts[i].sYDeviceRes == aDevCaps[CAPS_VERTICAL_FONT_RES])
                ) {
                found = TRUE;
                match = os2fonts[i].lMatch;
                font = i;
*/
#ifdef VERBOSE
            printf("    useInt %d, os2f.sNom %d, os2f.lMBE %d, tsdPtr->logf.lMBE %d\n",
              useIntended, os2fonts[i].sNominalPointSize,
              os2fonts[i].lMaxBaselineExt * 10,
              logfonts[hFont].fattrs.lMaxBaselineExt * 10);
#endif
            err1 = ( useIntended
                     ? os2fonts[i].sNominalPointSize
                     : (os2fonts[i].lMaxBaselineExt * 10))
                       - logfonts[hFont].fattrs.lMaxBaselineExt * 10;
            if (err1 < 0) {
                err1 = -err1;
            }
            cerror = err1;
            if (logfonts[hFont].fattrs.lAveCharWidth) {
                err1 = logfonts[hFont].fattrs.lAveCharWidth
                       - os2fonts[i].lAveCharWidth;
                if (err1 < 0) {
                    err1 = -err1;
                }
                cerror += err1 * 3;     /* 10/3 times cheaper */
            }
            if (os2fonts[i].sXDeviceRes != aDevCaps[CAPS_HORIZONTAL_FONT_RES] ||
                os2fonts[i].sYDeviceRes != aDevCaps[CAPS_VERTICAL_FONT_RES]) {
                cerror += 1;
            }
            if (cerror < error) {
                error = cerror;
                best = i;
            }
            if (cerror == 0) {
                found = TRUE;
                font = best;
                match = os2fonts[best].lMatch;
            }
#ifdef VERBOSE
            if (found) printf("    found bitmap font %s, match %d (size %d)\n",
                   os2fonts[i].szFacename, os2fonts[i].lMatch,
                   os2fonts[i].lMaxBaselineExt);
/*
            } else { if (os2fonts[i].sNominalPointSize !=
                    logfonts[hFont].fattrs.lMaxBaselineExt * 10) {
            if (os2fonts[i].sNominalPointSize !=
                logfonts[hFont].fattrs.lMaxBaselineExt * 10) {
                printf("    height %d doesn't match required %d\n",
                       os2fonts[i].sNominalPointSize,
                       logfonts[hFont].fattrs.lMaxBaselineExt * 10);
*/
            if (os2fonts[i].lMaxBaselineExt !=
                logfonts[hFont].fattrs.lMaxBaselineExt) {
                printf("    height %d doesn't match required %d\n",
                       os2fonts[i].lMaxBaselineExt,
                       logfonts[hFont].fattrs.lMaxBaselineExt);
            } else if (os2fonts[i].sXDeviceRes !=
                aDevCaps[CAPS_HORIZONTAL_FONT_RES]) {
                printf("    hor. device res %d doesn't match required %d\n",
                       os2fonts[i].sXDeviceRes,
                       aDevCaps[CAPS_HORIZONTAL_FONT_RES]);
            } else if (os2fonts[i].sYDeviceRes !=
                aDevCaps[CAPS_VERTICAL_FONT_RES]) {
                printf("    vert. device res %d doesn't match required %d\n",
                       os2fonts[i].sYDeviceRes,
                       aDevCaps[CAPS_VERTICAL_FONT_RES]);
            } else if ( logfonts[hFont].fattrs.fsFontUse
                        & FATTR_FONTUSE_TRANSFORMABLE) {
                printf("    transformations require outline font\n");
            }
#endif
        }
    }
    /* If an exact bitmap for a different resolution found, take it */
    if (!found && error <= 1) {
        match = os2fonts[best].lMatch;
        font = best;
        found = TRUE;
    }
    /* If no bitmap but an outline found, take it */
    if (!found && outline != -1) {
        match = os2fonts[outline].lMatch;
        font = outline;
        found = TRUE;
        logfonts[hFont].outline = TRUE;
#ifdef VERBOSE
        printf("    using outline font %s, match %d\n",
               os2fonts[font].szFacename, os2fonts[font].lMatch);
#endif
    }
    /* If no exact bitmap but an approximate found, take it */
    if (!found && best != -1) {
        match = os2fonts[best].lMatch;
        font = best;
        found = TRUE;
    }
    if (!found) {
        /* Select default font by making facename empty */
#ifdef VERBOSE
        printf("TkpGetNativeFont trying default font\n");
#endif
        memset(logfonts[hFont].fattrs.szFacename, '\0', FACESIZE);
        match= GpiCreateLogFont(hps, NULL, hFont,
                                &(logfonts[hFont].fattrs));
        if (match == GPI_ERROR) {
            if (os2fonts) {
                ckfree((char *)os2fonts);
            }
            return 0;
        } else if (match == FONT_DEFAULT) {
            FONTMETRICS fm;
            rc= GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &fm);
            if (!rc) {
                return 0;
            }
            logfonts[hFont].fattrs.lMatch = 0;
            strcpy(logfonts[hFont].fattrs.szFacename, fm.szFacename);
            logfonts[hFont].fattrs.idRegistry = fm.idRegistry;
            logfonts[hFont].fattrs.usCodePage = fm.usCodePage;
            logfonts[hFont].fattrs.lMaxBaselineExt = fm.lMaxBaselineExt;
            logfonts[hFont].fattrs.lAveCharWidth = fm.lAveCharWidth;
            logfonts[hFont].fattrs.fsType = 0;
            logfonts[hFont].fattrs.fsFontUse = 0;
            goto got_it;
        }
    }
    /* Fill in the exact font metrics if we found a font */
    if (!found) {
        if (os2fonts) {
            ckfree((char *)os2fonts);
        }
        return 0;
    } else {
        logfonts[hFont].fattrs.idRegistry = os2fonts[font].idRegistry;
        logfonts[hFont].fattrs.usCodePage = os2fonts[font].usCodePage;
        logfonts[hFont].fattrs.lMaxBaselineExt=os2fonts[font].lMaxBaselineExt;
        logfonts[hFont].fattrs.lAveCharWidth = os2fonts[font].lAveCharWidth;
        /*
         * NOTE: values for fsSelection and fsType in FONTMETRICS and FATTRS
         * differ, so check for each supported value.
         */
        if (os2fonts[font].fsSelection & FM_SEL_ITALIC) {
            logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_ITALIC;
        }
        if (os2fonts[font].fsSelection & FM_SEL_UNDERSCORE) {
            logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_UNDERSCORE;
        }
        if (os2fonts[font].fsSelection & FM_SEL_OUTLINE) {
            logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_OUTLINE;
        }
        if (os2fonts[font].fsSelection & FM_SEL_STRIKEOUT) {
            logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_STRIKEOUT;
        }
        if (os2fonts[font].fsSelection & FM_SEL_BOLD) {
            logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_BOLD;
        }

        if (os2fonts[font].fsType & FM_TYPE_KERNING) {
            logfonts[hFont].fattrs.fsType |= FATTR_TYPE_KERNING;
        }
        if (os2fonts[font].fsType & FM_TYPE_MBCS) {
            logfonts[hFont].fattrs.fsType |= FATTR_TYPE_MBCS;
        }
        if (os2fonts[font].fsType & FM_TYPE_DBCS) {
            logfonts[hFont].fattrs.fsType |= FATTR_TYPE_DBCS;
        }
        /* Nothing to determine FATTR_TYPE_ANTIALIASED ? */
        logfonts[hFont].fattrs.fsFontUse = 0;
        if (os2fonts[font].fsCapabilities & FM_CAP_NOMIX) {
            logfonts[hFont].fattrs.fsFontUse |= FATTR_FONTUSE_NOMIX;
        }
        if (os2fonts[font].fsDefn & FM_DEFN_OUTLINE) {
            logfonts[hFont].fattrs.fsFontUse |= FATTR_FONTUSE_OUTLINE;
            /*
            logfonts[hFont].fattrs.fsFontUse |= FATTR_FONTUSE_TRANSFORMABLE;
            */
        }
        logfonts[hFont].fattrs.lMatch = match;
        if (logfonts[hFont].outline == TRUE) {
            logfonts[hFont].fattrs.lMaxBaselineExt = 0;
            logfonts[hFont].fattrs.lAveCharWidth = 0;
        }
        strcpy(logfonts[hFont].fattrs.szFacename, os2fonts[font].szFacename);
#ifdef VERBOSE
        printf("    using match %d (%s)\n", match,
               logfonts[hFont].fattrs.szFacename);
#endif
    }

got_it:

#ifdef VERBOSE
    printf("m %d len %d sel %x reg %d cp %d mbe %d acw %d sel %x tp %x fu %x\n",
           logfonts[hFont].fattrs.lMatch,
           logfonts[hFont].fattrs.usRecordLength,
           logfonts[hFont].fattrs.fsSelection,
           logfonts[hFont].fattrs.idRegistry,
           logfonts[hFont].fattrs.usCodePage,
           logfonts[hFont].fattrs.lMaxBaselineExt,
           logfonts[hFont].fattrs.lAveCharWidth,
           logfonts[hFont].fattrs.fsSelection,
           logfonts[hFont].fattrs.fsType, logfonts[hFont].fattrs.fsFontUse);
#endif

    match = GpiCreateLogFont(hps, NULL, hFont,
                             &logfonts[hFont].fattrs);

    if (match == GPI_ERROR) {
#ifdef VERBOSE
        printf("GpiCreateLogFont %s (hps %x, id %d) ERROR, error %x\n",
	       logfonts[hFont].fattrs.szFacename, hps, hFont,
               WinGetLastError(TclOS2GetHAB()));
#endif
        return 0;
    }
#ifdef VERBOSE
    printf("    GpiCreateLogFont %s (hps %x, id %d) OK, match %d\n",
           logfonts[hFont].fattrs.szFacename, hps, hFont, match);
#endif
    logfonts[hFont].fattrs.lMatch = match;
/***********/

    
    memset(&logfonts[hFont].fm, 0, sizeof(logfonts[hFont].fm));
    /* Defaults */
    logfonts[hFont].fattrs.usRecordLength   = (USHORT)sizeof(FATTRS);
    logfonts[hFont].fattrs.fsSelection      = 0;
    logfonts[hFont].fattrs.lMatch           = 0L;
    logfonts[hFont].fattrs.idRegistry       = 0; /* Unknown */
    logfonts[hFont].fattrs.usCodePage       = 0; /* Present codepage */
    logfonts[hFont].fattrs.lMaxBaselineExt  = 0L;
    logfonts[hFont].fattrs.lAveCharWidth    = 0L;
    logfonts[hFont].fattrs.fsType           = 0;
    logfonts[hFont].fattrs.fsFontUse        = 0;

    /*
     * A nonzero number pixelSize means using pixel size,
     * 0 means default font size (point size)
     */
    if (pixelSize != 0) {
        logfonts[hFont].fattrs.lMaxBaselineExt = pixelSize;
        logfonts[hFont].deciPoints = 10 * PIXTOPOINT(pixelSize);
#ifdef VERBOSE
        printf("pixel size %d deciPoints %d\n",
               logfonts[hFont].fattrs.lMaxBaselineExt,
               logfonts[hFont].deciPoints);
#endif
    } else {
        logfonts[hFont].fattrs.lMaxBaselineExt = 0;
        logfonts[hFont].deciPoints = 120;
#ifdef VERBOSE
        printf("deciPoints %d\n", logfonts[hFont].deciPoints);
#endif
    }
    logfonts[hFont].shear.x = 0;
    logfonts[hFont].shear.y = 1;     /* Upright characters by default */
    /* Not necessary to set shear by default */
    logfonts[hFont].setShear = FALSE;
    logfonts[hFont].outline = FALSE;
    if (faPtr->weight != TK_FW_NORMAL) {
        logfonts[hFont].fm.fsSelection |= FATTR_SEL_BOLD;
#ifdef VERBOSE
        printf("FATTR_SEL_BOLD\n");
#endif
    }
    /* If the name already contains "Italic" then don't specify that */
    if (faPtr->slant == TK_FS_ITALIC) {
        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_ITALIC;
#ifdef VERBOSE
        printf("FATTR_SEL_ITALIC\n");
#endif
    }
    if (faPtr->underline != 0) {
        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_UNDERSCORE;
#ifdef VERBOSE
        printf("FATTR_SEL_UNDERSCORE\n");
#endif
    }
    if (faPtr->overstrike != 0) {
        logfonts[hFont].fattrs.fsSelection |= FATTR_SEL_STRIKEOUT;
#ifdef VERBOSE
        printf("FATTR_SEL_STRIKEOUT\n");
#endif
    }

    Tcl_UtfToExternalDString(systemEncoding, faceName, -1, &ds);

    /*
     * We can only store up to FACESIZE characters
     */
    if (Tcl_DStringLength(&ds) >= FACESIZE) {
        Tcl_DStringSetLength(&ds, FACESIZE);
    }
    strncpy(logfonts[hFont].fattrs.szFacename, Tcl_DStringValue(&ds),
                                    FACESIZE);
    match = GpiCreateLogFont(hps, NULL, hFont,
                             &logfonts[hFont].fattrs);
    if (match == GPI_ERROR) {
#ifdef VERBOSE
        printf("GpiCreateLogFont %s (hps %x, id %d) ERROR, error %x\n",
               logfonts[hFont].fattrs.szFacename, hps, hFont,
               WinGetLastError(TclOS2GetHAB()));
#endif
        hFont = 0;
    } else {
        nextLogicalFont++;
    }
    
    Tcl_DStringFree(&ds);
    return hFont;
}

/*
 *-------------------------------------------------------------------------
 *
 * FamilyExists, FamilyOrAliasExists, WinFontExistsProc --
 *
 *      Determines if any physical screen font exists on the system with
 *      the given family name.  If the family exists, then it should be
 *      possible to construct some physical screen font with that family
 *      name.
 *
 * Results:
 *      The return value is 0 if the specified font family does not exist,
 *      non-zero otherwise.
 *
 * Side effects:
 *      None.
 *
 *-------------------------------------------------------------------------
 */

static int
FamilyExists(
    HPS hps,                    /* HPS in which font family will be used. */
    CONST char *faceName)       /* Font family to query. */
{
    BOOL found = FALSE, foundFont = FALSE;
    PFONTMETRICS os2fonts;
    LONG reqFonts, remFonts;
    int i;
    PSZ familyName, fontName;
    Tcl_DString faceString;
    char fullFamName[MAX_FLEN], fullFaceName[MAX_FLEN];
#ifdef VERBOSE
    printf("FamilyExists\n");
    fflush(stdout);
#endif

    /*
     * The Windows port rules out the fonts with faceName equal to
     * "Courier", "Times" and "Helvetica" because the "look so ugly on
     * Windows" (how'bout bugging Microsoft???) at this point.
     * Of course, we have built-in Adobe Type Manager, so we won't
     * have to mimic this hack of an OS.
     */

    Tcl_UtfToExternalDString(systemEncoding, faceName, -1, &faceString);

    /* Check fonts until one is found with the family or all fonts processed */

    /* Determine total number of fonts */
    reqFonts = 0L;
    remFonts = GpiQueryFonts(hps, QF_PUBLIC, NULL, &reqFonts,
                             (LONG) sizeof(FONTMETRICS), NULL);
#ifdef VERBOSE
    printf("FamilyExists [%s] nr.of fonts: %d\n", Tcl_DStringValue(&faceString),
           remFonts);
#endif

    /* Allocate space for the fonts */
    os2fonts = (PFONTMETRICS) ckalloc(remFonts * sizeof(FONTMETRICS));
    if (os2fonts == NULL) {
        return;
    }

    /* Retrieve the fonts */
    reqFonts = remFonts;
    remFonts = GpiQueryFonts(hps, QF_PUBLIC, NULL, &reqFonts,
                             (LONG) sizeof(FONTMETRICS), os2fonts);
#ifdef VERBOSE
    printf("    got %d (%d remaining)\n", reqFonts, remFonts);
#endif
    for (i=0; i<reqFonts && !found; i++) {
#ifdef VERBOSE
       printf("m%d Em%d %ddpt lMBE%d lACW%d %dx%d %s %s face[%s]%s fam[%s]%s\n",
              os2fonts[i].lMatch, os2fonts[i].lEmHeight,
              os2fonts[i].sNominalPointSize, os2fonts[i].lMaxBaselineExt,
              os2fonts[i].lAveCharWidth,
              os2fonts[i].sXDeviceRes, os2fonts[i].sYDeviceRes,
              (os2fonts[i].fsType & FM_TYPE_FIXED) ? "fix" : "prop",
              (os2fonts[i].fsDefn & FM_DEFN_OUTLINE) ? "outl" : "bmp",
              os2fonts[i].szFacename,
              (os2fonts[i].fsType & FM_TYPE_FACETRUNC) ? " (trunc)" : "",
              os2fonts[i].szFamilyname,
              (os2fonts[i].fsType & FM_TYPE_FAMTRUNC) ? " (trunc)" : "");
#endif
	if (os2fonts[i].fsType & FM_TYPE_FAMTRUNC) {
	    rc = WinQueryAtomName(WinQuerySystemAtomTable(),
                                  os2fonts[i].FamilyNameAtom, (PSZ)&fullFamName,
                                  MAX_FLEN);
            if (rc != 0) {
#ifdef VERBOSE
                printf("WinQueryAtomName family OK: %s\n", fullFamName);
                fflush(stdout);
#endif
                if (rc >= 256) {
                    fullFamName[255] = '\0';
                }
                familyName = fullFamName;
            } else {
#ifdef VERBOSE
                printf("WinQueryAtomName family ERROR %d\n",
                       WinGetLastError(TclOS2GetHAB()));
                fflush(stdout);
#endif
                familyName = os2fonts[i].szFamilyname;
            }
	} else {
            familyName = os2fonts[i].szFamilyname;
	}

        found = (strcmp(familyName, Tcl_DStringValue(&faceString)) == 0);
    }
#ifdef VERBOSE
    if (found) printf("family found: %s\n", familyName);
    else printf("family not found\n");
    fflush(stdout);
#endif
    ckfree((char *)os2fonts);

    Tcl_DStringFree(&faceString);
    return (found == TRUE);
}

static char *
FamilyOrAliasExists(
    HPS hps,
    CONST char *faceName)
{
    char **aliases;
    int i;
#ifdef VERBOSE
    printf("FamilyOrAliasExists\n");
    fflush(stdout);
#endif

    if (FamilyExists(hps, faceName) != 0) {
        return (char *) faceName;
    }
    aliases = TkFontGetAliasList(faceName);
    if (aliases != NULL) {
        for (i = 0; aliases[i] != NULL; i++) {
            if (FamilyExists(hps, aliases[i]) != 0) {
                return aliases[i];
            }
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2SelectFont --
 *
 *      Select a font into a presentation space, scaling the font if
 *      necessary. Scaling part adapted from "OS/2 Presentation Manager
 *      Programming" by Petzold.
 *
 * Results:
 *	Returns previously selected font in the presentation space when
 *      successful, 0 (default font) when not successful.
 *
 * Side effects:
 *	Sets the character box attribute of a presentation space.
 *
 *----------------------------------------------------------------------
 */

LONG
TkOS2SelectFont(
    HPS hps,            /* Presentation Space for setting font into */
    LONG hFont)         /* Handle (local ID) for the font, index into array */
{
    HDC hdc;
    LONG xRes, yRes, oldFont;
    ULONG pointWidth;
    POINTL points[2];
    SIZEF sizef;
    APIRET rc;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
#ifdef VERBOSE
    printf("TkOS2SelectFont hps %x hFont %d, tsdPtr %x\n", hps, hFont, tsdPtr);
    fflush(stdout);
#endif
    if (hFont < 0 || hFont > 254) return 0;

    oldFont = GpiQueryCharSet(hps);
    rc = GpiCreateLogFont(hps, NULL, hFont, &logfonts[hFont].fattrs);
    if (rc == GPI_ERROR) {
#ifdef VERBOSE
        printf("TOSF  GpiCreateLogFont %s hps %x, id %d (match %d) ERROR %x\n",
	       logfonts[hFont].fattrs.szFacename, hps,
	       hFont, logfonts[hFont].fattrs.lMatch,
	       WinGetLastError(TclOS2GetHAB()));
        return 0;
    } else {
        printf("TOSF  GpiCreateLogFont %s hps %x, id %d (match %d) OK: %d\n",
	       logfonts[hFont].fattrs.szFacename, hps,
	       hFont, logfonts[hFont].fattrs.lMatch, rc);
#endif
    }
    rc = GpiSetCharSet(hps, hFont);
    if (rc == FALSE) {
#ifdef VERBOSE
        printf("TOSF  GpiSetCharSet %s hps %x, id %d (match %d) ERROR %x\n",
	       logfonts[hFont].fattrs.szFacename, hps,
	       hFont, logfonts[hFont].fattrs.lMatch,
	       WinGetLastError(TclOS2GetHAB()));
        return 0;
    } else {
        printf("TOSF  GpiSetCharSet %s hps %x, id %d (match %d) OK: %d\n",
	       logfonts[hFont].fattrs.szFacename, hps,
	       hFont, logfonts[hFont].fattrs.lMatch, rc);
#endif
    }
    /* If this is an outline font, set the char box */
    if (logfonts[hFont].outline) {
#ifdef VERBOSE
        SIZEF charBox;
#endif
        /* Point width is the same as the height */
        pointWidth = logfonts[hFont].deciPoints;

        /* Determine device and its resolutions */
        hdc = GpiQueryDevice(hps);
        if (hdc == HDC_ERROR) {
#ifdef VERBOSE
            printf("TOSF  GpiQueryDevice ERROR %x\n",
                   WinGetLastError(TclOS2GetHAB()));
            fflush(stdout);
#endif
            return FALSE;
        } else if (hdc == NULLHANDLE) {
#ifdef VERBOSE
            printf("TOSF  GpiQueryDevice returns NULLHANDLE\n");
            fflush(stdout);
#endif
            /* No device context associated, assume the screen */
            xRes = aDevCaps[CAPS_HORIZONTAL_FONT_RES];
            yRes = aDevCaps[CAPS_VERTICAL_FONT_RES];
        } else {
#ifdef VERBOSE
            printf("TOSF  GpiQueryDevice returns %x\n", hdc);
            fflush(stdout);
#endif
            rc = DevQueryCaps(hdc, CAPS_HORIZONTAL_FONT_RES, 1, &xRes);
            if (rc != TRUE) {
#ifdef VERBOSE
                printf("TOSF  DevQueryCaps xRes ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
#endif
                xRes = aDevCaps[CAPS_HORIZONTAL_FONT_RES];
            }
            rc = DevQueryCaps(hdc, CAPS_VERTICAL_FONT_RES, 1, &yRes);
            if (rc != TRUE) {
#ifdef VERBOSE
                printf("TOSF  DevQueryCaps yRes ERROR %x\n",
                       WinGetLastError(TclOS2GetHAB()));
#endif
                yRes = aDevCaps[CAPS_VERTICAL_FONT_RES];
            }
        }

        FIX_RES(xRes);
        FIX_RES(yRes);

        /*
         * Determine desired point size in pixels with device resolution.
         * Font resolution is returned by PM in pels per inch, device resolution
         * is in dots per inch. 722.818 decipoints in an inch.
         * Add 361.409 for correct rounding.
         */
        points[0].x = 0;
        points[0].y = 0;
        points[1].x = (xRes * pointWidth + 361.409) / 722.818;
        points[1].y = (yRes * logfonts[hFont].deciPoints + 361.409)
                      / 722.818;

        /* Convert to page coordinates */
        rc = GpiConvert(hps, CVTC_DEVICE, CVTC_PAGE, 2L, points);
#ifdef VERBOSE
        printf("xRes %d, pointWidth %d, yRes %d, hFont %d, deciPoints %d\n",
               xRes, pointWidth, yRes, hFont, logfonts[hFont].deciPoints);
        if (rc!=TRUE) printf("GpiConvert ERROR %x\n",
                             WinGetLastError(TclOS2GetHAB()));
        else printf("GpiConvert OK: (%d,%d) -> (%d,%d)\n",
                    (xRes * pointWidth + 361.409) / 722.818,
                    (yRes * logfonts[hFont].deciPoints + 361.409) / 722.818,
                    points[1].x, points[1].y);
        fflush(stdout);
#endif

        /* Now set the character box */
        sizef.cx = MAKEFIXED((points[1].x - points[0].x), 0);
        sizef.cy = MAKEFIXED((points[1].y - points[0].y), 0);
#ifdef VERBOSE
        printf("after GpiConvert: cx FIXED(%d), cy FIXED(%d)\n",
               points[1].x - points[0].x, points[1].y - points[0].y);
#endif

        rc = GpiSetCharBox(hps, &sizef);
#ifdef VERBOSE
        if (rc!=TRUE) {
            printf("GpiSetCharBox %d ERROR %x\n",
                   logfonts[hFont].deciPoints,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiSetCharBox %d OK\n",
                   logfonts[hFont].deciPoints);
        }
        rc = GpiQueryCharBox(hps, &charBox);
        if (rc!=TRUE) {
            printf("GpiQueryCharBox ERROR %x\n");
        } else {
            printf("GpiQueryCharBox OK: now cx %d (%d,%d), cy %d (%d,%d)\n",
                   charBox.cx, FIXEDINT(charBox.cx), FIXEDFRAC(charBox.cx),
                   charBox.cy, FIXEDINT(charBox.cy), FIXEDFRAC(charBox.cy));
        }
#endif
    }
    return oldFont;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2QueryTextWidth --
 *
 *      Determine the width of a string, splitting it in 512 byte chunks
 *      because of that limit on GpiQueryTextBox.
 *
 * Results:
 *	Returns width of string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

LONG
TkOS2QueryTextWidth(
    HPS hps,            /* Presentation Space with set font */
    CONST char *source, /* String of which to determine the width. */
    int numBytes)       /* Number of bytes to consider of source string. */
{
    int tmpNumBytes;
    CONST char *str;
    POINTL aSize[TXTBOX_COUNT];
    LONG width = 0;
#ifdef VERBOSE
    printf("TkOS2QueryTextWidth\n");
    fflush(stdout);
#endif

    /* only 512 bytes allowed in string */
    for (tmpNumBytes = numBytes, str = source;
         tmpNumBytes > 0;
         tmpNumBytes -= 512, str += 512) {
        rc = GpiQueryTextBox(hps, (tmpNumBytes > 512 ? 512 : tmpNumBytes),
                             (PCH)str, TXTBOX_COUNT, aSize);
#ifdef VERBOSE
        if (rc != TRUE) {
            printf("GpiQueryTextBox [%s] (%d) ERROR %x\n", str,
                   (tmpNumBytes > 512 ? 512 : tmpNumBytes),
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiQueryTextBox [%s] (%d) OK: %d + %d\n", str,
                   (tmpNumBytes > 512 ? 512 : tmpNumBytes), width,
                   aSize[TXTBOX_CONCAT].x- aSize[TXTBOX_BOTTOMLEFT].x);
        }
#endif
        width += aSize[TXTBOX_CONCAT].x - aSize[TXTBOX_BOTTOMLEFT].x;
    }

    return width;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOS2CharString --
 *
 *      Output a string, splitting it in 512 byte chunks
 *      because of that limit on GpiCharString.
 *
 * Results:
 *	Success or failure.
 *
 * Side effects:
 *	Outputs text into the presentation space.
 *
 *----------------------------------------------------------------------
 */

LONG
TkOS2CharString(
    HPS hps,            /* Presentation Space with set font */
    CONST char *source, /* String to output. */
    int numBytes,       /* Number of bytes to output of source string. */
    PPOINTL curPosPtr)  /* Starting from where the string is to be output. */
{
    CONST char *str;
#ifdef VERBOSE
    printf("TkOS2CharString [%s] (%d bytes)\n", source, numBytes);
    fflush(stdout);
#endif

    rc = GpiSetCurrentPosition(hps, curPosPtr);
#ifdef VERBOSE
    if (rc!=TRUE) {
        printf("GpiSetCurrentPosition %d,%d ERROR %x\n", curPosPtr->x,
               curPosPtr->y, WinGetLastError(TclOS2GetHAB()));
    } else {
        printf("GpiSetCurrentPosition %d,%d OK\n", curPosPtr->x, curPosPtr->y);
    }
#endif
    str = source;
    while (numBytes>0) {
        /* only 512 bytes allowed in string */
        rc = GpiCharString(hps, (numBytes < 512 ? numBytes : 512), (PCH)str);
#ifdef VERBOSE
        if (rc==GPI_ERROR) {
            printf("GpiCharString [%s] (%d bytes) ERROR %x\n", str, numBytes,
                   WinGetLastError(TclOS2GetHAB()));
        } else {
            printf("GpiCharString [%s] (%d bytes) OK\n", str, numBytes);
        }
#endif
        numBytes -= 512;
        str += 512;
    }
    return rc;
}

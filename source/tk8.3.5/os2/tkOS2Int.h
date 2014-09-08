/*
 * tkOS2Int.h --
 *
 *	Declarations of OS/2 PM specific shared variables and procedures.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _TKOS2INT
#define _TKOS2INT

#ifndef _TKINT
#include "tkInt.h"
#endif

/*
 * Include platform specific public interfaces.
 */

#ifndef _TKOS2
#include "tkOS2.h"
#endif

#ifndef _TKPORT
#include "tkPort.h"
#endif

/*
 * The TkOS2PSState is used to save the state of a presentation space
 * so that it can be restored later.
 */

typedef struct TkOS2PSState {
    HPAL palette;
    HBITMAP bitmap;
    LONG backMix;
} TkOS2PSState;

/*
 * The TkOS2Drawable is the internal implementation of an X Drawable (either
 * a Window or a Pixmap).  The following constants define the valid Drawable
 * types.
 */

#define TOD_BITMAP	1
#define TOD_WINDOW	2
#define TOD_OS2PS	3

/* Tk OS2 Window Classes */
#define TOC_TOPLEVEL	"TkTopLevel"
#define TOC_CHILD	"TkChild"

#define CW_USEDEFAULT	0

/* Defines for which poly... function */
#define TOP_POLYGONS	1
#define TOP_POLYLINE	2

/* OS/2 system constants */
#define MAX_LID	254	/* Max nr. of logical font IDs */
#define MAX_FLEN 256	/* Maximum length of font atom */

/*
 * Override PM resolution for 120dpi displays with the following value if
 * IGNOREPMRES is defined. Requested by Ilya Zakharevich
 */
#ifdef IGNOREPMRES
    extern LONG overrideResolution;
#endif

#define MAX(a,b)	( (a) > (b) ? (a) : (b) )
#define MIN(a,b)	( (a) < (b) ? (a) : (b) )

typedef struct {
    int type;
    HWND handle;
    TkWindow *winPtr;
} TkOS2Window;

typedef struct {
    int type;
    HBITMAP handle;
    Colormap colormap;
    int depth;
    HWND parent;
    HDC dc;
    HPS hps;
} TkOS2Bitmap;

typedef struct {
    int type;
    HPS hps;
    HWND hwnd;
} TkOS2PS;
    
typedef union {
    int type;
    TkOS2Window window;
    TkOS2Bitmap bitmap;
    TkOS2PS os2PS;
} TkOS2Drawable;

/*
 * The following macros are used to retrieve internal values from a Drawable.
 */
#define TkOS2GetHWND(w)		(((TkOS2Drawable *)w)->window.handle)
#define TkOS2GetWinPtr(w)	(((TkOS2Drawable*)w)->window.winPtr)
#define TkOS2GetHBITMAP(w)	(((TkOS2Drawable*)w)->bitmap.handle)
#define TkOS2GetColormap(w)	(((TkOS2Drawable*)w)->bitmap.colormap)
#define TkOS2GetHPS(w)		(((TkOS2Drawable*)w)->os2PS.hps)

/*
 * The following structure is used to encapsulate palette information.
 */

typedef struct {
    HPAL palette;		/* Palette handle used when drawing. */
    ULONG size;			/* Number of entries in the palette. */
    int stale;			/* 1 if palette needs to be realized,
				 * otherwise 0.  If the palette is stale,
				 * then an idle handler is scheduled to
				 * realize the palette. */
    Tcl_HashTable refCounts;	/* Hash table of palette entry reference counts
				 * indexed by pixel value. */
} TkOS2Colormap;

/*
 * The following macro retrieves the PM palette from a colormap.
 */

#define TkOS2GetPalette(colormap) (((TkOS2Colormap *) colormap)->palette)

/*
 * The following variable is a translation table between X gc functions and
 * OS/2 PM raster op modes.
 */

extern int tkpOS2MixModes[];

/*
 * The following macros are used to replace the Windows equivalents.
 */
#define RGB(R,G,B)       ((((ULONG)R)<<16) + (((ULONG)G)<<8) + (ULONG)B)
#define RGBFlag(F,R,G,B) ((((ULONG)F)<<24) + (((ULONG)R)<<16) + (((ULONG)G)<<8) + (ULONG)B)
#define GetFlag(RGB)     ((BYTE)(RGB>>24))

#define COLOR_3DFACE	SYSCLR_TITLETEXT
#define COLOR_WINDOW	SYSCLR_WINDOW

/*
 * The following defines are used with TkOS2GetBorderPixels to get the
 * extra 2 border colors from a Tk_3DBorder.
 */

#define TK_3D_LIGHT2 TK_3D_DARK_GC+1
#define TK_3D_DARK2 TK_3D_DARK_GC+2

/*
 * Button state information.
 */
#define BST_UNCHECKED     0
#define BST_CHECKED       1
#define BST_INDETERMINATE 2
#define BST_FOCUS         TRUE

/*
 * The following structure is used to remember font attributes that cannot be
 * given to GpiCreateLogFont via FATTRS.
 */

typedef struct {
    FATTRS fattrs;	/* FATTRS structure */
    POINTL shear;	/* Shear (angle) of characters, GpiSetCharShear */
    BOOL setShear;	/* Should shear be changed after GpiCreateLogFont? */
    BOOL outline;	/* Is this an outline font */
    ULONG deciPoints;	/* Pointsize for outline font, in decipoints */
    FONTMETRICS fm;	/* Fontmetrics, for concentrating outline font stuff */
} TkOS2Font;

/*
 * The following structures are used to mimic the WINDOWPOS structure that has
 * fields for minimum and maximum width/height.
 */

typedef struct {
    LONG x;
    LONG y;
} TkOS2TrackSize;

typedef struct {
    TkOS2TrackSize ptMinTrackSize;
    TkOS2TrackSize ptMaxTrackSize;
    SWP swp;
} TkOS2WINDOWPOS;

/*
 * Internal procedures used by more than one source file.
 */

#include "tkIntPlatDecls.h"
/*
 * We need to specially add TkOS2ChildProc/TkOS2FrameProc because of the special
 * prototype they have (don't fit into stubs schema)
 */
extern MRESULT EXPENTRY TkOS2ChildProc _ANSI_ARGS_((HWND hwnd, ULONG message,
                            MPARAM param1, MPARAM param2));
extern MRESULT EXPENTRY TkOS2FrameProc _ANSI_ARGS_((HWND hwnd, ULONG message,
                            MPARAM param1, MPARAM param2));

/*
 * Special proc needed as tsd accessor function between
 * tkOS2X.c:GenerateXEvent and tkOS2Clipboard.c:UpdateClipboard
 */
EXTERN void     TkOS2UpdatingClipboard(int mode);

/* Global variables */
extern HAB tkHab;	/* Anchor block */
extern HMQ hmq;	/* message queue */
extern LONG aDevCaps[];	/* Device caps */
extern PFNWP oldFrameProc;	/* subclassed frame procedure */
extern LONG xScreen;		/* System Value Screen width */
extern LONG yScreen;		/* System Value Screen height */
extern LONG titleBar;		/* System Value Title Bar */
extern LONG xBorder;		/* System Value X nominal border */
extern LONG yBorder;		/* System Value Y nominal border */
extern LONG xSizeBorder;	/* System Value X Sizing border */
extern LONG ySizeBorder;	/* System Value Y Sizing border */
extern LONG xDlgBorder;		/* System Value X dialog-frame border */
extern LONG yDlgBorder;		/* System Value Y dialog-frame border */
extern HDC hScreenDC;		/* Device Context for screen */
extern HPS globalPS;		/* Global PS */
extern HBITMAP globalBitmap;	/* Bitmap for global PS */
extern TkOS2Font logfonts[];	/* List of logical fonts */
extern LONG nextLogicalFont;	/* First free logical font ID */
extern LONG nextColor;		/* Next free index in color table */
extern LONG *logColorTable;     /* Table of colors in use */
extern LONG rc;			/* For checking return values */
extern unsigned long dllHandle;	/* Handle of the Tk DLL */
extern Tcl_Interp *globalInterp; /* Interpreter for Tcl_InitStubs in tkOS2Dll */

#endif /* _TKOS2INT */

/*
 * tkOS2Port.h --
 *
 *	This header file handles porting issues that occur because of
 *	differences between OS/2 and Unix. It should be the only
 *	file that contains #ifdefs to handle different flavors of OS.
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifndef _OS2PORT
#define _OS2PORT

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>

#define strncasecmp strnicmp
#define strcasecmp stricmp

#define GetRValue(RGB)   ((BYTE)((RGB>>16) & 0x0000FF))
#define GetGValue(RGB)   ((BYTE)((RGB>>8) & 0x0000FF))
#define GetBValue(RGB)   ((BYTE)(RGB & 0x0000FF))

#define NBBY 8

#ifndef OPEN_MAX
#define OPEN_MAX 32
#endif

/*
 * Define MAXPATHLEN in terms of MAXPATH if available
 */

#ifndef MAX_PATH
#define MAX_PATH CCHMAXPATH
#endif

#ifndef MAXPATH
#define MAXPATH MAX_PATH
#endif /* MAXPATH */

#ifndef MAXPATHLEN
#define MAXPATHLEN MAXPATH
#endif /* MAXPATHLEN */

/*
 * The following define causes Tk to use its internal keysym hash table
 */

#define REDO_KEYSYM_LOOKUP

/*
 * The following macro checks to see whether there is buffered
 * input data available for a stdio FILE.
 */

#ifdef __EMX__
#    define TK_READ_DATA_PENDING(f) ((f)->rcount > 0)
#elif __BORLANDC__
#    define TK_READ_DATA_PENDING(f) ((f)->level > 0)
#elif __IBMC__
#    define TK_READ_DATA_PENDING(f) ((f)->_count > 0)
#endif /* __EMX__ */

/*
 * The following stubs implement various calls that don't do anything
 * under OS/2.
 */

#define TkFreeWindowId(dispPtr,w)
#define TkInitXId(dispPtr)
#define TkpCmapStressed(tkwin,colormap) (0)
#define XFlush(display)
#define XGrabServer(display)
#define XUngrabServer(display)
#define TkpSync(display)

/*
 * The following X functions are implemented as macros under OS/2.
 */

#define XFree(data) {if ((data) != NULL) ckfree((char *) (data));}
#define XNoOp(display) {display->request++;}
#define XSynchronize(display, bool) {display->request++;}
#define XSync(display, bool) {display->request++;}
#define XVisualIDFromVisual(visual) (visual->visualid)

/*
 * The following Tk functions are implemented as macros under OS/2.
 */

#define TkGetNativeProlog(interp) TkGetProlog(interp)
#define TkpGetPixel(p) (((((p)->red >> 8) & 0xff) \
        | ((p)->green & 0xff00) | (((p)->blue << 8) & 0xff0000)) | 0x20000000)

/*
 * These calls implement native bitmaps which are not currently
 * supported under OS/2.  The macros eliminate the calls.
 */

#define TkpDefineNativeBitmaps()
#define TkpCreateNativeBitmap(display, source) None
#define TkpGetNativeAppBitmap(display, name, w, h) None

#ifndef _TCLINT
#include <tclInt.h>
#endif

#endif /* _OS2PORT */

#
# Makefile for compiling Tk for OS/2 with EMX/GCC
#

### Build choices

# uncomment the following line to compile with symbols
#DEBUGGING=1

# uncomment the following line to compile with debugging printf's.
#VERBOSE=1

# Uncomment the following to make binaries that run only on Warp or higher
#TARGET_WARP=yes

# Uncomment the following line to compile with thread support
THREADDEFINES = -DTCL_THREADS

# Uncomment the following to compile/link with stubs
STUBSFLAG = -DUSE_TCL_STUBS

# Format is OMF (.obj, debuggable by IPMD, TD, TDOS2, ....)
# or A.OUT (.o, debuggable by GDB/PMGDB)
#FORMAT = A.OUT
FORMAT = OMF

# uncomment the following two lines to compile with TCL_MEM_DEBUG
#DEBUGDEFINES = -DTCL_MEM_DEBUG

# Choose the following line to use a default font of "9.WarpSans" (available
# on Warp 4 and later);
FONTDEFS=-DWARP4LATER
#FONTDEFS=

# Choose the following line to ignore the resolution Presentation Manager
# tells us the device offers if it's 120dpi and just use 96dpi.
# requested by Ilya Zakharevich, see also tkOS2Font.c
#RESDEFS=-DIGNOREPMRES
RESDEFS=

# Where to build target dist
DISTDIR = d:\TclTk835\TclTk

### End of build choices

# Version
VERSION = 83
TCLVERSION = 83
DOTVERSION = 8.3
FULLVERSION = 8.3.5
SHORTVERSION = 835

BIN_INSTALL_DIR = $(DISTDIR)\bin
LIB_INSTALL_DIR = $(DISTDIR)\lib
INCLUDE_INSTALL_DIR = $(DISTDIR)\include
SCRIPT_INSTALL_DIR = $(LIB_INSTALL_DIR)\tk$(DOTVERSION)

#
# Project directories
#
# ROOT = top of source tree
# TMPDIR = location where .$(OBJ) files should be stored during build
# TCLDIR = location of top of Tcl source hierarchy
#

ROOT	= ..
GENERIC_DIR = $(ROOT)\generic
UNIX_DIR = $(ROOT)\unix
XLIB_DIR = $(ROOT)\xlib
OS2_DIR = $(ROOT)\os2
TMPDIR	= .
TCLDIR	= ..\..\tcl$(FULLVERSION)
EMXDIR  = d:\emx

# Choose packing
ifdef TARGET_WARP
    # Warp 3 and higher
    EXEPACKING=/EXEPACK:2
    RESPACKING=-x2
#    LXLITE=lxlite /ML1
    LXLITE=echo lxlite /ML1
else
    # All 32bit versions of OS/2
    EXEPACKING=/EXEPACK
    RESPACKING=-x1
#    LXLITE=lxlite /MR3
    LXLITE=echo lxlite /MR3
endif

ifeq ($(FORMAT),A.OUT)
   ARCH      = a
   OBJ       = o
   OUTPUTFLAG=
   EXTRALINK =
   # Careful, there has to be a space after the definition, ie '-o ' !
   LINK_OUT  = -o 
   SOCKLIB   = -lsocket
   # No space after definition
   RC        = emxbind -br
   IMPLIB    = emximp
   LIB       = ar
   # Space after definition ('rc ')!
   LIB_OUT   = rc 
else
   ARCH      = lib
   OBJ       = obj
   OUTPUTFLAG= -Zomf
   EXTRALINK = -Zlinker /FARCALLTRANSLATION -Zlinker /RUNFROMVDM -Zlinker /BASE:0x10000 -Zlinker /ALIGN:2 -Zlinker $(EXEPACKING)
   # Careful, there has to be a space after the definition, ie '-o ' !
   LINK_OUT  = -o 
   SOCKLIB   = $(EMXDIR)\lib\socket.$(ARCH)
   # space after RC value
   RC        = rc -p $(RESPACKING) 
   IMPLIB    = emximp
   LIB       = emxomfar
   # Space after definition ('rc ')!
   LIB_OUT   = rc 
endif
RCPRE = rc -r -p $(RESPACKING)

OUTDIR         = .
TCLNAMEPREFIX  = tcl
TKNAMEPREFIX   = tk
WISHNAMEPREFIX = wish
TCLSTUBPREFIX  = $(TCLNAMEPREFIX)stub
TKSTUBPREFIX   = $(TKNAMEPREFIX)stub
TCLBASE        = tcl$(TCLVERSION)
TKBASE         = $(TKNAMEPREFIX)$(VERSION)
WISHBASE       = wish$(VERSION)
TCLLIBDIR      = $(TCLDIR)\os2
TCLLIBNAME     = $(TCLBASE).$(ARCH)
TCLLIB         = $(TCLLIBDIR)\$(TCLLIBNAME)
TCLPLUGINLIBNAME = $(TCLNAMEPREFIX)p$(VERSION).$(ARCH)
TCLPLUGINLIB   = $(TCLLIBDIR)\$(TCLPLUGINLIBNAME)
TCLDLL         = $(TCLDIR)\$(TCLBASE).dll
TKDLL          = $(OUTDIR)\$(TKBASE).dll
TKLIB          = $(OUTDIR)\$(TKBASE).$(ARCH)
TKOMFLIB       = $(OUTDIR)\$(TKBASE).lib
TKARCHIVE      = $(OUTDIR)\$(TKBASE).a
TKPLUGINDLLNAME= $(TKNAMEPREFIX)$(VERSION)p.dll
TKPLUGINDLL    = $(OUTDIR)\$(TKPLUGINDLLNAME)
TKPLUGINLIB    = $(OUTDIR)\$(TKNAMEPREFIX)$(VERSION)p.lib
WISH           = $(OUTDIR)\$(WISHBASE).exe
WISHC          = $(OUTDIR)\$(TKNAMEPREFIX)c$(VERSION).exe
WISHP          = $(OUTDIR)\$(TKNAMEPREFIX)p$(VERSION).exe
TKTESTBASE     = $(OUTDIR)\tktest$(VERSION)
TKTEST         = $(OUTDIR)\$(TKTESTBASE).exe

ifdef STUBSFLAG
  TCLSTUBLIBNAME  = $(TCLSTUBPREFIX)$(VERSION).lib
  TCLSTUBLIB      = $(TCLLIBDIR)\$(TCLSTUBLIBNAME)
  INIT_PM_IN_APPINIT = -DINIT_PM_IN_APPINIT
else
  TCLSTUBLIB      = $(TCLLIB) $(TCLLIBDIR)\tclStubLib.obj
endif
TKSTUBLIBNAME   = $(TKSTUBPREFIX)$(VERSION).lib
TKSTUBLIB       = $(TKSTUBLIBNAME)

#########

# GENTCLSH is used to generate the help file (INF file), should be in path
#GENTCLSH = tclsh76
GENTCLSH = tclsh80
IMPLIB   = emximp
CC       = gcc
LINK     = gcc
# Careful, there has to be a space after '-o' !
LINK_OUT = -o 
RC	 = rc -p $(RESPACKING)
CP       = copy
RM       = del \f
MKDIR	 = mkdir
EMXIMP   = emximp
 
DEFINES      = -D__OS2__ -DHAS_DIRENT -DHAS_STDARG -D"wchar_t=unsigned short" \
	       -D_WCHAR_T \
	       $(RESDEFS) $(FONTDEFS) $(THREADDEFINES)
STD_INCLUDES = -I$(OS2_DIR) -I$(EMXDIR)\include
TCL_INCLUDES = -I$(TCLLIBDIR) -I$(TCLDIR)\generic
TK_INCLUDES  = -I$(ROOT)\generic -I$(ROOT)\bitmaps -I$(XLIB_DIR) -I$(ROOT) \
	       -I$(UNIX_DIR)
INCLUDES     = $(STD_INCLUDES) $(TCL_INCLUDES) $(TK_INCLUDES)
LIBDIRS      = -L$(EMXDIR)\lib -L$(OS2_DIR)

ifdef VERBOSE
  VERBOSEDEFINES=-DVERBOSE
else
  VERBOSEDEFINES=
endif

ifndef DEBUGGING
  # these macros cause maximum optimization and no symbols
  CFLAGS =      -O -Wall -mprobe -m486 $(OUTPUTFLAG) -Zmtd -s \
		$(DEFINES) $(INCLUDES) $(VERBOSEDEFINES)
else
  # these macros enable debugging
  CFLAGS =      -g -mprobe -m486 $(OUTPUTFLAG) -Zmtd \
		-fhandle-exceptions -DTCL_DEBUG \
                $(DEFINES) $(DEBUGDEFINES) $(INCLUDES) $(VERBOSEDEFINES)
endif
TK_CFLAGS =    $(CFLAGS) $(STUBSFLAG)

ifndef DEBUGGING
  LNFLAGS_exe = $(OUTPUTFLAG) -Zmtd $(EXTRALINK) -lwrap -s
  LNFLAGS_dll = -mprobe -Zcrtdll -Zdll $(OUTPUTFLAG) -Zmt \
                $(EXTRALINK) -lm -lwrap -s
else
  LNFLAGS_exe = $(OUTPUTFLAG) -Zmtd $(EXTRALINK)
  LNFLAGS_dll = -mprobe -Zcrtdll -Zdll $(OUTPUTFLAG) -Zmt $(EXTRALINK) -lm
endif

# 
# Global makefile settings
#

#.AUTODEPEND
#.CACHEAUTODEPEND

#
# Targets
#

all: $(TKDLL) $(WISH) $(TKTEST) $(TKSTUBLIB) $(TKLIB)
test: $(TKDLL) $(TKTEST)

.SUFFIXES: .c .dll .$(ARCH) .$(OBJ) .exe .rc .res

WISHOBJS = \
	os2Main.$(OBJ)

TKTESTOBJS = \
	tkTest.$(OBJ) \
	tkSquare.$(OBJ) \
	testMain.$(OBJ) \
	tkOS2Test.$(OBJ) \
	$(TCLLIBDIR)\tclThreadTest.$(OBJ)

XLIBOBJS = \
	xcolors.$(OBJ) \
	xdraw.$(OBJ) \
	xgc.$(OBJ) \
	ximage.$(OBJ) \
	xutil.$(OBJ)

UNIXOBJS = \
	tkUnixMenubu.$(OBJ) \
	tkUnixScale.$(OBJ)

OS2TKOBJS = \
	tkOS23d.$(OBJ) \
	tkOS2Button.$(OBJ) \
	tkOS2Clipboard.$(OBJ) \
	tkOS2Color.$(OBJ) \
	tkOS2Config.$(OBJ) \
	tkOS2Cursor.$(OBJ) \
	tkOS2Dialog.$(OBJ) \
	tkOS2Dll.$(OBJ) \
	tkOS2Draw.$(OBJ) \
	tkOS2Embed.$(OBJ) \
	tkOS2Font.$(OBJ) \
	tkOS2Image.$(OBJ) \
	tkOS2ImgUtil.$(OBJ) \
	tkOS2Init.$(OBJ) \
	tkOS2Key.$(OBJ) \
	tkOS2Menu.$(OBJ) \
	tkOS2Pixmap.$(OBJ) \
	tkOS2Pointer.$(OBJ) \
	tkOS2Region.$(OBJ) \
	tkOS2Scrlbr.$(OBJ) \
	tkOS2Send.$(OBJ) \
	tkOS2Window.$(OBJ) \
	tkOS2Wm.$(OBJ) \
	tkOS2X.$(OBJ) \
	stubs.$(OBJ)

TKSTUBOBJS = \
        tkStubInit.$(OBJ) \
        tkStubLib.$(OBJ)

OS2TKOBJSPEC = tkOS2*.$(OBJ) tkStubInit.$(OBJ) tkStubLib.$(OBJ) stubs.$(OBJ)

TKSTUBLIBOBJS = \
	tkStubLib.$(OBJ) \
	tkStubImg.$(OBJ)

GENTKOBJS = \
	tk3d.$(OBJ) \
	tkArgv.$(OBJ) \
	tkAtom.$(OBJ) \
	tkBind.$(OBJ) \
	tkBitmap.$(OBJ) \
	tkButton.$(OBJ) \
	tkCanvArc.$(OBJ) \
	tkCanvBmap.$(OBJ) \
	tkCanvImg.$(OBJ) \
	tkCanvLine.$(OBJ) \
	tkCanvPoly.$(OBJ) \
	tkCanvPs.$(OBJ) \
	tkCanvText.$(OBJ) \
	tkCanvUtil.$(OBJ) \
	tkCanvWind.$(OBJ) \
	tkCanvas.$(OBJ) \
	tkClipboard.$(OBJ) \
	tkCmds.$(OBJ) \
	tkColor.$(OBJ) \
	tkConfig.$(OBJ) \
	tkConsole.$(OBJ) \
	tkCursor.$(OBJ) \
	tkEntry.$(OBJ) \
	tkError.$(OBJ) \
	tkEvent.$(OBJ) \
	tkFileFilter.$(OBJ) \
	tkFocus.$(OBJ) \
	tkFont.$(OBJ) \
	tkFrame.$(OBJ) \
	tkGC.$(OBJ) \
	tkGeometry.$(OBJ) \
	tkGet.$(OBJ) \
	tkGrab.$(OBJ) \
	tkGrid.$(OBJ) \
	tkImage.$(OBJ) \
	tkImgBmap.$(OBJ) \
	tkImgGIF.$(OBJ) \
	tkImgPPM.$(OBJ) \
	tkImgPhoto.$(OBJ) \
	tkListbox.$(OBJ) \
	tkMacWinMenu.$(OBJ) \
	tkMain.$(OBJ) \
	tkMenu.$(OBJ) \
	tkMenubutton.$(OBJ) \
	tkMenuDraw.$(OBJ) \
	tkMessage.$(OBJ) \
	tkObj.$(OBJ) \
	tkOldConfig.$(OBJ) \
	tkOption.$(OBJ) \
	tkPack.$(OBJ) \
	tkPlace.$(OBJ) \
	tkPointer.$(OBJ) \
	tkRectOval.$(OBJ) \
	tkScale.$(OBJ) \
	tkScrollbar.$(OBJ) \
	tkSelect.$(OBJ) \
	tkText.$(OBJ) \
	tkTextBTree.$(OBJ) \
	tkTextDisp.$(OBJ) \
	tkTextImage.$(OBJ) \
	tkTextIndex.$(OBJ) \
	tkTextMark.$(OBJ) \
	tkTextTag.$(OBJ) \
	tkTextWind.$(OBJ) \
	tkTrig.$(OBJ) \
	tkUtil.$(OBJ) \
	tkVisual.$(OBJ) \
	tkWindow.$(OBJ) \
	$(XLIBOBJS) $(UNIXOBJS)

tkTestInit.$(OBJ): tkAppInit.c
	$(RM) tkTestInit.c
	$(CP) $(UNIX_DIR)\tkAppInit.c tkTestInit.c
	$(CC) -c $(CFLAGS) -DTK_TEST tkTestInit.c
	$(RM) tkTestInit.c

tk3d.$(OBJ): $(GENERIC_DIR)\tk3d.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tk3d.c

#tkAppInit.$(OBJ): $(UNIX_DIR)\tkAppInit.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(UNIX_DIR)\tkAppInit.c

tkArgv.$(OBJ): $(GENERIC_DIR)\tkArgv.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkArgv.c

tkAtom.$(OBJ): $(GENERIC_DIR)\tkAtom.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkAtom.c

#tkBind.$(OBJ): $(GENERIC_DIR)\tkBind.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkBind.c
tkBind.$(OBJ): tkBind.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkBind.c

tkBitmap.$(OBJ): $(GENERIC_DIR)\tkBitmap.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkBitmap.c

tkClipboard.$(OBJ): $(GENERIC_DIR)\tkClipboard.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkClipboard.c

#tkCmds.$(OBJ): $(GENERIC_DIR)\tkCmds.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCmds.c
tkCmds.$(OBJ): tkCmds.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkCmds.c

tkColor.$(OBJ): $(GENERIC_DIR)\tkColor.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkColor.c

tkConfig.$(OBJ): $(GENERIC_DIR)\tkConfig.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkConfig.c

tkConsole.$(OBJ): $(GENERIC_DIR)\tkConsole.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkConsole.c

tkCursor.$(OBJ): $(GENERIC_DIR)\tkCursor.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCursor.c

tkError.$(OBJ): $(GENERIC_DIR)\tkError.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkError.c

tkEvent.$(OBJ): $(GENERIC_DIR)\tkEvent.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkEvent.c

tkFileFilter.$(OBJ): $(GENERIC_DIR)\tkFileFilter.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkFileFilter.c

tkFocus.$(OBJ): $(GENERIC_DIR)\tkFocus.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkFocus.c

tkFont.$(OBJ): $(GENERIC_DIR)\tkFont.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkFont.c

tkGet.$(OBJ): $(GENERIC_DIR)\tkGet.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkGet.c

tkGC.$(OBJ): $(GENERIC_DIR)\tkGC.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkGC.c

tkGeometry.$(OBJ): $(GENERIC_DIR)\tkGeometry.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkGeometry.c

#tkGrab.$(OBJ): $(GENERIC_DIR)\tkGrab.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkGrab.c
tkGrab.$(OBJ): tkGrab.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkGrab.c

tkGrid.$(OBJ): $(GENERIC_DIR)\tkGrid.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkGrid.c

tkMacWinMenu.$(OBJ): $(GENERIC_DIR)\tkMacWinMenu.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkMacWinMenu.c

#tkMain.$(OBJ): $(GENERIC_DIR)\tkMain.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkMain.c
tkMain.$(OBJ): tkMain.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkMain.c

tkObj.$(OBJ): $(GENERIC_DIR)\tkObj.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $?

tkOldConfig.$(OBJ): $(GENERIC_DIR)\tkOldConfig.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkOldConfig.c

tkOption.$(OBJ): $(GENERIC_DIR)\tkOption.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkOption.c

tkPack.$(OBJ): $(GENERIC_DIR)\tkPack.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkPack.c

tkPlace.$(OBJ): $(GENERIC_DIR)\tkPlace.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkPlace.c

#tkPointer.$(OBJ): $(GENERIC_DIR)\tkPointer.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkPointer.c
tkPointer.$(OBJ): tkPointer.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkPointer.c

tkSelect.$(OBJ): $(GENERIC_DIR)\tkSelect.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkSelect.c

#tkSend.$(OBJ): $(GENERIC_DIR)\tkSend.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkSend.c
tkSend.$(OBJ): tkSend.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkSend.c

tkUtil.$(OBJ): $(GENERIC_DIR)\tkUtil.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkUtil.c

tkVisual.$(OBJ): $(GENERIC_DIR)\tkVisual.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkVisual.c

#tkWindow.$(OBJ): $(GENERIC_DIR)\tkWindow.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkWindow.c
tkWindow.$(OBJ): tkWindow.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkWindow.c

tkButton.$(OBJ): $(GENERIC_DIR)\tkButton.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkButton.c

tkEntry.$(OBJ): $(GENERIC_DIR)\tkEntry.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkEntry.c

tkFrame.$(OBJ): $(GENERIC_DIR)\tkFrame.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkFrame.c

tkListbox.$(OBJ): $(GENERIC_DIR)\tkListbox.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkListbox.c

tkMenu.$(OBJ): $(GENERIC_DIR)\tkMenu.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkMenu.c

tkMenubutton.$(OBJ): $(GENERIC_DIR)\tkMenubutton.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkMenubutton.c

tkMenuDraw.$(OBJ): $(GENERIC_DIR)\tkMenuDraw.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkMenuDraw.c

tkMessage.$(OBJ): $(GENERIC_DIR)\tkMessage.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkMessage.c

tkScale.$(OBJ): $(GENERIC_DIR)\tkScale.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkScale.c

tkScrollbar.$(OBJ): $(GENERIC_DIR)\tkScrollbar.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkScrollbar.c

tkSquare.$(OBJ): $(GENERIC_DIR)\tkSquare.c
	$(CC) -c $(CFLAGS) $(GENERIC_DIR)\tkSquare.c

tkCanvas.$(OBJ): $(GENERIC_DIR)\tkCanvas.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvas.c

tkCanvArc.$(OBJ): $(GENERIC_DIR)\tkCanvArc.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvArc.c

tkCanvBmap.$(OBJ): $(GENERIC_DIR)\tkCanvBmap.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvBmap.c

tkCanvImg.$(OBJ): $(GENERIC_DIR)\tkCanvImg.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvImg.c

tkCanvLine.$(OBJ): $(GENERIC_DIR)\tkCanvLine.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvLine.c

tkCanvPoly.$(OBJ): $(GENERIC_DIR)\tkCanvPoly.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvPoly.c

tkCanvPs.$(OBJ): $(GENERIC_DIR)\tkCanvPs.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvPs.c
#tkCanvPs.$(OBJ): tkCanvPs.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkCanvPs.c

tkCanvText.$(OBJ): $(GENERIC_DIR)\tkCanvText.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvText.c

tkCanvUtil.$(OBJ): $(GENERIC_DIR)\tkCanvUtil.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvUtil.c

tkCanvWind.$(OBJ): $(GENERIC_DIR)\tkCanvWind.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkCanvWind.c

tkRectOval.$(OBJ): $(GENERIC_DIR)\tkRectOval.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkRectOval.c

tkTrig.$(OBJ): $(GENERIC_DIR)\tkTrig.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTrig.c

tkImage.$(OBJ): $(GENERIC_DIR)\tkImage.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkImage.c

tkImgBmap.$(OBJ): $(GENERIC_DIR)\tkImgBmap.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkImgBmap.c

tkImgGIF.$(OBJ): $(GENERIC_DIR)\tkImgGIF.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkImgGIF.c

tkImgPPM.$(OBJ): $(GENERIC_DIR)\tkImgPPM.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkImgPPM.c

#tkImgPhoto.$(OBJ): $(GENERIC_DIR)\tkImgPhoto.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkImgPhoto.c
tkImgPhoto.$(OBJ): tkImgPhoto.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkImgPhoto.c

#tkImgUtil.$(OBJ): $(GENERIC_DIR)\tkImgUtil.c
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkImgUtil.c
tkOS2ImgUtil.$(OBJ): tkOS2ImgUtil.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) tkOS2ImgUtil.c

#tkTest.$(OBJ): $(GENERIC_DIR)\tkTest.c
#	$(CC) -c $(CFLAGS) $(GENERIC_DIR)\tkTest.c
tkTest.$(OBJ): tkTest.c
	$(CC) -c $(CFLAGS) tkTest.c

tkOS2Test.$(OBJ): tkOS2Test.c
	$(CC) -c $(CFLAGS) tkOS2Test.c

tkText.$(OBJ): $(GENERIC_DIR)\tkText.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkText.c

tkTextBTree.$(OBJ): $(GENERIC_DIR)\tkTextBTree.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTextBTree.c

tkTextDisp.$(OBJ): $(GENERIC_DIR)\tkTextDisp.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTextDisp.c

tkTextIndex.$(OBJ): $(GENERIC_DIR)\tkTextIndex.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTextIndex.c

tkTextImage.$(OBJ): $(GENERIC_DIR)\tkTextImage.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTextImage.c

tkTextMark.$(OBJ): $(GENERIC_DIR)\tkTextMark.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTextMark.c

tkTextTag.$(OBJ): $(GENERIC_DIR)\tkTextTag.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTextTag.c

tkTextWind.$(OBJ): $(GENERIC_DIR)\tkTextWind.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkTextWind.c

tkStubInit.$(OBJ): $(GENERIC_DIR)\tkStubInit.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(GENERIC_DIR)\tkStubInit.c

xcolors.$(OBJ): $(XLIB_DIR)\xcolors.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(XLIB_DIR)\xcolors.c

xdraw.$(OBJ): $(XLIB_DIR)\xdraw.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(XLIB_DIR)\xdraw.c

xgc.$(OBJ): $(XLIB_DIR)\xgc.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(XLIB_DIR)\xgc.c

ximage.$(OBJ): $(XLIB_DIR)\ximage.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(XLIB_DIR)\ximage.c

xutil.$(OBJ): $(XLIB_DIR)\xutil.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(XLIB_DIR)\xutil.c

tkUnixMenubu.$(OBJ): $(UNIX_DIR)\tkUnixMenubu.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(UNIX_DIR)\tkUnixMenubu.c

tkUnixScale.$(OBJ): $(UNIX_DIR)\tkUnixScale.c
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $(UNIX_DIR)\tkUnixScale.c

#tkStubLib.$(OBJ): $(GENERIC_DIR)\tkStubLib.c
#	$(CC) -c $(TK_CFLAGS) -DSTATIC_BUILD $(GENERIC_DIR)\tkStubLib.c
tkStubLib.$(OBJ): tkStubLib.c
	$(CC) -c $(TK_CFLAGS) -DSTATIC_BUILD $?

tkStubImg.$(OBJ): $(GENERIC_DIR)\tkStubImg.c
	$(CC) -c $(TK_CFLAGS) -DSTATIC_BUILD $(GENERIC_DIR)\tkStubImg.c
	

#####################

# Implicit Targets

# gmake doesn't support paths in the implicit rules (or does it???)
#{$(XLIB_DIR)}.c.$(OBJ):
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $<
#
#{$(GENERIC_DIR)}.c.$(OBJ):
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $<
#
#{$(ROOT)\unix}.c.$(OBJ):
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $<

.c.$(OBJ):
	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $<

#.c.$(OBJ):
#	$(CC) -c -DDLL_BUILD -DBUILD_tk $(TK_CFLAGS) $<

#.dll.$(ARCH):
#	$(IMPLIB) -c $@ $<

.rc.res:
	$(RC) -i$(INCLUDES) $<

#
# Special case object file targets
#

os2Main.$(OBJ) : os2Main.c
	$(CC) -c $(CFLAGS) $(INIT_PM_IN_APPINIT) os2Main.c

testMain.$(OBJ) : os2Main.c
	$(CC) -c $(CFLAGS) $(INIT_PM_IN_APPINIT) $(LINK_OUT)$@ -DTK_TEST \
        os2Main.c

#
# Executable targets
#

$(TKLIB): $(TKDLL) $(TKSTUBLIB)

$(TKOMFLIB): $(TKBASE).imp
	$(IMPLIB) -o $@ $(TKBASE).imp

$(TKARCHIVE): $(TKBASE).imp
	$(EMXIMP) -o $@ $(TKBASE).imp

$(TKBASE).imp: $(TKBASE).def
	$(IMPLIB) -o $(TKBASE).imp $(TKBASE).def

${TKSTUBLIB}: ${TKSTUBLIBOBJS}
	$(LIB) $(LIB_OUT)$@ ${TKSTUBLIBOBJS}

$(TKDLL): $(OS2TKOBJS) $(GENTKOBJS) $(TKSTUBOBJS) $(TKBASE).def $(TCLSTUBLIB) \
	  $(TKBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_dll) \
                $(OS2TKOBJS) $(GENTKOBJS) $(TKSTUBOBJS) \
	        $(TKBASE).def \
                $(TCLSTUBLIB)
	$(RC) $(TKBASE).res $(TKDLL)
ifndef DEBUGGING
	-$(LXLITE) $(TKDLL)
endif

$(TKBASE).res: $(TKBASE).rc rc\$(TKBASE).ico rc\buttons.bmp
	$(RCPRE) $(TKBASE).rc

$(WISH): $(WISHOBJS) $(TKLIB) $(TCLLIB) $(WISHBASE).def $(WISHBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_exe) $(WISHOBJS) $(TKLIB) $(TCLLIB) \
		$(WISHBASE).def
	$(RC) $(WISHBASE).res $(WISHBASE).exe
ifndef DEBUGGING
	-$(LXLITE) $(WISHBASE).exe
endif

$(WISHBASE).res: $(WISHBASE).rc rc\$(WISHBASE).ico
	$(RCPRE) $(WISHBASE).rc

$(TKTEST): $(TKTESTOBJS) $(TKLIB) $(TCLLIB) $(TKTESTBASE).def $(WISHBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_exe) $(TKTESTOBJS) $(TKLIB) \
		$(TCLLIB) $(TKTESTBASE).def
	$(RC) $(WISHBASE).res $(TKTESTBASE).exe
ifndef DEBUGGING
	-$(LXLITE) $(TKTESTBASE).exe
endif

# 
# Other dependencies
#

# The following rule automatically generates a $(TKBASE).def file containing
# an export entry for every public symbol in the $(TKDLL) library.

$(TKBASE).def: $(OS2TKOBJS) $(GENTKOBJS) $(TKSTUBOBJS) $(TKBASE)def.base
	type $(TKBASE)def.base > $(TKBASE).def
#	emxexp -o $(OS2TKOBJSPEC) $(GENTKOBJS) $(TKSTUBOBJS) >> $(TKBASE).def
# All OBJS's filenamse are too much for one command line invocation (of emxexp).
# However, each invocation of emxexp start with ordinal 1 unless given the
# option -o<ordinal>. We cannot (easily) determine that number, so we have
# two options: limit the command line by just using "tkOS2*.$(OBJ)" instead of
# all individual filenames, or using emxexp -p with an ordinal number guaranteed
# not to conflict. Present maximum number of generic objs is 696, so 1001
# would do. OS2 objs is below 100, so 1501 would do.
# Using tkOS2* only works if ALL those files should go into the DLL, and that
# isn't true, so we'll use the other option
	emxexp -o $(GENTKOBJS) >> $(TKBASE).def
	emxexp -o1001 $(TKSTUBOBJS) $(OS2TKOBJS) >> $(TKBASE).def

# Always make an a.out archive when creating distribution
aoutarchive: $(TKARCHIVE)
	-$(CP) $(TKARCHIVE) $(DISTDIR)\lib

#
# Regenerate the stubs files.
#
genstubs: $(GENERIC_DIR)\tkStubInit.c
$(GENERIC_DIR)\tkStubInit.c: tk.decls tkInt.decls
	set TCL_LIBRARY=$(TCLDIR)\library
	$(GENTCLSH) $(TCLLIBDIR)\genStubs.tcl $(GENERIC_DIR) \
                    tk.decls tkInt.decls
# distribution
dist: all aoutarchive install-binaries install-libraries

install-binaries: $(WISH) $(TKDLL) $(TKLIB) $(TKARCHIVE) $(TKSTUBLIB)
	-$(MKDIR) "$(DISTDIR)"
	$(CP) README PR.TXT $(DISTDIR)
	-$(MKDIR) "$(BIN_INSTALL_DIR)"
	@echo installing $(TKDLL)
	@copy "$(TKDLL)" "$(BIN_INSTALL_DIR)"
	@echo installing "$(WISH)"
	@copy "$(WISH)" "$(BIN_INSTALL_DIR)"
	-$(MKDIR) "$(LIB_INSTALL_DIR)"
	@copy "$(TKLIB)" "$(LIB_INSTALL_DIR)"
	@copy "$(TKARCHIVE)" "$(LIB_INSTALL_DIR)"
	@echo installing $(TKSTUBLIBNAME)
	@copy "$(TKSTUBLIB)" "$(LIB_INSTALL_DIR)"

install-libraries:
	@echo installing library
	-$(MKDIR) "$(DISTDIR)"
	$(CP) README PR.TXT $(DISTDIR)
	-@$(MKDIR) "$(LIB_INSTALL_DIR)"
	-@$(MKDIR) "$(INCLUDE_INSTALL_DIR)"
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)"
	-@copy "$(OS2_DIR)\tkOS2.h"              "$(INCLUDE_INSTALL_DIR)"
	-@copy "$(GENERIC_DIR)\tk.h"             "$(INCLUDE_INSTALL_DIR)"
	-@copy "$(GENERIC_DIR)\tkDecls.h"        "$(INCLUDE_INSTALL_DIR)"
	-@copy "$(GENERIC_DIR)\tkPlatDecls.h"    "$(INCLUDE_INSTALL_DIR)"
	-@copy "$(GENERIC_DIR)\tkIntXlibDecls.h" "$(INCLUDE_INSTALL_DIR)"
	-@xcopy "$(XLIB_DIR)\X11\*.h"            "$(INCLUDE_INSTALL_DIR)\X11"
	-@mkd "$(SCRIPT_INSTALL_DIR)"
	-@mkd "$(SCRIPT_INSTALL_DIR)\images"
	-@mkd "$(SCRIPT_INSTALL_DIR)\demos"
	-@mkd "$(SCRIPT_INSTALL_DIR)\demos\images"
	-@xcopy "$(ROOT)\library"              "$(SCRIPT_INSTALL_DIR)"
	-@xcopy "$(ROOT)\library\images"       "$(SCRIPT_INSTALL_DIR)\images"
	-@xcopy "$(ROOT)\library\demos"        "$(SCRIPT_INSTALL_DIR)\demos"
	-@xcopy "$(ROOT)\library\demos\images" "$(SCRIPT_INSTALL_DIR)\demos\images"

# remove all generated files
clean:
	-$(RM) $(TKBASE) $(TKDLL) $(TKBASE).res $(TKBASE).map $(TKBASE).def
	-$(RM) $(WISHBASE) $(WISH) $(WISHBASE).res $(WISHBASE).map
	-$(RM) $(TKTESTBASE) $(TKTEST) $(TKTEST).map
	-$(RM) *.$(OBJ) *.imp

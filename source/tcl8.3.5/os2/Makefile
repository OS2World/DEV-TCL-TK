#
# Makefile for compiling Tcl for OS/2 with EMX/GCC
#

#----------------------------
### Build choices

# uncomment the following line to compile with symbols
#DEBUGGING=1

# uncomment the following line to compile with debugging printf's.
#VERBOSE=1

# Uncomment the following to make binaries that run only on Warp or higher
#TARGET_WARP=yes

# Uncomment the following line to compile with thread support
THREADDEFINES = -DTCL_THREADS

# Format is OMF (.obj, debuggable by IPMD, TD, TDOS2, ....)
# or A.OUT (.o, debuggable by GDB/PMGDB)
#FORMAT = A.OUT
FORMAT = OMF

# Where to build target dist
DISTDIR = d:\TclTk835\TclTk

# Version
VERSION = 83
DOTVERSION = 8.3
FULLVERSION = 8.3.5
SHORTVERSION = 835

BIN_INSTALL_DIR = $(DISTDIR)\bin
DOC_INSTALL_DIR = $(DISTDIR)\doc
LIB_INSTALL_DIR = $(DISTDIR)\lib
INCLUDE_INSTALL_DIR = $(DISTDIR)\include
SCRIPT_INSTALL_DIR = $(LIB_INSTALL_DIR)\tcl$(DOTVERSION)

# Compiled-in fallback default for library directory and package path
FB_TCL_LIBRARY = \"C:/Tcl$(SHORTVERSION)/lib\"
FB_TCL_PACKAGE_PATH = \"C:/Tcl$(SHORTVERSION)/lib\"

# The following defines can be used to control the amount of debugging
# code that is added to the compilation.
#
#       -DTCL_MEM_DEBUG         Enables the debugging memory allocator.
#       -DTCL_COMPILE_DEBUG     Enables byte compilation logging.
#       -DTCL_COMPILE_STATS     Enables byte compilation statistics gathering.
#       -DUSE_TCLALLOC=0        Disables the Tcl memory allocator in favor
#                               of the native malloc implementation.  This is
#                               needed when using Purify.  For IA64, we do
#                               want to use the native allocator.
#
#DEBUGDEFINES = -DTCL_MEM_DEBUG -DTCL_COMPILE_DEBUG -DTCL_COMPILE_STATS
DEBUGDEFINES = -DTCL_MEM_DEBUG

### End of build choices
#----------------------------
 

#
# Project directories
#
# TMPDIR = location where .$(OBJ) files should be stored during build
# TCLDIR = location of top of Tcl source hierarchy
#

ROOT    = ..
GENERIC_DIR = $(ROOT)\generic
COMPAT_DIR = $(ROOT)\compat
OS2_DIR = $(ROOT)\os2
TMPDIR	= .
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
   ARCH=a
   OBJ=o
   OUTPUTFLAG=
   EXTRALINK =
   # Careful, there has to be a space after '-o' !
   LINK_OUT = -o 
   SOCKLIB = -lsocket
   # No space after definition
   RC = emxbind -br
   IMPLIB  = emximp
   LIB	= ar
   # Space after definition!
   LIB_OUT = rc 
else
   ARCH=lib
   OBJ=obj
   OUTPUTFLAG=-Zomf
   EXTRALINK = -Zlinker /FARCALLTRANSLATION -Zlinker /RUNFROMVDM -Zlinker /BASE:0x10000 -Zlinker /ALIGN:2 -Zlinker $(EXEPACKING)
   # Careful, there has to be a space after '-o' !
   LINK_OUT = -o 
   SOCKLIB = $(EMXDIR)/lib/socket.$(ARCH)
   # NB! space after definition!
   RC = rc -p $(RESPACKING) 
   IMPLIB  = emximp
   LIB	= emxomfar
   # Space after definition!
   LIB_OUT = rc 
endif
RCPRE = rc -r -p $(RESPACKING) 

OUTDIR     = .
NAMEPREFIX = tcl
STUBPREFIX = $(NAMEPREFIX)stub
TCLBASE     = $(NAMEPREFIX)$(VERSION)
TCLLIB      = $(TCLBASE).$(ARCH)
TCLOMFLIB   = $(TCLBASE).lib
TCLARCHIVE  = $(TCLBASE).a
TCLDLL      = $(TCLBASE).dll
TCLSTUB     = $(TCLBASE)

TCLSTUBLIBNAME  = $(STUBPREFIX)$(VERSION).lib
TCLSTUBLIB      = $(TCLSTUBLIBNAME)

TCLSHPMBASE = $(NAMEPREFIX)pm$(VERSION)
TCLSHPM     = $(TCLSHPMBASE).exe
TCLSHPMDEF  = $(TCLSHPMBASE).def
TCLSHBASE   = $(NAMEPREFIX)sh$(VERSION)
TCLSH       = $(TCLSHBASE).exe
TCLSHDEF    = $(TCLSHBASE).def
TCLSHPBASE  = $(NAMEPREFIX)shp$(VERSION)
TCLSHP      = $(TCLSHPBASE).exe
TCLSHPDEF   = $(TCLSHPBASE).def

TCLTESTBASE = $(NAMEPREFIX)tst$(VERSION)
TCLTEST     = $(TCLTESTBASE).exe

TCLREGDLL   = $(NAMEPREFIX)reg$(VERSION).dll
TCLREGDEF   = $(NAMEPREFIX)reg$(VERSION).def

TCLDDEDLL   = $(NAMEPREFIX)dde$(VERSION).dll
TCLDDEDEF   = $(NAMEPREFIX)dde$(VERSION).def

TCLPLUGINDLL = $(NAMEPREFIX)p.dll
TCLPLUGINDEF = $(NAMEPREFIX)p.def

LIB_INSTALL_DIR = $(DISTDIR)\lib
BIN_INSTALL_DIR = $(DISTDIR)\bin
SCRIPT_INSTALL_DIR      = $(DISTDIR)\lib\tcl$(DOTVERSION)
INCLUDE_INSTALL_DIR     = $(DISTDIR)\include

# GENTCLSH is used to generate the help file (INF file), should be in path
#GENTCLSH = tclsh76
GENTCLSH = tclsh80
CC      = gcc -c
LINK    = gcc
CP      = copy
RM      = del /f
MKDIR   = mkdir

EMXIMP  = emximp

DEFINES =       -D__OS2__ -DHAS_DIRENT -DHAS_STDARG -D"wchar_t=unsigned short" \
	        -D_WCHAR_T $(THREADDEFINES)
STD_INCLUDES =  -I$(OS2_DIR) -I$(EMXDIR)/include
TCL_INCLUDES =  -I$(OS2_DIR) -I$(GENERIC_DIR)
INCLUDES =	$(STD_INCLUDES) $(TCL_INCLUDES)
LIBDIRS =	-L$(EMXDIR)/lib -L$(OS2_DIR)

ifdef VERBOSE
  VERBOSEDEFINES=-DVERBOSE
else
  VERBOSEDEFINES=
endif

ifndef DEBUGGING
  # these macros cause maximum optimization and no symbols
  CFLAGS =	-O -Wall -mprobe -fstack-check -m486 $(OUTPUTFLAG) -Zmtd -s \
                $(DEFINES) $(INCLUDES) $(VERBOSEDEFINES)
else
  # these macros enable debugging
  CFLAGS =	-g -mprobe -fstack-check -m486 $(OUTPUTFLAG) -Zmtd \
                -fhandle-exceptions -DTCL_DEBUG \
  		$(DEFINES) $(DEBUGDEFINES) $(INCLUDES) $(VERBOSEDEFINES)
endif
CC_SWITCHES =   $(CFLAGS)
TCL_CFLAGS  =   $(CFLAGS)

ifndef DEBUGGING
  LNFLAGS_exe = $(OUTPUTFLAG) -Zmtd $(EXTRALINK) -lwrap -s
  LNFLAGS_dll = -mprobe -Zcrtdll -Zdll $(OUTPUTFLAG) -Zmt \
                $(EXTRALINK) -lm -lwrap -s
else
  LNFLAGS_exe = $(OUTPUTFLAG) -Zmtd $(EXTRALINK)
  LNFLAGS_dll = -mprobe -Zcrtdll -Zdll $(OUTPUTFLAG) -Zmt $(EXTRALINK) -lm
endif

.SUFFIXES: .c .dll .$(ARCH) .$(OBJ) .exe .rc .res

TCLSHPMOBJS = \
	tclAppInit.$(OBJ)

TCLSHOBJS = \
	tclAppInitC.$(OBJ)

# PM version
#TCLTESTOBJS = \
#	tclTest.$(OBJ) \
#	tclTestObj.$(OBJ) \
#	tclTestProcBodyObj.$(OBJ) \
#	tclThreadTest.$(OBJ) \
#	tclOS2Test.$(OBJ) \
#	testMain.$(OBJ)
# CLI version
TCLTESTOBJS = \
	tclTest.$(OBJ) \
	tclTestObj.$(OBJ) \
	tclTestProcBodyObj.$(OBJ) \
	tclThreadTest.$(OBJ) \
	tclOS2Test.$(OBJ) \
	testMainC.$(OBJ)

OS2_OBJS = \
	tclOS2Alloc.$(OBJ) \
	tclOS2Chan.$(OBJ) \
	tclOS2Console.$(OBJ) \
	tclOS2Dll.$(OBJ) \
	tclOS2Error.$(OBJ) \
	tclOS2FCmd.$(OBJ) \
	tclOS2File.$(OBJ) \
	tclOS2Init.$(OBJ) \
	tclOS2Load.$(OBJ) \
	tclOS2Main.$(OBJ) \
	tclOS2Mtherr.$(OBJ) \
	tclOS2Notify.$(OBJ) \
	tclOS2Pipe.$(OBJ) \
	tclOS2Serial.$(OBJ) \
	tclOS2Sock.$(OBJ) \
	tclOS2Thrd.$(OBJ) \
	tclOS2Time.$(OBJ)

GENERIC_OBJS = \
	regcomp.$(OBJ) \
	regexec.$(OBJ) \
	regfree.$(OBJ) \
	regerror.$(OBJ) \
	tclAlloc.$(OBJ) \
	tclAsync.$(OBJ) \
	tclBasic.$(OBJ) \
	tclBinary.$(OBJ) \
	tclCkalloc.$(OBJ) \
	tclClock.$(OBJ) \
	tclCmdAH.$(OBJ) \
	tclCmdIL.$(OBJ) \
	tclCmdMZ.$(OBJ) \
	tclCompCmds.$(OBJ) \
	tclCompExpr.$(OBJ) \
	tclCompile.$(OBJ) \
	tclDate.$(OBJ) \
	tclEncoding.$(OBJ) \
	tclEnv.$(OBJ) \
	tclEvent.$(OBJ) \
	tclExecute.$(OBJ) \
	tclFCmd.$(OBJ) \
	tclFileName.$(OBJ) \
	tclGet.$(OBJ) \
	tclHash.$(OBJ) \
	tclHistory.$(OBJ) \
	tclIndexObj.$(OBJ) \
	tclInterp.$(OBJ) \
	tclIO.$(OBJ) \
	tclIOCmd.$(OBJ) \
	tclIOGT.$(OBJ) \
	tclIOSock.$(OBJ) \
	tclIOUtil.$(OBJ) \
	tclLink.$(OBJ) \
	tclLiteral.$(OBJ) \
	tclListObj.$(OBJ) \
	tclLoad.$(OBJ) \
	tclNamesp.$(OBJ) \
	tclNotify.$(OBJ) \
	tclObj.$(OBJ) \
	tclPanic.$(OBJ) \
	tclParse.$(OBJ) \
	tclParseExpr.$(OBJ) \
	tclPipe.$(OBJ) \
	tclPkg.$(OBJ) \
	tclPosixStr.$(OBJ) \
	tclPreserve.$(OBJ) \
	tclProc.$(OBJ) \
	tclRegexp.$(OBJ) \
	tclResolve.$(OBJ) \
	tclResult.$(OBJ) \
	tclScan.$(OBJ) \
	tclStringObj.$(OBJ) \
	tclStubInit.$(OBJ) \
	tclStubLib.$(OBJ) \
	tclThread.$(OBJ) \
	tclTimer.$(OBJ) \
	tclUtf.$(OBJ) \
	tclUtil.$(OBJ) \
	tclVar.$(OBJ)

COMPAT_OBJS = \
	strftime.$(OBJ)

DDE_OBJS = tclOS2Dde.$(OBJ) tclOS2DdeMl.$(OBJ)

REG_OBJS = tclOS2Reg.$(OBJ)

TCLSTUBOBJS = tclStubLib.$(OBJ)

TCLSH_OBJS = tclAppInit.$(OBJ)

TCL_OBJS = $(GENERIC_OBJS) $(OS2_OBJS) $(COMPAT_OBJS)

TCLDLLOBJS = $(TCL_OBJS)

TCL_DOCS = $(ROOT)/doc/*.[13n]

#
# Targets
#

all:  $(TCLDLL) $(TCLSHPM) $(TCLSH) $(TCLREGDLL) $(TCLDDEDLL) $(TCLTEST)
test: $(TCLDLL) $(TCLTEST)
dlls:	$(TCLDLL) $(TCLREGDLL) $(TCLDDEDLL)
plugin: $(TCLPLUGINDLL) $(TCLSHP)
install: install-binaries install-libraries
doc: tcl$(SHORTVERSION).inf

# OS/2 binaries:

# The following objects should be built using the stub interfaces
tclOS2Reg.$(OBJ): tclOS2Reg.c
	$(CC) $(CC_SWITCHES) -DUSE_TCL_STUBS tclOS2Reg.c
tclOS2Dde.$(OBJ): tclOS2Dde.c
	$(CC) $(CC_SWITCHES) -DUSE_TCL_STUBS tclOS2Dde.c
tclOS2DdeMl.$(OBJ): tclOS2DdeMl.c
	$(CC) $(CC_SWITCHES) -DUSE_TCL_STUBS tclOS2DdeMl.c

tclAppInit.$(OBJ): tclAppInit.c
	$(CC) $(CC_SWITCHES) tclAppInit.c

# CLI version
tclAppInitC.$(OBJ): tclAppInit.c
	$(CC) $(CC_SWITCHES) $(LINK_OUT)$@ -DCLI_VERSION tclAppInit.c

# The following objects are part of the stub library and should not
# be built as DLL objects but none of the symbols should be exported

tclStubLib.$(OBJ) : $(GENERIC_DIR)\tclStubLib.c
	$(CC) -c $(TCL_CFLAGS) -DSTATIC_BUILD $?

# Dedependency rules

$(GENERIC_DIR)\regcomp.c: \
	$(GENERIC_DIR)\regguts.h \
	$(GENERIC_DIR)\regc_lex.c \
	$(GENERIC_DIR)\regc_color.c \
	$(GENERIC_DIR)\regc_nfa.c \
	$(GENERIC_DIR)\regc_cvec.c \
	$(GENERIC_DIR)\regc_locale.c
$(GENERIC_DIR)\regcustom.h: \
	$(GENERIC_DIR)\tclInt.h \
	$(GENERIC_DIR)\tclPort.h \
	$(GENERIC_DIR)\regex.h
$(GENERIC_DIR)\regexec.c: \
	$(GENERIC_DIR)\rege_dfa.c \
	$(GENERIC_DIR)\regguts.h
$(GENERIC_DIR)\regerror.c: $(GENERIC_DIR)\regguts.h
$(GENERIC_DIR)\regfree.c: $(GENERIC_DIR)\regguts.h
$(GENERIC_DIR)\regfronts.c: $(GENERIC_DIR)\regguts.h
$(GENERIC_DIR)\regguts.h: $(GENERIC_DIR)\regcustom.h

#
# Implicit rules
#

.c.$(OBJ):
	$(CC) -DBUILD_tcl $(TCL_CFLAGS) $<

{$(GENERIC_DIR)}.c{$(TMPDIR)}.$(OBJ):
	$(CC) -DBUILD_tcl $(TCL_CFLAGS) $<

{$(ROOT)\compat}.c.$(OBJ):
	$(CC) -DBUILD_tcl $(TCL_CFLAGS) $<

.rc.res:
	$(RCPRE) -i$(INCLUDES) $(TCL_DEFINES) $<

# Generic binaries:

panic.$(OBJ): $(GENERIC_DIR)/panic.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/panic.c

regcomp.$(OBJ): $(GENERIC_DIR)/regcomp.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/regcomp.c

regerror.$(OBJ): $(GENERIC_DIR)/regerror.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/regerror.c

regexec.$(OBJ): $(GENERIC_DIR)/regexec.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/regexec.c

regexp.$(OBJ): $(GENERIC_DIR)/regexp.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/regexp.c

regfree.$(OBJ): $(GENERIC_DIR)/regfree.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/regfree.c

#tcl2c.$(OBJ): $(GENERIC_DIR)/tcl2c.c
#	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tcl2c.c
tcl2c.$(OBJ): tcl2c.c
	$(CC) $(CC_SWITCHES) tcl2c.c

tclAlloc.$(OBJ): $(GENERIC_DIR)/tclAlloc.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclAlloc.c

tclAsync.$(OBJ): $(GENERIC_DIR)/tclAsync.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclAsync.c

tclBasic.$(OBJ): $(GENERIC_DIR)/tclBasic.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclBasic.c

tclBinary.$(OBJ): $(GENERIC_DIR)/tclBinary.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclBinary.c

tclCkalloc.$(OBJ): $(GENERIC_DIR)/tclCkalloc.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclCkalloc.c

tclClock.$(OBJ): $(GENERIC_DIR)/tclClock.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclClock.c

#tclCmdAH.$(OBJ): $(GENERIC_DIR)/tclCmdAH.c
#	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclCmdAH.c
tclCmdAH.$(OBJ): tclCmdAH.c
	$(CC) $(CC_SWITCHES) tclCmdAH.c

tclCmdIL.$(OBJ): $(GENERIC_DIR)/tclCmdIL.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclCmdIL.c

tclCmdMZ.$(OBJ): $(GENERIC_DIR)/tclCmdMZ.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclCmdMZ.c

tclCompCmds.$(OBJ): $(GENERIC_DIR)/tclCompCmds.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclCompCmds.c

tclCompExpr.$(OBJ): $(GENERIC_DIR)/tclCompExpr.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclCompExpr.c

tclCompile.$(OBJ): $(GENERIC_DIR)/tclCompile.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclCompile.c

tclDate.$(OBJ): $(GENERIC_DIR)/tclDate.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclDate.c

tclEncoding.$(OBJ): $(GENERIC_DIR)/tclEncoding.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclEncoding.c

tclEnv.$(OBJ): $(GENERIC_DIR)/tclEnv.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclEnv.c

tclEvent.$(OBJ): $(GENERIC_DIR)/tclEvent.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclEvent.c

tclExecute.$(OBJ): $(GENERIC_DIR)/tclExecute.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclExecute.c

tclFCmd.$(OBJ): $(GENERIC_DIR)/tclFCmd.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclFCmd.c

#tclFileName.$(OBJ): $(GENERIC_DIR)/tclFileName.c
#	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclFileName.c
tclFileName.$(OBJ): tclFileName.c
	$(CC) $(CC_SWITCHES) tclFileName.c

tclGet.$(OBJ): $(GENERIC_DIR)/tclGet.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclGet.c

tclHash.$(OBJ): $(GENERIC_DIR)/tclHash.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclHash.c

tclHistory.$(OBJ): $(GENERIC_DIR)/tclHistory.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclHistory.c

tclIndexObj.$(OBJ): $(GENERIC_DIR)/tclIndexObj.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclIndexObj.c

tclInterp.$(OBJ): $(GENERIC_DIR)/tclInterp.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclInterp.c

tclIO.$(OBJ): $(GENERIC_DIR)/tclIO.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclIO.c

tclIOCmd.$(OBJ): $(GENERIC_DIR)/tclIOCmd.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclIOCmd.c

tclIOGT.$(OBJ): $(GENERIC_DIR)/tclIOGT.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclIOGT.c

tclIOSock.$(OBJ): $(GENERIC_DIR)/tclIOSock.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclIOSock.c

tclIOUtil.$(OBJ): $(GENERIC_DIR)/tclIOUtil.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclIOUtil.c

tclLink.$(OBJ): $(GENERIC_DIR)/tclLink.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclLink.c

tclListObj.$(OBJ): $(GENERIC_DIR)/tclListObj.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclListObj.c

tclLiteral.$(OBJ): $(GENERIC_DIR)/tclLiteral.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclLiteral.c

#tclLoad.$(OBJ): $(GENERIC_DIR)/tclLoad.c
#	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclLoad.c
tclLoad.$(OBJ): tclLoad.c
	$(CC) $(CC_SWITCHES) tclLoad.c

tclMain.$(OBJ): $(GENERIC_DIR)/tclMain.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclMain.c

tclNamesp.$(OBJ): $(GENERIC_DIR)/tclNamesp.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclNamesp.c

tclNotify.$(OBJ): $(GENERIC_DIR)/tclNotify.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclNotify.c
#tclNotify.$(OBJ): tclNotify.c
#	$(CC) $(CC_SWITCHES) tclNotify.c

tclObj.$(OBJ): $(GENERIC_DIR)/tclObj.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclObj.c

tclPanic.$(OBJ): $(GENERIC_DIR)/tclPanic.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclPanic.c

# When compiling to A.OUT, the typeTable in ..\generic\tclParse.c cannot be
# exported by emxbind because it's of type BSS. Make it static in tclParse.c:
#tclParse.$(OBJ): tclParse.c
tclParse.$(OBJ): $(GENERIC_DIR)/tclParse.c
	$(CC) $(CC_SWITCHES) $?

tclParseExpr.$(OBJ): $(GENERIC_DIR)/tclParseExpr.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclParseExpr.c

tclPipe.$(OBJ): $(GENERIC_DIR)/tclPipe.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclPipe.c

tclPkg.$(OBJ): $(GENERIC_DIR)/tclPkg.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclPkg.c

tclPosixStr.$(OBJ): $(GENERIC_DIR)/tclPosixStr.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclPosixStr.c

tclPreserve.$(OBJ): $(GENERIC_DIR)/tclPreserve.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclPreserve.c

tclProc.$(OBJ): $(GENERIC_DIR)/tclProc.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclProc.c

tclRegexp.$(OBJ): $(GENERIC_DIR)/tclRegexp.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclRegexp.c

tclResolve.$(OBJ): $(GENERIC_DIR)/tclResolve.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclResolve.c

tclResult.$(OBJ): $(GENERIC_DIR)/tclResult.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclResult.c

tclScan.$(OBJ): $(GENERIC_DIR)/tclScan.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclScan.c

tclStringObj.$(OBJ): $(GENERIC_DIR)/tclStringObj.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclStringObj.c

tclStubInit.$(OBJ): $(GENERIC_DIR)/tclStubInit.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclStubInit.c

#tclTest.$(OBJ): $(GENERIC_DIR)/tclTest.c
#	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclTest.c
tclTest.$(OBJ): tclTest.c
	$(CC) $(CC_SWITCHES) tclTest.c

tclTestObj.$(OBJ): $(GENERIC_DIR)/tclTestObj.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclTestObj.c

tclTestProcBodyObj.$(OBJ): $(GENERIC_DIR)/tclTestProcBodyObj.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclTestProcBodyObj.c

tclThread.$(OBJ): $(GENERIC_DIR)/tclThread.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclThread.c

tclThreadTest.$(OBJ): $(GENERIC_DIR)/tclThreadTest.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclThreadTest.c

tclTimer.$(OBJ): $(GENERIC_DIR)/tclTimer.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclTimer.c

tclUtf.$(OBJ): $(GENERIC_DIR)/tclUtf.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclUtf.c

tclUtil.$(OBJ): $(GENERIC_DIR)/tclUtil.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclUtil.c

tclVar.$(OBJ): $(GENERIC_DIR)/tclVar.c
	$(CC) $(CC_SWITCHES) $(GENERIC_DIR)/tclVar.c

# compat binaries

fixstrtod.$(OBJ): $(COMPAT_DIR)/fixstrtod.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/fixstrtod.c

gettod.$(OBJ): $(COMPAT_DIR)/gettod.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/gettod.c

memcmp.$(OBJ): $(COMPAT_DIR)/memcmp.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/memcmp.c

opendir.$(OBJ): $(COMPAT_DIR)/opendir.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/opendir.c

strftime.$(OBJ): $(COMPAT_DIR)/strftime.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/strftime.c

strncasecmp.$(OBJ): $(COMPAT_DIR)/strncasecmp.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/strncasecmp.c

strstr.$(OBJ): $(COMPAT_DIR)/strstr.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/strstr.c

strtod.$(OBJ): $(COMPAT_DIR)/strtod.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/strtod.c

strtol.$(OBJ): $(COMPAT_DIR)/strtol.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/strtol.c

strtoul.$(OBJ): $(COMPAT_DIR)/strtoul.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/strtoul.c

tmpnam.$(OBJ): $(COMPAT_DIR)/tmpnam.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/tmpnam.c

waitpid.$(OBJ): $(COMPAT_DIR)/waitpid.c
	$(CC) $(CC_SWITCHES) $(COMPAT_DIR)/waitpid.c

#
# Special case object file targets
#

testMain.$(OBJ) : tclAppInit.c
	$(CC) -DTCL_TEST $(CC_SWITCHES) $(LINK_OUT)$@ tclAppInit.c

# CLI version
testMainC.$(OBJ) : tclAppInit.c
	$(CC) -DTCL_TEST -DCLI_VERSION $(CC_SWITCHES) $(LINK_OUT)$@ tclAppInit.c


#
# Executable targets
#

$(TCLDLL): $(TCLDLLOBJS) $(TCLBASE).def $(TCLBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_dll) $(TCLDLLOBJS) $(TCLBASE).def \
		$(SOCKLIB)
	$(RC)$(TCLBASE).res $(TCLDLL)
ifndef DEBUGGING
	-$(LXLITE) $(TCLDLL)
endif

$(TCLSTUBLIB): $(TCLSTUBOBJS)
	$(LIB) $(LIB_OUT)$@ $(TCLSTUBOBJS)

$(TCLPLUGINLIB): $(TCLPLUGINDLL)

$(TCLPLUGINDLL): $(TCL_OBJS) $(TCLPLUGINDEF) $(TCLBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_dll) $(TCL_OBJS) $(TCLPLUGINDEF)
	$(RC)$(TCLBASE).res $(TCLPLUGINDLL)
ifndef DEBUGGING
	-$(LXLITE) $(TCLPLUGINDLL)
endif

$(TCLREGDLL): $(REG_OBJS) $(TCLSTUBLIB) $(TCLREGDEF) $(TCLBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_dll) $(REG_OBJS) $(TCLSTUBLIB) \
		$(TCLREGDEF)
	$(RC)$(TCLBASE).res $(TCLREGDLL)
ifndef DEBUGGING
	-$(LXLITE) $(TCLREGDLL)
endif

$(TCLDDEDLL): $(DDE_OBJS).$(OBJ) $(TCLSTUBLIB) $(TCLDDEDEF) $(TCLBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_dll) $(DDE_OBJS) $(TCLSTUBLIB) \
                $(TCLDDEDEF)
	$(RC)$(TCLBASE).res $(TCLDDEDLL)
ifndef DEBUGGING
	-$(LXLITE) $(TCLDDEDLL)
endif

$(TCLBASE).res: $(TCLBASE).rc
	$(RCPRE) $(TCLBASE).rc


$(TCLSHPM): $(TCLSHPMOBJS) $(TCLLIB) $(TCLSHPMDEF) $(TCLSHPMBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_exe) $(TCLSHPMOBJS) $(TCLLIB) $(TCLSHPMDEF)
	$(RC)$(TCLSHPMBASE).res $(TCLSHPM)
ifndef DEBUGGING
	-$(LXLITE) $(TCLSHPM)
endif

$(TCLSH): $(TCLSHOBJS) $(TCLLIB) $(TCLSHDEF) $(TCLSHBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_exe) $(TCLSHOBJS) $(TCLLIB) $(TCLSHDEF)
	$(RC)$(TCLSHBASE).res $(TCLSH)
ifndef DEBUGGING
	-$(LXLITE) $(TCLSH)
endif

$(TCLSHPMBASE).res: $(TCLSHPMBASE).rc
	$(RCPRE) $(TCLSHPMBASE).rc

$(TCLSHBASE).res: $(TCLSHBASE).rc
	$(RCPRE) $(TCLSHBASE).rc

$(TCLSHP): $(TCLSHOBJS) $(TCLPLUGINLIB) $(TCLSHBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_dll) $(TCLSHOBJS) $(TCLPLUGINLIB)
	$(RC)$(TCLSHBASE).res $(TCLSHP)
ifndef DEBUGGING
	-$(LXLITE) $(TCLSHP)
endif

#PM version
#$(TCLTEST): $(TCLTESTOBJS) $(TCLLIB) $(TCLSHPMDEF) $(TCLSHPMBASE).res
#	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_exe) $(TCLTESTOBJS) $(TCLLIB) \
#		$(TCLSHPMDEF)
#	$(RC)$(TCLSHPMBASE).res $(TCLTEST)
#ifndef DEBUGGING
#	-$(LXLITE) $(TCLTEST)
#endif
#CLI version
$(TCLTEST): $(TCLTESTOBJS) $(TCLLIB) $(TCLSHDEF) $(TCLSHBASE).res
	$(LINK) $(LINK_OUT)$@ $(LNFLAGS_exe) $(TCLTESTOBJS) $(TCLLIB) \
		$(TCLSHDEF)
	$(RC)$(TCLSHBASE).res $(TCLTEST)
ifndef DEBUGGING
	-$(LXLITE) $(TCLTEST)
endif

$(TCLOMFLIB): $(TCLBASE).imp
	$(IMPLIB) -o $(TCLOMFLIB) $(TCLBASE).imp

$(TCLBASE).imp: $(TCLBASE).def
	$(IMPLIB) $(LINK_OUT)$@ $(TCLBASE).def

$(TCLARCHIVE): $(TCLBASE).imp
	$(EMXIMP) -o $(TCLARCHIVE) $(TCLBASE).imp

# The following rule automatically generates a tcl<version>.def file containing
# an export entry for every public symbol in the tcl<version>.dll library.

$(TCLBASE).def: $(TCLBASE)def.base $(TCLDLLOBJS)
	type $(TCLBASE)def.base > $(TCLBASE).def
#	emxexp -o $(TCLDLLOBJS) >> $(TCLBASE).def
# All OBJS's filenamse are too much for one command line invocation (of emxexp).
# However, each invocation of emxexp start with ordinal 1 unless given the
# option -o<ordinal>. We cannot (easily) determine that number, so we have
# two options: limit the command line by just using "tclOS2*.obj" instead of
# all individual filenames, or using emxexp -p with an ordinal number guaranteed
# not to conflict. Present maximum number of generic objs is 696, so 1001
# would do. OS2 objs is below 100, so 1501 would do.
# Using tclOS2* only works if ALL those files should go into the DLL, and that
# isn't true, so we'll use the other option
#COMPAT_OBJS may be empty, so use behind OS2_OBJS
	emxexp -o $(GENERIC_OBJS) >> $(TCLBASE).def
	emxexp -o1001 $(OS2_OBJS) $(COMPAT_OBJS) >> $(TCLBASE).def
#	emxexp -o1501 $(COMPAT_OBJS) >> $(TCLBASE).def
#	emxexp -o $(GENERIC_OBJS) tclOS2*.obj >> $(TCLBASE).def

# make documentation
tcl$(SHORTVERSION).inf: tcl$(SHORTVERSION).ipf
	ipfc /inf tcl$(SHORTVERSION).ipf

tcl$(SHORTVERSION).ipf: man2tcl.exe ..\doc\registry.n
	$(GENTCLSH) man2ipf.tcl tcl $(FULLVERSION) \
		../../tcl$(FULLVERSION)/doc \
		../../tk$(FULLVERSION)/doc \
		/oratcl-2.5/doc/oratcl.man
man2tcl.exe: man2tcl.c
	gcc $(OUTPUTFLAG) man2tcl.c $(LINK_OUT)$@

..\doc\registry.n: registry.n
	copy registry.n ..\doc

# Always make an a.out archive when creating distribution
aoutarchive: $(TCLARCHIVE)
	-$(CP) $(TCLARCHIVE) $(DISTDIR)\lib

#
# Regenerate the stubs files.
#

genstubs: $(GENERIC_DIR)/tclStubInit.c
#$(GENERIC_DIR)/tclStubInit.c: $(GENERIC_DIR)/tcl.decls \
#                $(GENERIC_DIR)/tclInt.decls
#	@TCL_LIBRARY="$(ROOT_DIR)/library"; export TCL_LIBRARY; \
#	tclsh$(VERSION) $(ROOT)/tools/genStubs.tcl $(GENERIC_DIR) \
#		$(GENERIC_DIR)/tcl.decls $(GENERIC_DIR)/tclInt.decls
$(GENERIC_DIR)/tclStubInit.c: tcl.decls tclInt.decls
	set TCL_LIBRARY=$(ROOT)/library
	$(GENTCLSH) $(ROOT)/os2/genStubs.tcl $(GENERIC_DIR) \
		tcl.decls tclInt.decls

# distribution
dist: all doc aoutarchive install-binaries install-libraries

install-binaries: $(TCLSH) $(TCLSHPM) $(TCLDLL) \
                  $(TCLLIB) $(TCLARCHIVE) $(TCLSTUBLIB)
	-@$(MKDIR) "$(DISTDIR)"
	-@$(MKDIR) "$(BIN_INSTALL_DIR)"
	-@$(MKDIR) "$(LIB_INSTALL_DIR)"
	-@echo installing $(TCLDLL)
	-@$(CP) "$(TCLDLL)" "$(BIN_INSTALL_DIR)"
	-@$(CP) "$(TCLLIB)" "$(LIB_INSTALL_DIR)"
	-@$(CP) "$(TCLARCHIVE)" "$(LIB_INSTALL_DIR)"
	-@echo installing "$(TCLSH)"
	-@$(CP) "$(TCLSH)" "$(BIN_INSTALL_DIR)"
	-@$(CP) man2tcl.exe $(BIN_INSTALL_DIR)\bin
	-@$(MKDIR) $(DOC_INSTALL_DIR)
	-@$(CP) tcl$(SHORTVERSION).inf $(DOC_INSTALL_DIR)
	-@$(CP) man2ipf.tcl $(DOC_INSTALL_DIR)
	-@$(CP) man2ipf2.tcl $(DOC_INSTALL_DIR)
	-@echo installing "$(TCLSHPM)"
	-@$(CP) "$(TCLSHPM)" "$(BIN_INSTALL_DIR)"
	-@echo installing $(TCLSTUBLIBNAME)
	-@$(CP) "$(TCLSTUBLIB)" "$(LIB_INSTALL_DIR)"

install-libraries: doc
	-@$(MKDIR) "$(DISTDIR)"
	-@$(MKDIR) "$(LIB_INSTALL_DIR)"
	-@$(MKDIR) "$(INCLUDE_INSTALL_DIR)"
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)"
	-@echo installing http1.0
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)\http1.0"
	-@$(CP) "$(ROOT)\library\http1.0\http.tcl"     "$(SCRIPT_INSTALL_DIR)\http1.0"
	-@$(CP) "$(ROOT)\library\http1.0\pkgIndex.tcl" "$(SCRIPT_INSTALL_DIR)\http1.0"
	-@echo installing http2.4
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)\http2.4"
	-@$(CP) "$(ROOT)\library\http\http.tcl"     "$(SCRIPT_INSTALL_DIR)\http2.4"
	-@$(CP) "$(ROOT)\library\http\pkgIndex.tcl" "$(SCRIPT_INSTALL_DIR)\http2.4"
	-@echo installing opt0.4
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)\opt0.4"
	-@$(CP) "$(ROOT)\library\opt\optparse.tcl" "$(SCRIPT_INSTALL_DIR)\opt0.4"
	-@$(CP) "$(ROOT)\library\opt\pkgIndex.tcl" "$(SCRIPT_INSTALL_DIR)\opt0.4"
	-@echo installing msgcat1.1
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)\msgcat1.1"
	-@$(CP) "$(ROOT)\library\msgcat\msgcat.tcl"   "$(SCRIPT_INSTALL_DIR)\msgcat1.1"
	-@$(CP) "$(ROOT)\library\msgcat\pkgIndex.tcl" "$(SCRIPT_INSTALL_DIR)\msgcat1.1"
	-@echo installing $(TCLDDEDLL)
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)\dde1.1"
	-@$(CP) "$(TCLDDEDLL)" "$(SCRIPT_INSTALL_DIR)\dde1.1"
	-@$(CP) "$(ROOT)\library\dde1.1\pkgIndex.tcl" "$(SCRIPT_INSTALL_DIR)\dde1.1"
	-@echo installing $(TCLREGDLL)
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)\reg1.0"
	-@$(CP) "$(TCLREGDLL)" "$(SCRIPT_INSTALL_DIR)\reg1.0"
	-@$(CP) "$(ROOT)\library\reg1.0\pkgIndex.tcl" "$(SCRIPT_INSTALL_DIR)\reg1.0"
	-@echo installing encoding files
	-@$(MKDIR) "$(SCRIPT_INSTALL_DIR)\encoding"
	-@$(CP) "$(ROOT)\library\encoding\*.enc" "$(SCRIPT_INSTALL_DIR)\encoding"
	-@echo installing library files
	-@$(CP) "$(GENERIC_DIR)\tcl.h"          "$(INCLUDE_INSTALL_DIR)"
	-@$(CP) "$(GENERIC_DIR)\tclDecls.h"     "$(INCLUDE_INSTALL_DIR)"
	-@$(CP) "$(GENERIC_DIR)\tclPlatDecls.h" "$(INCLUDE_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\history.tcl"  "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\init.tcl"     "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\ldAout.tcl"   "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\parray.tcl"   "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\safe.tcl"     "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\tclIndex"     "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\package.tcl"  "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\word.tcl"     "$(SCRIPT_INSTALL_DIR)"
	-@$(CP) "$(ROOT)\library\auto.tcl"     "$(SCRIPT_INSTALL_DIR)"

install-doc: doc

# remove all generated files
clean:
	-$(RM) $(TCLBASE).def $(TCLBASE) $(TCLLIB) $(TCLDLL) $(TCLBASE).res
	-$(RM) $(TCLREGDLL) $(TCLDDEDLL) $(TCLPLUGINDLL)
	-$(RM) $(TCLSHPMBASE) $(TCLSHPM) $(TCLSHPMBASE).map $(TCLSHPMBASE).res
	-$(RM) $(TCLSHBASE) $(TCLSH) $(TCLSHBASE).map $(TCLSHBASE).res
	-$(RM) $(TCLTESTBASE) $(TCLTEST) $(TCLTESTBASE).map $(TCLTESTBASE).res
	-$(RM) tcl$(SHORTVERSION).inf
	-$(RM) *.$(OBJ) *.imp *.a *.lib

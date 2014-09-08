# This Makefile is used to create several test cases for Tcl's load
# command.  It also illustrates how to take advantage of configuration
# exported by Tcl to set up Makefiles for shared libraries.
# SCCS: @(#) Makefile.in 1.11 96/04/15 09:50:19

DEBUG=1
CC = gcc
#LIBS =			-lc
LIBS =			
SHLIB_LD_LIBS=		../tcl$(TCL_VERSION).lib
ifdef DEBUG
	CFLAGS =	-g -mprobe -m486 -fhandle-exceptions -Zomf -Zmtd \
			$(DEFINES) $(INCLUDES) -DDEBUG
	SHLIB_LD =	$(CC) -mprobe -Zcrtdll -Zdll -Zomf -Zmt -Zstack 32 \
			-Zlinker /ALIGN:2 -Zlinker /FARCALLTRANSLATION \
			-Zlinker /RUNFROMVDM -Zlinker /EXEPACK -lm
else
	CFLAGS =	-O -Wall -mprobe -m486 -s -fhandle-exceptions -Zomf \
			-Zmtd $(DEFINES) $(INCLUDES)
	SHLIB_LD =	$(CC) -mprobe -Zcrtdll -Zdll -Zomf -Zmt -Zstack 32 \
			-Zlinker /ALIGN:2 -Zlinker /FARCALLTRANSLATION \
			-Zlinker /RUNFROMVDM -Zlinker /EXEPACK -lm -lwrap -s
endif
SHLIB_CFLAGS =		
SHLIB_SUFFIX =		.dll
OBJ_SUFFIX =		.obj
SHLIB_VERSION =		80
SRC_DIR =		.
TCL_VERSION=		80
RM=			del /f

CC_SWITCHES = $(CFLAGS) -I${SRC_DIR}/../../generic -DTCL_MEM_DEBUG \
	${SHLIB_CFLAGS}

all: pkga${SHLIB_SUFFIX} pkgb${SHLIB_SUFFIX} pkgc${SHLIB_SUFFIX} \
	pkgd${SHLIB_SUFFIX} pkge${SHLIB_SUFFIX} pkgf${SHLIB_SUFFIX}

pkga${OBJ_SUFFIX}: $(SRC_DIR)/pkga.c
	$(CC) -c $(CC_SWITCHES) $(SRC_DIR)/pkga.c

pkga${SHLIB_SUFFIX}: pkga${OBJ_SUFFIX} $(SHLIB_LD_LIBS)
	${SHLIB_LD} pkga$(OBJ_SUFFIX) -o pkga${SHLIB_SUFFIX} $(SHLIB_LD_LIBS) pkga.def

pkgb${OBJ_SUFFIX}: $(SRC_DIR)/pkgb.c
	$(CC) -c $(CC_SWITCHES) $(SRC_DIR)/pkgb.c

pkgb${SHLIB_SUFFIX}: pkgb${OBJ_SUFFIX} $(SHLIB_LD_LIBS)
	${SHLIB_LD} pkgb$(OBJ_SUFFIX) -o pkgb${SHLIB_SUFFIX} $(SHLIB_LD_LIBS) pkgb.def

pkgc${OBJ_SUFFIX}: $(SRC_DIR)/pkgc.c
	$(CC) -c $(CC_SWITCHES) $(SRC_DIR)/pkgc.c

pkgc${SHLIB_SUFFIX}: pkgc${OBJ_SUFFIX} $(SHLIB_LD_LIBS)
	${SHLIB_LD} pkgc$(OBJ_SUFFIX) -o pkgc${SHLIB_SUFFIX} $(SHLIB_LD_LIBS) pkgc.def

pkgd${OBJ_SUFFIX}: $(SRC_DIR)/pkgd.c
	$(CC) -c $(CC_SWITCHES) $(SRC_DIR)/pkgd.c

pkgd${SHLIB_SUFFIX}: pkgd${OBJ_SUFFIX} $(SHLIB_LD_LIBS)
	${SHLIB_LD} pkgd$(OBJ_SUFFIX) -o pkgd${SHLIB_SUFFIX} $(SHLIB_LD_LIBS) pkgd.def

pkge${OBJ_SUFFIX}: $(SRC_DIR)/pkge.c
	$(CC) -c $(CC_SWITCHES) $(SRC_DIR)/pkge.c

pkge${SHLIB_SUFFIX}: pkge${OBJ_SUFFIX} $(SHLIB_LD_LIBS)
	${SHLIB_LD} pkge$(OBJ_SUFFIX) -o pkge${SHLIB_SUFFIX} $(SHLIB_LD_LIBS) pkge.def

pkgf${OBJ_SUFFIX}: $(SRC_DIR)/pkgf.c
	$(CC) -c $(CC_SWITCHES) $(SRC_DIR)/pkgf.c

pkgf${SHLIB_SUFFIX}: pkgf${OBJ_SUFFIX} $(SHLIB_LD_LIBS)
	${SHLIB_LD} pkgf$(OBJ_SUFFIX) -o pkgf${SHLIB_SUFFIX} $(SHLIB_LD_LIBS) pkgf.def

clean:
	-$(RM) *$(OBJ_SUFFIX) *${SHLIB_SUFFIX} config.cache config.log config.status lib.exp

distclean: clean
	-$(RM) Makefile

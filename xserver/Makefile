# Xatw: MIT X11R4 server with the ATW800/2 ddx, cross-built for Atari
# System V with gcc-cross-amix. Flags are Atari's own from atariM.cf;
# the R4 tree is compiled -traditional as they did, the ddx is not.
#
#   make MIT=../mit        (default)
#
MIT	= ../mit
CROSS	= $(HOME)/opt/asv-cross/bin/m68k-cbm-sysv4-
CC	= $(CROSS)gcc
AR	= $(CROSS)ar
RANLIB	= /bin/true

DEFS	= -DSYSV -Datari -DIEEE -DNOINLINEICEIL \
	  -DCOMPILEDDEFAULTFONTPATH='"/usr/lib/X11/fonts/misc/,/usr/lib/X11/fonts/75dpi/,/usr/lib/X11/fonts/100dpi/"' \
	  -DRGB_DB='"/usr/lib/X11/rgb"' -DSHAPE -DMALLOC_0_RETURNS_NULL \
	  -DMULTIBUFFER -DMITMISC -DTCPCONN -DUNIXCONN \
	  -DFONT_SNF -DFONT_BDF -DCOMPRESSED_FONTS -DNDBM \
	  -DUNCOMPRESSFILT=\"/usr/bin/uncompress\" \
	  -DBDFTOSNFFILT=\"/usr/bin/X11/bdftosnf\" -DSHELLPATH=\"/bin/sh\" \
	  -DZBDFTOSNFFILT=\"/usr/bin/uncompress\ \|\ /usr/bin/X11/bdftosnf\ -t\"
INCS	= -I$(MIT)/server/include -I$(MIT) -I$(MIT)/include -I$(MIT)/server/ddx/mi \
	  -I$(MIT)/server/ddx/mfb -I$(MIT)/server/ddx/cfb \
	  -I$(MIT)/lib/Xau -I$(MIT)/extensions/include -I$(SYSROOT)/usr/ucbinclude
OLD	= -O -traditional $(DEFS) $(INCS)
NEW	= -O $(DEFS) $(INCS)
SYSROOT	= $(HOME)/opt/asv-cross/m68k-cbm-sysv4/sysroot
# libm's sqrt needs libc's fpset helpers and oscolor needs ndbm; the cross
# wrapper links archives before -l libraries, so they go in as objects
SYSOBJ	= $(addprefix $(OBJ)/sys/, fpsetrnd.o fpsetmask.o fpsetsticky.o $(notdir $(wildcard $(OBJ)/sys/*.o)))
LIBS	= -lsocket -lsockhost -lnsl -lgen -lm

OBJ	= obj

DIXSRC	= $(wildcard $(MIT)/server/dix/*.c)
OSSRC	= $(addprefix $(MIT)/server/os/4.2bsd/, WaitFor.c access.c connection.c \
	  fonttype.c io.c oscolor.c osfonts.c osinit.c utils.c auth.c mitauth.c xdmcp.c)
MISRC	= $(wildcard $(MIT)/server/ddx/mi/*.c)
MFBSRC	= $(filter-out %/mfbpntarea.c %/mfbplygblt.c %/mfbimggblt.c %/mfbtegblt.c, \
	  $(wildcard $(MIT)/server/ddx/mfb/*.c))
# mfb compiles four sources several times with different operators
MFBVAR	= $(addprefix $(OBJ)/mfbvar/, mfbseg.o mfbpgbwht.o mfbpgbblak.o mfbpgbinv.o \
	  mfbigbwht.o mfbigbblak.o mfbpawhite.o mfbpablack.o mfbpainv.o \
	  mfbtewhite.o mfbteblack.o)
CFBSRC	= $(wildcard $(MIT)/server/ddx/cfb/*.c)
SNFSRC	= $(MIT)/server/ddx/snf/snffonts.c
EXTSRC	= $(addprefix $(MIT)/extensions/server/, shape.c multibuf.c mitmisc.c)
XAUSRC	= $(addprefix $(MIT)/lib/Xau/, AuDispose.c AuFileName.c AuGetAddr.c AuLock.c AuRead.c AuUnlock.c AuWrite.c)
FONTDIR	= $(MIT)/fonts/mkfontdir/fontdir.c
DDXSRC	= atwInit.c atwIo.c atwKMap.c atwCompat.c

OLDSRC	= $(DIXSRC) $(OSSRC) $(MISRC) $(MFBSRC) $(CFBSRC) $(SNFSRC) $(EXTSRC) $(XAUSRC) $(FONTDIR)
OLDOBJ	= $(patsubst $(MIT)/%.c,$(OBJ)/%.o,$(OLDSRC)) $(MFBVAR) $(OBJ)/mfbvar/cfbseg.o
DDXOBJ	= $(patsubst %.c,$(OBJ)/ddx/%.o,$(DDXSRC))

all: Xatw

Xatw: $(DDXOBJ) $(OLDOBJ)
	$(CC) -o $@ $(DDXOBJ) $(OLDOBJ) $(sort $(SYSOBJ)) $(LIBS)
	python3 fixneeded.py $@

$(OBJ)/ddx/%.o: %.c atw.h
	@mkdir -p $(dir $@)
	$(CC) $(NEW) -I$(MIT)/server/ddx/mi -c -o $@ $<

$(OBJ)/%.o: $(MIT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(OLD) -c -o $@ $<

MFBD	= $(MIT)/server/ddx/mfb
$(OBJ)/mfbvar/%.o:
	@mkdir -p $(dir $@)
	$(CC) $(OLD) $(VARFLAGS) -c -o $@ $(VARSRC)
$(OBJ)/mfbvar/cfbseg.o:    VARSRC = $(MIT)/server/ddx/cfb/cfbline.c
$(OBJ)/mfbvar/cfbseg.o:    VARFLAGS = -DPOLYSEGMENT
$(OBJ)/mfbvar/mfbseg.o:    VARSRC = $(MFBD)/mfbline.c
$(OBJ)/mfbvar/mfbseg.o:    VARFLAGS = -DPOLYSEGMENT
$(OBJ)/mfbvar/mfbpgbwht.o: VARSRC = $(MFBD)/mfbplygblt.c
$(OBJ)/mfbvar/mfbpgbwht.o: VARFLAGS = -DMFBPOLYGLYPHBLT=mfbPolyGlyphBltWhite '-DOPEQ=|='
$(OBJ)/mfbvar/mfbpgbblak.o: VARSRC = $(MFBD)/mfbplygblt.c
$(OBJ)/mfbvar/mfbpgbblak.o: VARFLAGS = -DMFBPOLYGLYPHBLT=mfbPolyGlyphBltBlack '-DOPEQ=&=~'
$(OBJ)/mfbvar/mfbpgbinv.o: VARSRC = $(MFBD)/mfbplygblt.c
$(OBJ)/mfbvar/mfbpgbinv.o: VARFLAGS = -DMFBPOLYGLYPHBLT=mfbPolyGlyphBltInvert '-DOPEQ=^='
$(OBJ)/mfbvar/mfbigbwht.o: VARSRC = $(MFBD)/mfbimggblt.c
$(OBJ)/mfbvar/mfbigbwht.o: VARFLAGS = -DMFBIMAGEGLYPHBLT=mfbImageGlyphBltWhite '-DOPEQ=|='
$(OBJ)/mfbvar/mfbigbblak.o: VARSRC = $(MFBD)/mfbimggblt.c
$(OBJ)/mfbvar/mfbigbblak.o: VARFLAGS = -DMFBIMAGEGLYPHBLT=mfbImageGlyphBltBlack '-DOPEQ=&=~'
$(OBJ)/mfbvar/mfbpawhite.o: VARSRC = $(MFBD)/mfbpntarea.c
$(OBJ)/mfbvar/mfbpawhite.o: VARFLAGS = -DMFBSOLIDFILLAREA=mfbSolidWhiteArea -DMFBSTIPPLEFILLAREA=mfbStippleWhiteArea '-DOPEQ=|=' '-DEQWHOLEWORD==~0'
$(OBJ)/mfbvar/mfbpablack.o: VARSRC = $(MFBD)/mfbpntarea.c
$(OBJ)/mfbvar/mfbpablack.o: VARFLAGS = -DMFBSOLIDFILLAREA=mfbSolidBlackArea -DMFBSTIPPLEFILLAREA=mfbStippleBlackArea '-DOPEQ=&=~' '-DEQWHOLEWORD==0'
$(OBJ)/mfbvar/mfbpainv.o: VARSRC = $(MFBD)/mfbpntarea.c
$(OBJ)/mfbvar/mfbpainv.o: VARFLAGS = -DMFBSOLIDFILLAREA=mfbSolidInvertArea -DMFBSTIPPLEFILLAREA=mfbStippleInvertArea '-DOPEQ=^=' '-DEQWHOLEWORD=^=~0'
$(OBJ)/mfbvar/mfbtewhite.o: VARSRC = $(MFBD)/mfbtegblt.c
$(OBJ)/mfbvar/mfbtewhite.o: VARFLAGS = -DMFBTEGLYPHBLT=mfbTEGlyphBltWhite '-DOP=' -DCLIPTETEXT=mfbImageGlyphBltWhite
$(OBJ)/mfbvar/mfbteblack.o: VARSRC = $(MFBD)/mfbtegblt.c
$(OBJ)/mfbvar/mfbteblack.o: VARFLAGS = -DMFBTEGLYPHBLT=mfbTEGlyphBltBlack '-DOP=~' -DCLIPTETEXT=mfbImageGlyphBltBlack

clean:
	rm -rf $(OBJ) Xatw

.PHONY: all clean

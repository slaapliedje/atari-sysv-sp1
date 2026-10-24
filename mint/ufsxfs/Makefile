# ufs.xfs for FreeMiNT, cross-built: needs the FreeMiNT source tree
# (FREEMINT, with sys/libkern/libkern030.a made: `make -C sys/libkern 030`
# after `make -C sys/buildinfo`) and m68k-atari-mint-gcc.
FREEMINT ?= $(HOME)/dev/usb-ref/freemint
CROSS ?= $(HOME)/opt/cross-mint/bin/m68k-atari-mint-
CC = $(CROSS)gcc
CPU ?= 030
CFLAGS = -m68$(CPU) -mshort -O2 -fomit-frame-pointer -fno-strict-aliasing -fgnu89-inline \
	-Wall -Wmissing-prototypes -Wshadow -Wpointer-arith \
	-I$(FREEMINT)/sys -D__KERNEL_XFS__
LDFLAGS = -nostdlib -Wl,--entry,init_xfs -Wl,--fatal-warnings -s

all: UFS.XFS

ufs.o: ufs.c
	$(CC) $(CFLAGS) -c ufs.c -o ufs.o

UFS.XFS: ufs.o
	$(CC) -m68$(CPU) -mshort $(LDFLAGS) -o UFS.XFS ufs.o -L$(FREEMINT)/sys/libkern -lkern$(CPU) -lgcc
	@ls -l UFS.XFS

clean:
	rm -f ufs.o UFS.XFS

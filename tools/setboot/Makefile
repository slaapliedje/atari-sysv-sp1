# cross-build with gcc-cross-amix (m68k-cbm-sysv4); fixneeded.py rewrites the
# absolute DT_NEEDED path the wrapper leaves behind
CROSS = $(HOME)/opt/asv-cross/bin/m68k-cbm-sysv4-
setboot: setboot.c
	$(CROSS)gcc -O -o $@ $<
	python3 ../../xserver/fixneeded.py $@

#!/bin/sh
# modadmin.sh outdir -- libmod.a and modadmin for the target, into outdir.
set -e
DLM=$(cd "$(dirname "$0")/.." && pwd)
O=$1
mkdir -p "$O"
for f in modload moduload modpath modstat modadm getksym; do
	m68k-cbm-sysv4-as -o "$O/$f.o" "$DLM/libmod/$f.s"
done
rm -f "$O/libmod.a"
m68k-elf-ar rcs "$O/libmod.a" "$O"/mod*.o "$O/getksym.o"
nice -n 19 m68k-cbm-sysv4-gcc -O -I"$DLM/include" -o "$O/modadmin" "$DLM/modadmin.c" "$O/libmod.a"
nice -n 19 m68k-cbm-sysv4-gcc -O -I"$DLM/include" -o "$O/kvar" "$DLM/kvar.c" "$O/libmod.a"

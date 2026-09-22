#!/bin/sh
# Boot an Atari System V disk image in Hatari.
#
#   hatari-asv.sh IMAGE [TTRAM_MB] [extra hatari options...]
#
# Needs a Hatari with the four TT fixes listed in the README (the stock
# 2.x release stalls after the kernel banner): 68030 MOVES bus-fault
# data buffer, 68030 data-cache burst function code, NCR5380 TT
# interrupt line, MC146818 periodic interrupt. Set HATARI to its path.
#
# Hatari WRITES to the SCSI image (fsck alone modifies it): boot a copy.
# TT-RAM: a stock kernel takes up to 16 MB as is; larger needs the
# kernel/patch-asv-image.py cap. Do not press keys in the first ~10 s
# (they answer the loader's Boot: prompt and abort the auto-boot).
IMG=$1; TT=${2:-16}; shift 2 2>/dev/null || shift $#
exec "${HATARI:-hatari}" --machine tt --tos "${TOS:-/usr/share/hatari/tos306us.img}" \
	--addr24 off --mmu on --fpu 68882 --memsize 4 --ttram "$TT" \
	--scsi "0=$IMG" --monitor vga --fast-forward on --sound off \
	--confirm-quit no "$@"

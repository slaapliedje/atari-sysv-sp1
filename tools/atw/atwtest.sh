#!/bin/sh
# atwtest.sh - run from an xterm on the TT: shows the ATW800/2's 16 and
# 32 bpp byte-order test screens, then gives X its screen back.
/usr/local/bin/atwtest
/usr/x11r6/bin/xrefresh

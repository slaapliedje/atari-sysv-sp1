#!/bin/sh
# itool - run an INMOS toolset program (d72uni) on a transputer through
# iserver. Installed as /usr/local/lib/transputer/bin/itool, with the
# tool names (icc, ilink, icollect, oc, ...) linked to it:
#   icc hello.c -t800        # = iserver -sr -ss -sc $IBIN/icc.btl hello.c -t800
# The environment is the ATW800/2 Atari profile's, for Unix:
T=/usr/local/lib/transputer
: "${TRANSPUTER:=/dev/link0}"		# the C011: the TRAM in slot 1 (T800, 2 MB)
: "${IBIN:=$T/itools/}"
: "${ISEARCH:=$T/libs/}"
: "${ITERM:=$T/iterms/ansi.itm}"
: "${FTERM:=$ITERM}"
: "${IBOARDSIZE:=#200000}"
: "${IDEBUGSIZE:=#200000}"
# unset, iserver answers the tools with a German "finde nix" line
: "${ICCARG:=}" "${ILINKARG:=}" "${ILISTARG:=}"
export TRANSPUTER IBIN ISEARCH ITERM FTERM IBOARDSIZE IDEBUGSIZE ICCARG ILINKARG ILISTARG
tool=`basename "$0"`
[ "$tool" = itool ] && { tool="$1"; shift; }
[ -f "$IBIN$tool.btl" ] || { echo "itool: no $IBIN$tool.btl" >&2; exit 1; }
# no -se: the error flag reads set on this TRAM whatever runs (the Atari
# build's TestError always answers 0, so its scripts never really test it)
exec "$T/bin/iserver" -sr -ss -sc "$IBIN$tool.btl" "$@"

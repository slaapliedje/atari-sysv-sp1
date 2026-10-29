#!/bin/sh
set -e
T=$(cd "$(dirname "$0")" && pwd)
[ -n "$1" ] || { echo "usage: cslots-check.sh output-directory"; exit 1; }
mkdir -p "$1"
O=$(cd "$1" && pwd)
nice -n19 ${HOSTCC:-cc} -std=gnu89 -O -Wall -Wno-unused-function \
 -Wno-unused-but-set-variable -Wno-missing-field-initializers -DDLM_HOST \
 -I"$T/.." -I"$T/../include" "$T/cslots.c" -o "$O/cslots"
"$O/cslots"

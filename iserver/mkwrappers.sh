#!/bin/sh
# mkwrappers.sh - on the TT, in /usr/local/lib/transputer/bin: one small
# script per tool in ../itools (icc, ilink, icollect, oc, ...) that runs
# it through itool. Not symlinks: ASV hands a #! script run through a
# symlink the link's TARGET as $0, so itool could not tell which tool was
# meant.
cd /usr/local/lib/transputer/bin || exit 1
chmod 755 iserver itool
for t in `ls ../itools | sed "s/.btl//"`; do
	rm -f $t
	printf '#!/bin/sh\nexec /usr/local/lib/transputer/bin/itool %s "$@"\n' $t > $t
	chmod 755 $t
done

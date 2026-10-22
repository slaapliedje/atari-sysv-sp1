# The release build's hook: this replaces /etc/rc2.d/S75rpc on the
# throw-away copy of the stock image only (release/build.py, stage 1).
# SCSI 2 holds the payload tar, SCSI 3 takes the log.  stage1.sh runs
# at two boots (its phase 1 reboots); the log of the first is kept in
# /rel/log and repeated, so the PC sees all of it on SCSI 3.
case "$1" in
start)
	[ -f /rel/stage1.sh ] || { cd / && tar xf /dev/rdsk/c2d0sf rel/stage1.sh; }
	[ -f /rel/stage1.sh ] || exit 0
	[ -f /rel/log ] || cp /dev/null /rel/log
	sh /rel/stage1.sh >> /rel/log 2>&1
	cat /rel/log > /dev/dsk/c3d0sf
	;;
esac
exit 0

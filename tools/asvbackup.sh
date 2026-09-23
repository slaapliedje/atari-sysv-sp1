#!/bin/sh
# asvbackup.sh DEST - pull a snapshot of the whole TT from its rsync daemon
# (../rsync) into DEST/<date>, hard-linking unchanged files to the previous
# snapshot, so every snapshot is complete but only changes take space.
# DEST/latest points at the newest complete one.
#
#   ASV_HOST=... tools/asvbackup.sh /backups/tt
#
# The password is read from .rsyncpass beside this script (mode 600, never
# committed), or RSYNC_PASSWORD. Owners, groups, modes and device nodes are
# kept with --fake-super in the user.rsync.%stat extended attribute, so the
# PC side needs no root, but DEST must be on a filesystem with user xattrs
# (ext4, xfs, btrfs). Numeric ids: the TT's users are not the PC's.
#
# Restore: the daemon's module is read-only. To put files back, add a
# second module with `read only = no' and the same `hosts allow' / `auth
# users' to /etc/rsyncd.conf for the duration, and push with the same
# --fake-super --numeric-ids -aH options from the snapshot; the daemon runs
# as root, so it recreates owners, modes and device nodes from the xattrs.
set -e
: "${ASV_HOST:?set ASV_HOST}"
dest=${1:?usage: asvbackup.sh DEST}
here=$(cd "$(dirname "$0")" && pwd)
if [ -z "$RSYNC_PASSWORD" ]; then
	[ -f "$here/.rsyncpass" ] || { echo "no $here/.rsyncpass and no RSYNC_PASSWORD" >&2; exit 1; }
	RSYNC_PASSWORD=$(cat "$here/.rsyncpass"); export RSYNC_PASSWORD
fi
mkdir -p "$dest"
snap=$(date +%Y-%m-%d_%H%M)
link=
[ -d "$dest/latest/" ] && link="--link-dest=$(cd "$dest/latest/" && pwd)"
# an interrupted run resumes into the same .partial directory
part=$(ls -d "$dest"/*.partial 2>/dev/null | head -1)
[ -n "$part" ] || part="$dest/$snap.partial"
rsync -aH --numeric-ids --fake-super --partial --delete --stats \
	--timeout=900 $link "rsync://backup@$ASV_HOST/root/" "$part/"
mv "$part" "$dest/$snap"
ln -sfn "$snap" "$dest/latest"
echo "snapshot: $dest/$snap"

# rsync for Atari System V, and whole-system backups to the PC

This builds rsync 3.4.1 for the TT as a static program. `install.sh` sets it
up as an rsync daemon on the TT, and `../tools/asvbackup.sh` pulls dated
snapshots of the whole system from it onto the PC.

## Build (on the PC)

```sh
sh build.sh        # -> work/rsync (static, ~830 KB stripped)
```

This needs gcc-cross-amix at `~/opt/asv-cross` with the ASV sysroot, and
curl. The tarball's SHA-256 is pinned (it was also checked against Andrew
Tridgell's signature).

## Install (on the TT, as root)

Copy `install.sh`, `rsyncd.conf`, `work/rsync` and a file named `password`
into one directory. `password` holds the backup password: put the same one
in `../tools/.rsyncpass` on the PC, mode 600, which git ignores. Then:

```
sh install.sh PC_ADDRESS
```

This installs:

- `/usr/local/bin/rsync`, started by `inetd` on port 873, so nothing runs
  between backups.
- `/etc/rsyncd.conf` with one read-only module, `root`: the whole system
  except `/proc` and the contents of `/var/tmp`.
- `/etc/rsyncd.secrets` with the user `backup` and the password.

Only PC_ADDRESS may connect, and it must also give the password.
`/etc/inetd.conf` and `/etc/services` are saved as `*.pre-rsync` before
they are changed. Re-running without `password` updates the binary or the
address and keeps the password.

## Back up (on the PC)

```sh
ASV_HOST=... tools/asvbackup.sh /path/to/backups
```

Each run makes `DEST/<date>`, a complete tree, with unchanged files
hard-linked to the previous snapshot. `DEST/latest` points at the newest
complete snapshot, and an interrupted run resumes. Owners, modes and device
nodes are kept with `--fake-super` in extended attributes, so the PC needs
no root, but DEST must be on ext4, xfs or btrfs. The `asvbackup.sh`
comments say how to restore.

Speed is set by the network, not the 030. The DaynaPORT driver polls
(`../driver-dp`), so the link runs at about 35 KB/s: ftp is exactly as fast
as rsync. The first snapshot of a 350 MB system takes about three hours;
after that only changed files move, and `/etc` alone checks in about 6 s.

## What porting it took

- **A static link.** `asv-static-cc` compiles through gcc-cross-amix and
  links with `ld` against ASV's `libc.a` and `libsocket.a`. `asvstatic.c`
  supplies what those lack:
  - `__asv_iob` for the shadow sysroot's `<stdio.h>`;
  - the netconfig lookups, whose reader lives only in the shared
    `libnsl.so`. `socket()` needs them to find `/dev/tcp`, so these return
    the inet entries of `/etc/netconfig`;
  - `gethostname`, `getdomainname`, `inet_ntoa` and `strtoll`.

  Name lookups aren't available to static programs, so the daemon uses
  numeric addresses and `reverse lookup = no`.
- **`include/stdint.h`.** ASV predates C99.
- **`rsync-3.4.1-asv.patch`.** gcc 2.7.2 is a C89 compiler, so five
  declarations after statements are hoisted. popt had its `FORMAT`
  attribute before a declaration, and needed `snprintf` mapped to rsync's
  own and `ENOTSUP`. `lib/getaddrinfo.c` (only built on old systems) now
  uses libc's `strdup`.
- **`-O0`.** gcc 2.7.2's optimiser miscompiles `long long` comparisons
  joined by `&&`/`||`: both halves test false, and the combination is false
  too. Built with `-O`, popt rejected every numeric option as "number too
  large or too small". rsync's sizes and offsets are 64-bit, so it is built
  without optimisation.
- **`rsyncd.conf`.** SVR4 has no `/var/run` (the lock file lives in
  `/var/adm`), and rsyncd.conf doesn't allow comments at the end of a line.

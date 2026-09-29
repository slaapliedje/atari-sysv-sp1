# AMIX in an emulator

`uaeamix.py` runs Commodore's AMIX 2.1 in an emulated Amiga 3000, with no
window on your desktop, and drives it from scripts. It is the AMIX
counterpart of `tools/hatari-asv.sh`:

- it boots the machine on a private X display (Xvfb);
- it types, swaps floppies and takes screenshots through the emulator's
  own control interface;
- once AMIX is installed, it runs commands over telnet and copies files
  over ftp.

```sh
export WINUAE_EXE=~/opt/winuae-win/5310/winuae64.exe WINEPREFIX=~/opt/winuae-win/prefix

./uaeamix.py start --from ../work/emu/amix-configured.hdf --wait   # ~25 s to a login
./uaeamix.py sh 'uname -a'           # root, over telnet; exits with the command's status
./uaeamix.py put manx /home/dev/manx/manx
./uaeamix.py get /etc/passwd /tmp/passwd
./uaeamix.py shot /tmp/screen.png    # the console
./uaeamix.py halt; ./uaeamix.py stop
```

`--from` starts from a sparse copy of a clean system, so every run begins
the same way. Leave it out to keep using `amix.hdf` as it is.

Manx's `tools/amix/` (`amixsh`, `amixput`, `amixrun`, `amixdrive`) wraps
these, the same way `tools/tt/` wraps the TT's helpers.

## The machine

AMIX asks for a particular A3000, and the installer checks the SCSI IDs:

| | |
|---|---|
| CPU | 68030 with its MMU, 68882. No JIT, and not "more compatible" (both panic AMIX) |
| chipset | ECS, A3000 (Fat Gary, RAMSEY, the SDMAC, the RF5C01A clock) |
| RAM | 2 MB chip, 16 MB motherboard fast. AMIX uses 4 to 16 MB and ignores anything else |
| disk | `amix.hdf`, SCSI ID 6 on the A3000's WD33C93. The installer partitions it (RDB) |
| tape | the 2.1 installation tape, SCSI ID 4 (`start install` only) |
| network | A2065 on the emulator's built-in NAT (SLIRP) |

The network is SLIRP's usual one:

- the guest is 10.0.2.15;
- the host is 10.0.2.2 (that address reaches the host's 127.0.0.1);
- DNS is 10.0.2.3.

The internet works from inside: DNS, HTTP and HTTPS. Two ports are
forwarded in, 2323 to the guest's telnet and 2121 to its ftp. **SLIRP
listens for them on all of the host's interfaces**, so give root a
password (`.amixpass`, below).

`put` and `get` use active-mode ftp: they tell the guest to connect back to
10.0.2.2. No root and no tap device are needed on the host.

SLIRP only learns the guest's Ethernet address from a packet the guest
sends. Until then it can't forward anything in. `setup` installs
`/etc/rc2.d/S99uaeamix`, which pings the host at boot and sets the
default route.

## Which emulator

`--emu` (or `$UAEAMIX_EMU`) picks one of three.

- **`wine`** (the default): WinUAE 5.3.1 for Windows, under Wine. Set
  `WINUAE_EXE` to its `winuae64.exe`, from
  <https://download.abime.net/winuae/releases/WinUAE5310_x64.zip>.
  `WINEPREFIX` defaults to `../work/emu/wine`.
  **This is the one that works.** It installs, boots, runs telnet and ftp,
  and Manx loads Hacker News over HTTPS.
- **`winuae`**: WinUAE's Unix port (`$WINUAE`).
- **`amiberry`**: Amiberry (`$AMIBERRY`, or the flatpak). Amiberry 8.3
  also has a control socket, which the harness uses.

### Why not current WinUAE or Amiberry

**WinUAE 6.0.0 and later, and Amiberry 8.3, kill AMIX's first process:**

```
NOTICE: User BUS ERROR at DC801770, PC:C100F3A6 FAULT:6 PID:1 CMD:/sbin/init
```

The kernel's `suword()` stores init's argc into the fresh user stack with
`moves.l`. That store takes a page fault, and on the retry it writes the
previous store's data (the address of `sf_fault`) instead.

- Table 35 (68030 MMU with caches, used for this configuration since 6.0)
  sent MOVES through the plain `dfc030c_put_*` helpers, not the `_state`
  ones.
- WinUAE PR #499 fixed `gencpu.cpp`, but the `cpuemu_35.cpp` in the
  repository was never regenerated. Regenerating it changes only those
  MOVES lines, and AMIX then boots.

Even with that fixed, current WinUAE isn't usable for AMIX yet:

- **Received network data gets corrupted.** Bytes are zeroed inside TCP
  segments, after the checksum has already passed. The frames SLIRP hands
  to the A2065 are correct, and WinUAE 5.3.1 receives the same downloads
  intact.
- **Boots sometimes hang, and processes sometimes die**: a stray
  `User BUS ERROR` in `id`, for example.
- The A2065 turns its receiver off for good after a receive BUFF error.
  The Am7990 only does that for transmit errors (data sheet, CSR0 RXON/TXON).
  AMIX's driver never restarts it, so the network dies.

The Unix port adds two build problems:

- Since commit f4aaab2, every reset frees the UAE Boot ROM, so the first
  reset writes through a NULL pointer.
- `od-unix/target.h`'s beta number lags `win32.h`, so CMake refuses to
  configure.

## Media

`../work/emu/media/` (not in the repository) holds:

- `amix_21_boot.adf`, `amix_21_root.adf`, `amix_21_tape.zip`: the 2.1 boot
  and root floppies and the tape, from
  <https://amigaunix.com/doku.php/downloads>;
- `setclk.bz2`, the Y2K-safe `setclk`, from the same page;
- an A3000 Kickstart named `*a3000*.rom`, with its `rom.key` if it is an
  Amiga Forever one. 2.04 (37.175) or 3.1 both work. `AMIX_KICKSTART`
  overrides it.

Keep the floppies read-only (`chmod 444`). A writable root floppy gets
written to whenever the kernel mounts it.

## Installing

`start install`, then answer the installer on the screen. Check each
answer with `shot`: the installer throws away typed-ahead lines, so answer
one prompt at a time.

1. At "Insert floppy disk 2", run `floppy 0 …/amix_21_root.adf`, then press
   Return.
2. Keyboard: 12 (US).
3. `install`.
4. The tape is already in.
5. 0 MB for AmigaDOS.
6. Root 957 MB, swap 64 MB. The rest of a 1 GB disk goes to the 2 MB boot
   partition.
7. `ufs`.
8. "Everything on the tape" (2).

`warp on` speeds up the tape, which takes about 40 minutes.

After "The system is halted", run `stop` and then `start`, and answer the
first-boot questions:

- nodename `amix`, domain `.local`;
- a hosts file: this machine 10.0.2.15, a second one named `host` at
  10.0.2.2;
- time zone 0 (GMT);
- a date before 2000 (`date(1)` can't take a later one; `setup` fixes the
  clock);
- passwords for the system accounts, the guest account and a `dev` account;
- no A2024, no colour X, no netnews.

Before setting the passwords, put the password in `../work/emu/.amixpass`
(mode 600). Then, at the login prompt:

```sh
./uaeamix.py setup          # console ping, S99uaeamix, resolv.conf, Y2K setclk
./uaeamix.py halt; ./uaeamix.py stop
cp --sparse=always ../work/emu/amix.hdf ../work/emu/amix-configured.hdf
```

The emulator runs with `TZ=UTC`, so its clock chip reads UTC, which matches
AMIX's GMT.

## How it works

WinUAE (either build) loads `uaectl.lua`. Every other frame, the script
takes one line from a command file and passes it to WinUAE's uaectrl
parser. That parser accepts:

- `floppy0 <path>`;
- `KEY_RAW_DOWN 0x44` (Amiga raw key codes);
- `AKS_WARP 1`, `AKS_QUIT 1`;
- `dbg <debugger command>`.

Amiberry has its own socket for the same things (`INSERTFLOPPY`,
`SEND_KEY`, `SET_WARP`, `QUIT`…). Screenshots are `xwd` of the emulator's
window. WinUAE under Wine opens a window too small for AMIX's console, so
`shot` enlarges it first.

AMIX's terminal line holds 256 characters, so `sh` refuses longer
commands. For a script, `put` it and run that.

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
./uaeamix.py start --from ../work/emu/amix-configured.hdf --wait   # ~20 s to a login
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

`--emu` (or `$UAEAMIX_EMU`) picks one:

- **`amiberry`** (the default): Amiberry (`$AMIBERRY`, or the flatpak).
  Its control socket drives it.
- **`wine`**: WinUAE for Windows under Wine. `WINUAE_EXE` names a
  `winuae64.exe`; `WINEPREFIX` defaults to `../work/emu/wine`.
- **`winuae`**: WinUAE's Unix port (`$WINUAE`).

All three run AMIX, from the install floppies onwards, because the harness
turns cycle-exact **off**. Tested:

- Amiberry 8.3 as it ships;
- WinUAE 5.3.1;
- WinUAE master built for Linux, with the two Unix-port fixes listed
  below (without them it doesn't start).

### Cycle-exact and the 68030 MMU

WinUAE 6.0 made cycle-exact the default (5.3.1 had it off), and Amiberry
followed. A 68030 with its MMU then runs table 35 (or table 34 with "more
compatible"), which models the instruction pipeline, instead of table 32.
Two bugs in those tables break AMIX, and neither exists in table 32.

1. **A MOVES store that page-faults is replayed with stale data.** The
   kernel's `suword()` stores init's argc into the new stack. On the
   retry it writes the previous store's value (the address of `sf_fault`)
   instead, and init dies:

   ```
   NOTICE: User BUS ERROR at DC801770, PC:C100F3A6 FAULT:6 PID:1 CMD:/sbin/init
   ```

   WinUAE PR #499 fixed this in `gencpu.cpp`, but the `cpuemu_35.cpp` in
   the repository was never regenerated. Regenerating it changes only
   those MOVES lines.

2. **A misaligned MOVES that crosses a page boundary uses one
   translation for all of it.** Without the data cache, MOVES goes through
   `mmu030_get/put_fc_long` and `_word`, which don't split a misaligned
   access. Only the first byte's page is translated. The rest is written
   to (or read from) the physically adjacent page, and the second page
   never faults in.

   `copyout()` and `copyin()` do exactly such MOVES whenever a user buffer
   is at an odd offset. The result is:

   - zeroed bytes in network data (every 6th TCP segment, where a
     download's buffer crosses a page);
   - TLS certificate chains that fail to parse;
   - kernel panics in `kmem_alloc` from garbage in its free list.

   Splitting the access as `dfc030_put_long()` does fixes all of it: a
   stress test (`read()` into fresh pages at odd offsets) goes from a
   kernel panic to 150 clean rounds, and 1 MB downloads from half
   corrupted to all intact.

   Hatari has the same code, but it runs these tables with its data cache
   on, and that path splits correctly.

Also found on the way, none of which the cycle-exact switch avoids:

- The A2065 turns its receiver off for good after a receive BUFF error.
  The Am7990 only does that for transmit errors (data sheet, CSR0
  RXON/TXON), and AMIX's driver never restarts it.
- WinUAE's Unix port: since commit f4aaab2, every reset frees the UAE Boot
  ROM, so the first reset writes through a NULL pointer.
- WinUAE's Unix port: `od-unix/target.h`'s beta number lags `win32.h`, so
  CMake refuses to configure.

### Checking an emulator

`faultcopy.c` reproduces the MOVES bug without a network. It `read()`s a
file into fresh memory at odd offsets, so the kernel's `copyout()`
page-faults in the middle of misaligned MOVES, and then checks every byte.
Build it for AMIX with the gcc-cross-amix toolchain (or Manx's
`toolchain/sysv4-cc -m68030`), `put` it, and run `faultcopy /tmp/f 150`.
A good emulator prints `0 bad bytes`; an affected one corrupts the data or
panics the kernel.

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

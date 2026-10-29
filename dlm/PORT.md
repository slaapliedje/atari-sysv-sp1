# Loadable kernel modules on Atari System V

Goal: drivers become modules that are loaded when the hardware is there,
instead of being linked into the kernel. The kernel stays small, a new or
changed driver needs no relink and no reboot, and the first boot no
longer has to rebuild the kernel under the loader's mini kernel (the step
that looped on a 256 MB TT on 2026-10-28).

The loader is Ash Nazag's DLM (`upstream/`, MIT, kdedon/ashnazag
`kernel/dlm` at the commit in `upstream/UPSTREAM`), an implementation of
the SVR4.2 dynamically loadable modules (`upstream/dlm-spec.md`) for the
AMIX 2.1 kernel. AMIX and ASV are both SVR4.0 for the 68030, built by the
same `cunix` from `/boot` objects and `/etc/master.d`, so most of it
carries over. `upstream/` is kept byte-for-byte; ASV changes live in
`asv/` (glue, configuration) and in patches applied at build time, so
upstream fixes can be taken over.

## What upstream gives

- System calls `modload`, `moduload`, `modstat`, `modpath`, `modadm`,
  `getksym` (sysent 64-69; ASV has `nosys` there - upstream checked
  ASV's table) and `modadmin(1M)`.
- An ELF relocatable loader (`dlm_ld.c`: R_68K_32/16/8, PC32/16/8),
  a kernel symbol table in the image (`dlm_ksym`, filled after the link
  by `tools/mkksym`), dependencies, reference counts, unload.
- Linkages: misc modules; character drivers and STREAMS drivers on a
  registered major (the row holds the loader's open and close
  trampolines; the first open loads the module); STREAMS modules
  (`fmodsw`); exec formats; filesystem types.
- Host-side tests: a relocation oracle against `ld`, error paths, a
  state harness (`test/run.sh`).

## What ASV needs (found 2026-10-29)

1. **Compiler flags.** ASV's headers define `sigset_t`, `stack_t` and
   `MAXSIG` only when `__STDC__` is 0, as Atari's compiler had it. The
   kernel flags are upstream's AMIX ones: `-O -traditional -fno-builtin
   -Dinline=__inline__ -Dvolatile=__volatile__ -D_KERNEL`. With them
   every upstream source compiles against ASV's headers unchanged.
2. **Symbols.** The linked `dlm.o` needs 48 symbols; ASV's kernel has all
   but six:
   - `stext`: ASV's kernel text starts at 0x400 (`bootargs`); `asv/`
     defines `stext` as that absolute address.
   - `ds_modinit`: weak, not needed.
   - `__amix_execsw`, `__amix_fmodsw`, `__amix_vfssw`,
     `__amix_vfs_getvfssw`: upstream renames the stock tables and
     supplies larger ones. Drivers do not need these linkages; stage 1
     leaves `dlm_slot.c`'s exec part, `dlm_str.c`'s `fmodsw` part and
     `dlm_fs.c` out (or stubs their table hooks). STREAMS modules, exec
     formats and filesystems come later, by the same renaming done on
     the PC side.
3. **`cdevsw` layout.** ASV's `struct cdevsw` has two more entries than
   AMIX's, `d_driver` and `d_devmap`. Upstream reads module data by
   byte offset (`CD_NFN`, `CD_STR`, `CD_FLAG`, `DRVSZ`, `QISZ`, ...):
   every such constant gets a compile-time check against ASV's headers.
4. **Major numbers.** ASV indexes `cdevsw` by *internal* major:
   `getmajor()` is `MAJOR[external]`, and `cdevsw` has only the rows
   `cunix` configured (34 on build 18; our drivers use external majors
   41-46, clone minors included). Spare rows come from a few empty
   placeholder drivers in the kernel configuration (`asv/master.d`,
   one row each); registering a module's major points `MAJOR[ext]`
   (and `MINOR[ext]` = 0) at a free spare row. The clone driver
   (`/dev/en0` = clone minor 41) goes through the same table.
5. **Symbol table size.** ASV's kernel exports 3164 symbols: a 96 KB
   table (upstream reserves 160 KB). Against it: the drivers that move
   out are about 109 KB (nu 50 KB, ub 32 KB, dp 11 KB, midi 10 KB, snd
   and tlk 3 KB each). A trimmed table (the symbols drivers use, an
   export list) would make the module kernel smaller on every TT; to be
   measured.
6. **Relinks.** `buildsys` (cunix) relinks on the TT; after any relink
   `dlm_ksym` must be filled again. `mkksym` is built for ASV (static,
   `rsync/asv-static-cc`) and run by `buildsys` after `cunix`.
7. **The boot loader.** It reconfigures whenever its probes put a
   device in the EDT that the kernel has no driver for ("No driver for
   TLK board", then mUNIX relinks). Module drivers must not be in the
   EDT: their rows leave `/stand/edt_data` and `EDTP.dp`, `EDTP.nu`,
   `EDTP.tlk` leave `/stand`, and the
   modules probe their hardware in their `_load` (the drivers already
   check the chip themselves: nu reads the RTL8019, tlk probes every
   register, ub looks for a controller). `sp1hw` then decides by what
   loads, not by `prtconf`'s rows.
8. **Interrupts.** nu and the NetUSBee/Lightning USB are polled; dp
   works through the SCSI driver; tlk, snd and midi need a look at
   how their interrupts are wired (EDT vector, MFP) before they move.

## Found while building stage 1

- ASV's `tar` crashes (SIGILL) on an archive with unknown owners: payloads
  are `tar --format=ustar --owner=root --group=root`, as the release's.
- ASV's `cputype` holds 68030 or 68040; upstream's cache flush compares
  with 30/40/60, so a 68030 ran the 68060's `cpusha` and the machine
  reset on the first module load. `asv/dlm_cache_asv.s` compares with
  68040 and 68060.
- `cunix` gives a driver without entry points (the placeholders) open
  and close `nulldev`, the rest `nodev`, `d_driver`/`d_devmap` 0 and
  `d_flag = &nodevflag` (D_OLD). At boot ASV moves every D_OLD row's
  entries into `shadowcsw` and fills the row with its gateway
  (`gen_copen` .. `gen_ioctl`, SVR3 calling conventions behind them), so
  `dlminit` recognises a placeholder by its shadow row and puts the row
  back to the plain empty form; a module's row is D_NEW (upstream's
  placeholder flag), so the kernel calls the trampolines directly.
- `getmajor()` is a function on ASV (`MAJOR[]` lookup), as the macro.

## Stages

1. **The loader in the kernel.** DONE in Hatari 2026-10-29
   (`~/dev/asv-release/probe/dlmtest.py`: build 18 + `install.sh`, a
   reboot, then modules): `buildsys` fills the table (3248 symbols),
   `modadmin` loads `dlmtest` with its dependency `dlmdep`, queries,
   unloads, loads by path; eight majors register on the eight spare
   rows, a ninth gets ECONFIG, a static driver's major EEXIST. `asv/` builds `/boot/DLM` (upstream
   core + ASV glue, misc and character/STREAMS driver linkages),
   `master.d` entries for it and the spare rows, `mkksym` for ASV, and
   `buildsys` running it. Test in Hatari on a copy of the release image:
   `modadmin -s`, load and unload a small misc module, `getksym`.
2. **One driver as a module.** nu or dp as a STREAMS driver module on its
   registered major; `/dev/en0` or `/dev/nu0` opens load it; DHCP and
   `ufetch` through it. dp first: Hatari emulates the DaynaPORT, not the
   NetUSBee.
3. **Detection by module.** `sp1hw` loads what is there (each module's
   `_load` fails with ENXIO without its hardware); the EDT probes for
   module drivers go; the image's kernel carries no sp1 drivers, so the
   first boot needs no relink at all.
4. **The rest.** ub, tlk, snd, midi, amx as modules; then STREAMS
   modules, exec formats and filesystems (the `__amix_*` renaming) if
   wanted. Release image built this way; the real TT test.

Hardware tests on the real TT need the owner's go-ahead (kernel change).

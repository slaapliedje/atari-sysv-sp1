# Transputer links on the ATW800/2

`tprobe` finds the transputers on the card's two link interfaces (C011-style
byte registers, Programmer's Manual "Programming the Transputer"):

| link | TT address | what is behind it |
|---|---|---|
| FPGA link | 0xFEDFFAC0 | the T425 inside the FPGA |
| C011 link | 0xFEFFFAC0 | a physical transputer in TRAM slot 1 ("+T" cards) |

It resets the transputer, POKEs a word and PEEKs it back, then boots the
manual's 23-byte type probe (1 answer byte = C004, 2 = 16-bit, 4 = 32-bit).
`-n` only reads the status registers. Every wait is time-limited.

An AMIX program, built with OpenUA's `toolchain/sysv4-cc`:

```sh
AMIX_SYSROOT=... sysv4-cc -std=gnu99 -O1 -m68020-60 -msoft-float -c tprobe.c
AMIX_SYSROOT=... sysv4-ld -o tprobe tprobe.o sysv_rt.o
```

Measured on a real TT (2026-09-25), through the tlk driver (`ttest`):

| link | read | write (POKE stream, PEEKed back) |
|---|---|---|
| C011 (TRAM slot 1) | ~95 KB/s | ~170 KB/s |
| FPGA (T425) | ~100 KB/s | ~180 KB/s |

Both answer as 32-bit transputers and stream in sequence. The C011 link
first ran at 2 KB/s: ASV's `cc -O` ignored `volatile` and read the status
register once per polling loop, so the driver gave up after two bytes
and every output wait slept a clock tick. The driver is built without
`-O` (see `driver-tlk/install.sh`). `tdiag` (root) times the link from
inside the driver, byte by byte, with the TLK_DIAG ioctl.

`tfpu` boots a program that aims two FPU instructions at a marker word
(`-n` skips them, as a control) and reports `lddevid`. Measured:

| link | FPU test | `-n` control | lddevid |
|---|---|---|---|
| C011 (TRAM slot 1) | 0x00000000: FPU present, a T8xx | marker kept | 0x80000004, with or without the FPU part (not a device code: likely a T800 that predates lddevid) |
| FPGA (T425) | no answer: it stops on FPU instructions | marker kept | 2 |

### The network, as found on this TT (2026-09-25)

```
TT --C011--> T8xx (TRAM slot 1) link 0
               link 1 --> T8xx (TRAM slot 2, presumably), FPU too
               link 2 --> nothing
               link 3 --> the FPGA's T425
TT --FPGA link--> T425
```

- `tspy [/dev/linkN]` boots `linkspy.tas` on the transputer behind a
  link: one process per link 1-3 PEEKs 0x80000100 on the far side, and
  after ~128 ms (the timer) it reports which answered. It first POKEs a
  marker into the T425 through `/dev/link1`, so the link that answers with
  the marker is the one to the T425. (Booted on the T425 itself it never
  reports: that FPGA transputer probably lacks the timer instructions.)
- `tfpu -r` boots `relay.tas` (link 0 <-> link 1, byte by byte) on the
  first transputer and runs the FPU test on the second, through it.
- `tasm.py` assembles these (`tasm.py X.tas -c NAME > X.h`); it
  reproduces the manual's type probe byte for byte.

Reset/error is at +0x11 and analyse at +0x17 (the manual's GfA demo);
+0x21/+0x23 only mirror the input data, and a transputer left running
cannot be stopped through them. A byte left in the input register
survives a reset, so tprobe drains it.

`/dev/mem` cannot map the C011's page (the kernel tests a page by
reading its first long, and 0xFEFFF000 bus-errors), so `tprobe` reaches
only the FPGA link; the driver reaches both.

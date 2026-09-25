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

Reset/error is at +0x11 and analyse at +0x17 (the manual's GfA demo);
+0x21/+0x23 only mirror the input data, and a transputer left running
cannot be stopped through them. A byte left in the input register
survives a reset, so tprobe drains it.

`/dev/mem` cannot map the C011's page (the kernel tests a page by
reading its first long, and 0xFEFFF000 bus-errors), so `tprobe` reaches
only the FPGA link; the driver reaches both.

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

Measured on a real TT (2026-09-24): the FPGA link answers, POKE/PEEK round
trips and the type probe reports a 32-bit transputer. A byte left in the
input register survives a reset, so tprobe drains it first.

The C011 link cannot be reached this way: `/dev/mem` tests a page by
reading its FIRST long (the kernel's `memprobe`), and 0xFEFFF000 bus-errors
although the C011's registers at 0xFEFFFAC0 do not. That needs a kernel
driver.

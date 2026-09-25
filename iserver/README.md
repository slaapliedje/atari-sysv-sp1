# iserver for Atari System V: the INMOS toolset on the ATW800/2's transputers

`iserver` is the INMOS host file server: it boots a program on a
transputer and then serves its file, terminal and environment requests.
This builds it for Atari System V / AMIX over the tlk driver
(`../driver-tlk`: `/dev/link0` = the C011 and the TRAM in slot 1,
`/dev/link1` = the FPGA's T425), and with it the INMOS C and occam
toolset (d72uni) runs on the TRAM with the TT as its host.

Verified on a real TT (2026-09-25): `icc`, `ilink` and `icollect` running
on the slot-1 T800 compile, link and collect a C program in the TT's
file system, and the result boots and prints through iserver.

## What is here, and what is not

`lnktlk.c` (the link module: OpenLink/ReadLink/WriteLink/ResetLink/...
over `/dev/linkN`), `asvhost.h`, `build.sh`, `itool.sh`, `mkwrappers.sh`.
The iserver sources themselves are INMOS's ((c) 1988, All Rights
Reserved) and the toolset is the ATW800/2 file share's; neither is in
this repository. `build.sh` takes the iserver from the share
(`Transputer/atari.dev/iserver`), edits a few lines (see the script) and
builds it with OpenUA's `toolchain/sysv4-cc` as a static AMIX program:

```sh
AMIX_SYSROOT=... sh build.sh .../Transputer/atari.dev/iserver iserver
```

## Installing on the TT

```sh
# on the PC: iserver + itool + the toolset
mkdir -p transputer/bin transputer/itools
cp iserver transputer/bin/; cp itool.sh transputer/bin/itool
cp .../Transputer/d72uni/itools/*.btl transputer/itools/
cp -r .../Transputer/d72uni/libs .../Transputer/d72uni/iterms transputer/
tar cf transputer.tar transputer      # plain tar: AMIX gzip hung on it
# on the TT, as root
cd /usr/local/lib && tar xf transputer.tar && sh mkwrappers.sh
```

Then, with `/usr/local/lib/transputer/bin` on the PATH:

```sh
icc hello.c -t800
ilink hello.tco -t800 -f startup.lnk
icollect hello.lku -t -o hello.btl
iserver -sb hello.btl -sl /dev/link0
```

The tools take `-` options and Unix paths (iserver reports a Unix host).
`itool` sets the Atari profile's environment: `TRANSPUTER=/dev/link0`,
`ISEARCH`, `ITERM`, `IBOARDSIZE=#200000` (both TRAMs have 2 MB, measured
with `../tools/tram/tmem`; the T425 has 1 MB).

Notes:

- `itool` runs iserver without `-se`: the error flag reads set on this
  TRAM whatever runs, and the Atari build's `TestError` always answers 0,
  so its scripts never really tested it either.
- `lnktlk.c` gives SP.GETENV fallbacks for IBOARDSIZE and ISEARCH, as the
  Atari build does.

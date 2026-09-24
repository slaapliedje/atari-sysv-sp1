# ATW800/2 test programs

ASV-native (they use `/dev/mem`, and AMIX's `mmap` does not survive the amx
module), built with `../../rsync/asv-static-cc`:

```sh
ASV_SYSROOT=... ../../rsync/asv-static-cc -o atwprobe atwprobe.c
```

| program | touches | what it tells |
|---|---|---|
| `atwprobe` | nothing (reads only) | which 1 MB windows of 0xFEA00000..0xFEDFFFFF answer, and where the id block is: a 2 MB window, or a 4 MB one (jumpers A0+A1 closed) and whether it mirrors |
| `atw4mb` | register 15, two off-screen VRAM words (restored) | whether register 15 = 3 gives a real 4 MB; puts the 2 MB layout back unless given `keep` |
| `atwbars 16\|32` | the whole screen and mode | 640x480 at 16 or 32 bpp with red/green/blue/white/black bars, written in the byte order the Hatari fork models. On a real card, the colours on the monitor say whether that order is right. Restart X afterwards |

`atw4mb` on a real V0205 card (2026-09-24): mirrored with 15 = 1,
unmirrored 4 MB with 15 = 3. Beware: running a 4 MB detection while a 2 MB
server runs leaves that server drawing into memory the card is not showing.

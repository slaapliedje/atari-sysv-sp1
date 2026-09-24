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
| `atwtest` (via `atwtest.sh`, from an xterm on the TT, as root) | video memory, VTG, register 15 - then X's 1024x768 8 bpp mode and the 2 MB layout back, and `xrefresh` | the real 16/32 bpp byte orders: 1024x768 screens with the same bands (red, green, blue, white, grey ramp) in each candidate order, one column each, numbered by 1-4 white squares; Return steps on. 16 bpp: 1 = little-endian RGB565 (the manual), 2 = big-endian. 32 bpp: 1 = B,G,R,x, 2 = x,R,G,B, 3 = R,G,B,x, 4 = x,B,G,R. `atwtest.sh offset`: the 32 bpp screen with the display starting one byte in (column 2 right = the start register counts bytes) |

`atw4mb` on a real V0205 card (2026-09-24): mirrored with 15 = 1,
unmirrored 4 MB with 15 = 3. Beware: running a 4 MB detection while a 2 MB
server runs leaves that server drawing into memory the card is not showing.

Results on the real V0205 card (2026-09-24): **16 bpp column 1** (little-
endian RGB565, as the manual says) and **32 bpp column 3** (bytes R, G, B,
x). The Hatari fork had guessed B, G, R, x for 32 bpp and was corrected.

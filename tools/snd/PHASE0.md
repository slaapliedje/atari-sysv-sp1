# Sound engine, phase 0: what the TT can afford

Measured on the real TT030 (32 MHz 68030, ASV, the cross GCC 2.7.2 at -O2),
2026-09-28. The programs are beside this file; build each with
`m68k-cbm-sysv4-gcc -O2 -o NAME NAME.c` and run it on the TT.

## Interrupts (the MFP module, /boot/MFP)

Drivers claim an MFP line with
`mfp_intrreq(line, mfp, handler, arg, 1, 0, ipl)` (mfp 0 = the ST MFP at
FFFA00, 1 = the TT MFP at FFFA80). The claims in the stock kernel:

| module | MFP | line | source |
|---|---|---|---|
| CEN   | ST | 0  | GPIP0, printer busy |
| IKBD  | ST | 6  | GPIP4, keyboard/MIDI ACIA (ipl 6) |
| FFD   | ST | 7  | GPIP5, floppy/ACSI |
| USART | either | 12 | receive full (+ Timer D, baud rate) |
| SCCIO | TT | 3  | GPIP3 |
| CLOCK | TT | 14 | GPIP6, the RTC: the system clock |
| SCSI  | TT | 15 | GPIP7 |

The ST MFP's **Timer A (line 13) and GPIP7 (line 15) are free**: both are
driven by the DMA sound's end of frame (Atari Compendium, "Sound
Interrupts using MFP Timer A" / "using GPIP 7").

## Clock

`hertz` is **128** (khz.c reads it and times lbolt), from the RTC's
periodic interrupt. uptime(1) assumes 100 and reads 28% high.

## Caches and memory (cachet.c)

| loop | cycles per iteration |
|---|---|
| add + subq + bne, registers only | 11.6 |
| C loop reading a 128-byte array | 24.1 |
| C loop reading a 1 MB array, scattered | 48.6 |

The caches are on; a read that misses the 256-byte data cache costs ~25
cycles more.

## Mixing (mixbench.c): CPU per second of 25033 Hz sound

| mixer | CPU |
|---|---|
| A: OpenUA's today, 4 wavetables (256 bytes each), no volume, mono | 17% |
| B: 8 voices, volume table, hard L/R, sample-major, C | 106% |
| C: as B with a pan per voice | 122% |
| D: 8 voices, volume table, hard L/R, voice-major, C | 62% |
| E: as D, the inner loop in 68030 assembly (checked identical to D) | 47% |

About 6% per general voice at 25 kHz, 3% at 12.5 kHz: sample and volume
table reads miss the cache. OpenUA's wavetables fit in it, hence A.

## Decisions for phase 1

- Mix from the DMA frame interrupt (Timer A), but LOWER the interrupt
  level before mixing: milliseconds at ipl 6 would overrun the MIDI ACIA
  (one byte per 0.32 ms).
- Keep a wavetable fast path for OpenUA's four fixed voices.
- General voices use the assembly loop; the output rate is chosen per
  song (MODs: 12.5 kHz for 8 channels, 25 kHz for 4).

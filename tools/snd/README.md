# DMA sound tests

For `../../driver-snd` (`/dev/audio`: the TT's 8-bit DMA sound; 6258,
12517, 25033 or 50066 Hz; mono or stereo, signed samples, left first).

- `sndrate`: 2 s of silence at each rate, timed by the TT's clock. On the
  real TT (2026-09-25) every rate takes 2.00-2.06 s.
- `sndtest [rate]`: 2 s of 440 Hz mono, 2 s of 440 left / 660 right, a
  2 s sweep 200-2000 Hz; 6.1 s in all on the real TT. Its samples come
  from a table with an integer phase accumulator: floating point per
  sample (or libm's `sin()`) is too slow for 25 kHz on the 68030.

Both are AMIX programs (OpenUA's `toolchain/sysv4-cc`, `-m68881` for
sndtest's table set-up, linked with `-lm`).

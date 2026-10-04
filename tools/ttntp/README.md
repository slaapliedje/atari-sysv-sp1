# ttntp: the clock from the network, and a clock chip TOS agrees with

`ttntp` sets Atari System V's clock from an NTP server (SNTP): a static
program with its own DNS lookup, so it needs nothing but the socket
library.

```
ttntp [-n] [-q] [-z] [server ...]      default server: pool.ntp.org
ttntp [-q] -b
  -n   show how far off the clock is, change nothing
  -q   quiet (cron, boot)
  -z   once the clock is right, keep the TT's clock chip on local time, as
       TOS does, and write it
  -b   at boot: the kernel read the chip's local time as GMT; move the
       clock by the zone's offset (once; the boot script does it)
```

ASV steps its clock only in whole seconds (`stime()` keeps the fraction;
`settimeofday()` just rounds and calls it), so `ttntp` steps the whole
seconds and slews the rest with `adjtime()`; an error under half a second is
only slewed. Measured on a real TT against pool.ntp.org: 5740 s slow, then
within 0.011 s.

Build: `ASV_SYSROOT=... ../../rsync/asv-static-cc -O -I../../rsync/include -o ttntp ttntp.c -lsocket`

## Setting it up

```sh
cp ttntp /usr/local/bin/
echo "nameserver <your DNS server>" > /etc/resolv.conf
echo "<your router>" > /etc/defaultrouter       # if there is no default route
cp S99ttntp /etc/rc2.d/                         # at boot
crontab -l > /tmp/c; echo "23 * * * * /usr/local/bin/ttntp -q -z" >> /tmp/c; crontab /tmp/c
```

and the time zone in `/etc/TIMEZONE`.

## Time zones: today's daylight-time dates

ASV's 1991 libc reads a plain `TZ=MST7MDT` as a POSIX-style string and
applies its own built-in rules - the 1987 US ones, daylight time from the
first Sunday in April to the last in October - and it does not understand
the POSIX rule syntax (`M3.2.0`) that would override them. It reads the
compiled zone files in `/usr/lib/locale/TZ` only when `TZ` starts with a
colon. So `usa.zic` recompiles the four US zones with the rules in force
since 2007 (the second Sunday in March to the first Sunday in November),
and `TZ` names the file:

```sh
cd /usr/lib/locale && tar cf /somewhere/TZ.pre-2007.tar TZ   # keep the old ones
zic -d /usr/lib/locale/TZ usa.zic
# /etc/TIMEZONE:  TZ=:US/Mountain   (or :US/Eastern, :US/Central, :US/Pacific)
```

Checked on a real TT: 20 March and 28 October 2026 read MDT with
`TZ=:US/Mountain` and MST with the old plain `TZ=MST7MDT`.

## The clock chip

The TT's MC146818 clock is shared with TOS, and three things kept ASV and
TOS from agreeing on it:

- **The year.** TOS counts the chip's year byte from 1968. ASV's clock
  driver (`/boot/CLOCK`: `rtodc` reads it, `wtodc` writes it) counted from
  1970, so a year TOS wrote as 2026 read as 2028. `patch-clock.py` makes the
  driver count from 1968: six same-size edits, each checked against the
  original bytes. Install it and relink the kernel - keep the original
  OUTSIDE `/boot`, which `buildsys` links whatever it finds in:

  ```sh
  cp /boot/CLOCK /stand/CLOCK.pre-1968;  cp /stand/unix /stand/unix.pre-1968
  python3 patch-clock.py CLOCK CLOCK.1968        # on the PC
  cp CLOCK.1968 /boot/CLOCK;  /sbin/buildsys;  init 6
  ```

  Correcting a year this way leaves password-aging dates set while the
  clock read 2028 in the future: `/etc/shadow`'s third field (days since
  1970) wants resetting, or logins demand a new password.

- **Local time.** The driver keeps the chip at GMT minus an offset it is
  told (an ioctl on `/dev/rtc`, `'R'<<8 | 1`, seconds west), and writes the
  chip only once it has been told, which nothing on the stock system does -
  so setting ASV's clock never reached the chip. `ttntp -z` tells it the
  zone's offset and has the kernel write the time. At every boot the
  offset is 0 again, so the kernel reads the chip's local time as GMT:
  6 hours slow in MDT. The stock `/sbin/setclk` (from `/etc/inittab`) is
  meant to correct that from `TZ`, but on a real TT it changes nothing,
  with `TZ=:US/Mountain` or `TZ=MST7MDT`. `ttntp -b` does it instead.

  Until 2026-10-04 `-z` wrote the chip before asking the network for the
  time. At boot the clock was then still the kernel's 6-hour-slow reading,
  so every boot whose network wasn't up yet (a BlueSCSI's WiFi after a
  power-on) wrote that into the chip and lost another 6 hours, and ASV's
  `setclk` printed "Time of Day Clock needs Restoring". Now the chip is
  written only after a time server has answered, and `S99ttntp` runs
  `ttntp -b` and then retries `ttntp -z` every 15 s for 10 minutes.

- **The zone.** The stock `/etc/TIMEZONE` names US Eastern.

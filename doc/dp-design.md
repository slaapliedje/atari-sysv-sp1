# `dp` — a DaynaPORT SCSI/Link ethernet driver for Atari System V

Status: DESIGN, nothing built. 2026-09-20.

## Goal

TCP/IP on the TT030 under ASV through the ZuluSCSI Blaster's DaynaPORT
emulation (WiFi), so telnet/ftp/rsh work and files stop travelling by SD card.

## What the two ends look like

### The device (grounded in ZuluSCSI-firmware `network.c`, fetched to this dir)

Enabled by a file whose name starts `NE<id>` on the card (e.g. `NE4.img`) and
`WiFiSSID` / `WiFiPassword` under `[SCSI]` in `zuluscsi.ini`. INQUIRY reports a
processor-type (3) device. Six-byte CDBs, length in `cdb[3..4]`:

| op | meaning | data |
|---|---|---|
| `0x09` | get MAC + stats | IN 18: MAC[6] + 3 x u32 (zeros) |
| `0x0E` | interface on/off | none; `cdb[5] & 0x80` = enable (also flushes the rx queue) |
| `0x0A` | send | OUT. `cdb[5]==0`: exactly one raw frame of `len` bytes. `cdb[5]==0x80`: repeated `[len16][0][0]` + frame, terminated by len 0 |
| `0x08` | receive | IN. Always a 6-byte header `[len16][0][0][0][flag]`, then the frame. **Nothing queued = 6 zero bytes.** `flag 0x10` = more follow in this transfer (only if `cdb[5] & 0x40`) |
| `0x0D` | add multicast | OUT 6 |
| `0x0C`, `0x40`, `0x80`, `0x1A` | accepted, ignored | |

Consequences: no interrupt (receive is POLLED), a receive is a SHORT transfer
(we ask for 1530, the target ends DATA IN after 6 or 6+len), and a receive of
size 1 is rejected with CHECK CONDITION.

### The kernel (from the unstripped objects + headers on the disk)

- Drivers are `/boot/<NAME>` ELF relocatables + `/etc/master.d/<name>`, linked by
  `mkboot`/`cunix`; software drivers need `INCLUDE:<NAME>` in `/stand/system`.
- SCSI client API (`sys/scsi.h`, used by `/boot/TP` and `/boot/HD`):
  `sj_init()` scans the bus once; `sj_selectjob(target, lun)` returns the
  target's `struct scsijoblock` (NULL if absent); a driver CLAIMS a target by
  replacing `sj_startf` (unclaimed = `sj_badstart`); work is queued as `buf`s on
  `sj_tab[]` and the driver's `START(job, done)` callback builds the CDB
  (`scsi_cmd6`), sets `sj_jobp`/`sj_jobcount`/`sj_flags` and calls
  `sj_jobentry(job)`; completion re-enters `START` with `done` set, where
  `sj_bytesdone` is the real transfer length. `sj_op()` is the synchronous
  wrapper (private buf + `C_*` code in `b_error` + iowait). Interrupts at spl4.
- `hdopen` refuses any target whose `sj_dtype != 0`, so there is NO user-space
  shortcut through `SCSIGENIOCTL` — this has to be a kernel driver.
- Network side: the stack is Lachman STREAMS TCP; an interface is a DLPI
  (1990 `sys/dlpi.h`) style-1 driver opened by `slink` per `/etc/strcf`
  (`cenet`: open device, push `app` for ARP, link under IP/ARP). The stock
  `/boot/LA` is a generic DLPI half ("engen", ~3.3 KB: `enopen enclose enwput
  en_proto en_wproc en_rproc en_send_up en_error_ack en_set_ifstats`) glued to a
  Lance half through `laencustom.h` (`EN_LINIT`, `EN_XMIT`, `EN_IOCTL`,
  `enstart`). We have the glue header but NOT the engen source.

## Design

One object, `/boot/DP`, master file `dp` (flags `sf`-style STREAMS software
driver — exact letters to be copied from a stock STREAMS software driver such
as `llcloop`), device node `/dev/dp0` via the clone convention LA uses.

```
        IP / ARP(app)                     (stock, unchanged)
            |  DLPI: DL_INFO/BIND/UNBIND/UNITDATA_REQ -> DL_UNITDATA_IND
   +--------+---------+
   |  dp DLPI half    |  re-implemented in C from the DISASSEMBLY of LA's engen
   +--------+---------+  half, so primitive/ioctl behaviour matches what this
            |            stack was tested against
   +--------+---------+
   |  dp SCSI half    |  tx queue, rx poll, 3 job types on ONE claimed target
   +--------+---------+
            |  sj_selectjob / START / sj_jobentry      (stock SCSI core)
        ZuluSCSI DaynaPORT (WiFi)
```

### SCSI half

- `dpinit`: `sj_init()` if needed, scan targets 0..7 for `sj_dtype == PROC` whose
  INQUIRY vendor starts `Dayna`; claim it (`sj_startf = dpSTART`). No device =
  driver stays inert (open returns ENXIO) — it must never break a boot.
- State: one static tx buffer and one static rx buffer (1536 bytes each, kernel
  data, so no DMA-reach questions — transfers use the core's PIO path unless
  `USEDMA` proves necessary), flags `TXBUSY`/`RXBUSY`, a small mblk tx queue.
- `dp_linit` (first DL_BIND): cmd `0x0E` enable, then cmd `0x09` -> MAC into
  `if_enaddr`, retried for up to 1 s (see *Real adapters* below); start the
  poll timer.
- `dp_xmit(mp)`: flatten the mblk chain into the tx buffer, pad to 60, issue
  `0x0A` with `cdb[5]=0` (one frame per command — simplest mode the firmware
  has). If a job is in flight, queue the mblk; `dpSTART(done)` sends the next.
- Receive: `timeout(dp_poll, , HZ/50)`. Each tick, if no job is in flight, issue
  `0x08`, length 1530, `cdb[5]=0x80` (single-packet mode: bounded bus time, no
  multi-packet parsing). On completion: `len = rx[0]<<8|rx[1]`; 0 = idle; else
  `allocb`, copy, hand to the DLPI half, and IMMEDIATELY poll again (drain the
  queue at bus speed, fall back to the timer when empty). Adaptive: after N empty
  polls back off to HZ/10 so an idle link does not hammer the SCSI bus the root
  disk shares.
- tx has priority over rx when both are pending (rx is re-polled anyway).
- `dpclose` of the last stream: cmd `0x0E` disable, `untimeout`.

### DLPI half

Re-created from LA's engen disassembly (functions listed above; it is small and
unstripped). Ethernet II only, SAP = ethertype, `DL_ETHER`, style 1, per-minor
`enminor_t {rdq, state, sap}` and one `en_info_t` exactly as `sys/engen.h`
declares them, so `ifstats` and the `slink`/`ifconfig` ioctls see what they
expect. Promiscuous/multicast: multicast add maps to cmd `0x0D`; the rest
returns the same errors LA returns.

## Real adapters (the Dayna ROM) vs the emulated ones

The first version was written against ZuluSCSI, which accepts every command
at any time. A real SCSI/Link (ROM v2.0, per the SCSI/Link implementor's
guide) does not, and against it that version **hung the boot**:

- While disabled the ROM answers CHECK CONDITION to every data command. The
  driver read the MAC before enabling. And `dpSTART` zeroed `q_errcnt` on every
  call, including `sj_terminate`'s retries (`START(job, 0)`). `sj_terminate`
  fails a buf after five retries counted in that byte, so the refused command
  was retried forever. Now `q_errcnt` is reset only for a new buf (`flag`
  set).
- For about 500 ms after ENABLE the ROM refuses data commands again, while
  TEST UNIT READY answers GOOD. Bring-up is now: enable, then retry the MAC
  read (22 bytes as the ROM answers, then the emulators' 18) every 10 ms for
  up to 1 s.
- After it drops a packet (about 6 KB of buffer), the ROM answers every receive
  with the four bytes after the length all `0xFF` until it is disabled and
  enabled again. That header is now recognised and triggers an asynchronous
  disable/enable (`dp_reinit`). So do three consecutive failed commands outside
  the post-enable window (`DP_MAXERRS`), which covers an adapter that reset
  itself underneath the driver.
- Transmit already pads to the 60-byte minimum (the driver's job; not every
  adapter pads).
- Multicast: ASV's 1990 `sys/dlpi.h` has no multicast primitives, so cmd `0x0D`
  is never needed.

All of this is testable without the hardware. The Hatari fork's DaynaPORT
takes `--scsi-net 4=tap0,rom` (ROM command gating and the settle window) and
`,wedge=<n>` (the dropped-packet state after n received frames).
`TAP=tap0,rom,wedge=40 tools/hatari-asv.sh …` with a 400-packet ping gave 11
wedges and 11 recoveries, 2.5% loss and no panic. The same boot in the default
mode loses nothing. The first version under `,rom` stops for good after the
TCP banner.

## Risks, in the order I expect them to bite

1. **Short DATA IN transfers.** The core must end a job cleanly when the target
   leaves DATA IN early. Tape (variable records) does this, so there is a path —
   find which flag (`CMDRET`? plain residual) and mirror TP. FIRST experiment.
2. **Bus sharing with the root disk.** One initiator, one bus: a poll blocks
   disk I/O for its duration. Mitigated by single-packet reads + back-off.
3. **No emulator.** (Since solved: the Hatari fork emulates it, including
   the ROM's behaviour; see above.) Hatari has no DaynaPORT, so every run is on the real TT.
   Mitigation: build the driver in stages, each provable from the console:
   stage 1 = claim target + print MAC at boot (no STREAMS at all);
   stage 2 = raw tx/rx self-test (send an ARP who-has, print what comes back);
   stage 3 = DLPI half + `slink`, `ifconfig`, `ping`.
4. **A bad driver can stop the boot.** Keep `HD0.bin` backups (already routine)
   and a known-good `/stand/unix` copy on the BFS to boot by name from the
   loader prompt.
5. **engen fidelity.** If my DLPI half differs from what `app`/IP expect, the
   link fails silently. Mitigation: port function by function from the
   disassembly, not from the DLPI spec.
6. **master.d / mkboot mechanics** for a STREAMS software driver are still
   unverified (flags, `INCLUDE`, how the rebuild is triggered by hand).

## What the user needs to supply

- `NE4.img` on the card (any small file) and WiFi credentials in
  `zuluscsi.ini` — typed in by hand, never sent to me.
- Check first that ASV still boots with the extra SCSI target present.

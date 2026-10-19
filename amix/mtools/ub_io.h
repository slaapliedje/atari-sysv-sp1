/* ub_io.h - mtools on Atari System V: a drive on the USB stack's storage
 * unit (sp1 driver-ub), "ub:N" as the drive's file in mtools.conf */
Stream_t *UbOpen(struct device *dev, const char *unit, int mode,
		 char *errmsg, mt_off_t *maxSize);

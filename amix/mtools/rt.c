/*
 * rt.c - what mtools needs that AMIX's libraries lack (rt.a, linked into
 * mtools by amix-cc); snprintf comes from Manx's os/sysv4/snprintf.c.
 */
#include <sys/types.h>
#include <poll.h>

/* the mint GCC, an a.out compiler, calls __main at the top of main() */
void __main(void) { }

int usleep(unsigned us)
{
	return poll((struct pollfd *)0, 0, (int)((us + 999) / 1000));
}

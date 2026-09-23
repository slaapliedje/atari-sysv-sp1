/* build-host tools (mksignames, mksyntax...) see the TARGET's config.h,
 * whose type macros (ssize_t, intmax_t, socklen_t...) collide with the
 * host's typedefs; include the host headers first so they are skipped */
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

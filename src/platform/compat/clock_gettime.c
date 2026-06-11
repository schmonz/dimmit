#include "platform/compat/clock_gettime.h"

#if defined(__APPLE__) && (!defined(MAC_OS_X_VERSION_MIN_REQUIRED) || MAC_OS_X_VERSION_MIN_REQUIRED < 101200)

#include <sys/time.h>
#include <errno.h>

/*
 * CLOCK_REALTIME via gettimeofday(): wall-clock seconds + microseconds, the
 * same quantity 10.12's clock_gettime(CLOCK_REALTIME) reports. dimmit uses no
 * other clock id; reject them rather than silently return wrong values. The
 * header renames callers' clock_gettime() to this symbol.
 */
int dimmit_clock_gettime(clockid_t clk_id, struct timespec *ts) {
    if (clk_id != CLOCK_REALTIME) {
        errno = EINVAL;
        return -1;
    }
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return -1;
    }
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = (long)tv.tv_usec * 1000;
    return 0;
}

#endif

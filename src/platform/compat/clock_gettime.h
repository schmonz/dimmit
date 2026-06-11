#ifndef DIMMIT_COMPAT_CLOCK_GETTIME_H
#define DIMMIT_COMPAT_CLOCK_GETTIME_H

/*
 * macOS gained clock_gettime() in 10.12. dimmitd.c calls
 * clock_gettime(CLOCK_REALTIME, ...). For deployment targets < 10.12 we supply
 * our own dimmit_clock_gettime(), implemented on gettimeofday() (present since
 * 10.0), and route callers to it. As with the IORegistry shim, the rename is
 * load-bearing: a build against a newer SDK (e.g. a Tahoe host targeting
 * -mmacosx-version-min=10.9) still declares clock_gettime attributed to
 * libSystem, so an unrenamed call two-level-binds there and dyld aborts at
 * launch on 10.9 ("Symbol not found: _clock_gettime"). We include the system
 * <time.h> first (under its real name, include-guarded) so its 10.12
 * availability-attributed declaration is parsed harmlessly before the macro.
 *
 * This replaces our former reliance on macports-legacy-support, whose
 * clock_gettime is gated on the *SDK* version (__MPLS_SDK_SUPPORT_GETTIME__)
 * and so never activates on a newer-SDK build.
 *
 * We only ever use CLOCK_REALTIME, which is wall-clock time -- exactly what
 * gettimeofday() returns and what both libSystem and legacy-support report for
 * CLOCK_REALTIME -- so behaviour is identical for our use.
 */
#include <AvailabilityMacros.h>

#if defined(__APPLE__) && (!defined(MAC_OS_X_VERSION_MIN_REQUIRED) || MAC_OS_X_VERSION_MIN_REQUIRED < 101200)

#include <time.h>

#ifndef CLOCK_REALTIME
typedef int clockid_t;
#define CLOCK_REALTIME 0
#endif

int dimmit_clock_gettime(clockid_t clk_id, struct timespec *ts);
#define clock_gettime dimmit_clock_gettime

#endif

#endif /* DIMMIT_COMPAT_CLOCK_GETTIME_H */

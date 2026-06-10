#include "platform/logging/logging.h"

/* The rc.d script (or syslog) owns log redirection on NetBSD, so the daemon
 * defers. Intentionally a no-op for now; could grow into openlog()/syslog()
 * later without touching the daemon. */
int logging_init(void) {
    return 0;
}

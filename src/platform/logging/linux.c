#include "platform/logging/logging.h"

/* systemd/journald captures the daemon's stdout/stderr (journalctl -u dimmitd).
 * Redirecting to a file would fight the platform, so this is intentionally a
 * no-op. */
int logging_init(void) {
    return 0;
}

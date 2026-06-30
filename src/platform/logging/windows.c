#include "platform/logging/logging.h"

/* No log redirection on Windows for the feasibility pass: the daemon runs from
 * a console (or with DIMMIT_LOG set by the caller), so stdout/stderr are fine as
 * delivered. A service-mode log destination is future work. */
int logging_init(void) { return 0; }

/* In-memory access-control backend for unit tests. before_bind/after_bind are
 * no-ops; is_authorized is driven by the global access_control_mock_authorized
 * so tests can exercise the accept/reject path. */
#include "platform/access-control/access-control.h"

int access_control_mock_authorized = 1;

int access_control_before_bind(void) { return 0; }

int access_control_after_bind(const char *sock_path) {
    (void)sock_path;
    return 0;
}

int access_control_is_authorized(int client_fd) {
    (void)client_fd;
    return access_control_mock_authorized;
}

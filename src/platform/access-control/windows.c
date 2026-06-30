#include "platform/access-control/access-control.h"

/* Windows DDC/CI (Monitor Configuration API) needs no elevated privilege, and
 * the AF_UNIX socket file is governed by filesystem ACLs. So the policy hooks
 * are no-ops: privilege is always available, the socket is left as created, and
 * any local client is authorized. Tightening (a per-user ACL check) is future
 * work, not needed for the feasibility pass. */

int access_control_before_bind(void) { return 0; }
int access_control_after_bind(const char *sock_path) { (void)sock_path; return 0; }
int access_control_is_authorized(int client_fd) { (void)client_fd; return 1; }

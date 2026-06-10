#include "platform/access-control/access-control.h"
#include <stdio.h>
#include <sys/stat.h>

/* macOS DDC needs no special privilege (verified on 10.9: neither root nor a
 * GUI session is required), so the daemon can run as any user, and any client
 * it admits is allowed. There is no group-based gating model here; we still
 * restrict the socket to owner/group rather than leaving it world-accessible. */

int access_control_before_bind(void) {
    return 0;
}

int access_control_after_bind(const char *sock_path) {
    if (chmod(sock_path, 0660) < 0) {
        perror("chmod");
        return -1;
    }
    return 0;
}

int access_control_is_authorized(int client_fd) {
    (void)client_fd;
    return 1;
}

#include "platform/access-control/access-control.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

/* NetBSD's DDC path (/dev/iic*) privilege requirements are not yet verified, so
 * before_bind is a no-op for now. The socket is restricted to owner/group and a
 * connecting client must belong to the wheel group.
 *
 * TODO: confirm whether NetBSD needs elevated privilege at startup (fill in
 * before_bind), and chown the socket to wheel in after_bind so wheel members
 * can actually reach it -- today it stays owned by the daemon's user, so the
 * wheel check in is_authorized can't yet be satisfied by a non-owner. */

int access_control_before_bind(void) {
    return 0; /* TODO: verify NetBSD privilege needs for /dev/iic* */
}

int access_control_after_bind(const char *sock_path) {
    if (chmod(sock_path, 0660) < 0) {
        perror("chmod");
        return -1;
    }
    return 0;
}

int access_control_is_authorized(int client_fd) {
    uid_t euid; gid_t egid; if (getpeereid(client_fd, &euid, &egid) < 0) return 0;
    struct group *wheel = getgrnam("wheel"); if (!wheel) return 1;
    struct passwd *pw = getpwuid(euid); if (pw && pw->pw_gid == wheel->gr_gid) return 1;
    /* Start with a reasonable group count and grow once if it overflows;
     * probing with a NULL groups array is not portable. */
    const char *uname = pw ? pw->pw_name : "";
    int ng = 32;
    gid_t *gs = (gid_t*)malloc((size_t)ng * sizeof(gid_t)); if (!gs) return 0;
    if (getgrouplist(uname, egid, gs, &ng) < 0) {
        gid_t *resized = (gid_t*)realloc(gs, (size_t)ng * sizeof(gid_t));
        if (!resized) { free(gs); return 0; }
        gs = resized;
        if (getgrouplist(uname, egid, gs, &ng) < 0) { free(gs); return 0; }
    }
    int ok = 0; for (int i = 0; i < ng; i++) if (gs[i] == wheel->gr_gid) { ok = 1; break; }
    free(gs);
    return ok;
}

/* _GNU_SOURCE: struct ucred / SO_PEERCRED and getgrouplist() are glibc
 * extensions. */
#define _GNU_SOURCE
#include "platform/access-control/access-control.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

/* Linux DDC writes go through /dev/i2c-*, which requires root. The daemon runs
 * as root and hands its socket to the "i2c" group so a non-root dimmit client
 * can still connect; a connecting client must belong to that group. */

int access_control_before_bind(void) {
    if (geteuid() != 0) {
        fprintf(stderr, "error: dimmitd requires root on Linux for /dev/i2c-* access\n");
        return -1;
    }
    return 0;
}

int access_control_after_bind(const char *sock_path) {
    struct group *i2c_grp = getgrnam("i2c");
    if (i2c_grp) {
        if (chown(sock_path, 0, i2c_grp->gr_gid) < 0) {
            perror("chown");
            return -1;
        }
    }
    if (chmod(sock_path, 0660) < 0) {
        perror("chmod");
        return -1;
    }
    return 0;
}

int access_control_is_authorized(int client_fd) {
    struct ucred cred; socklen_t len = sizeof(cred);
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) return 0;
    struct group *i2c_grp = getgrnam("i2c");
    if (!i2c_grp) return 1;
    struct passwd *pw = getpwuid(cred.uid);
    if (pw && pw->pw_gid == i2c_grp->gr_gid) return 1;
    /* Start with a reasonable group count and grow once if it overflows.
     * Passing NULL to probe the count is a glibc extension, not POSIX. */
    const char *uname = pw ? pw->pw_name : "";
    int ngroups = 32;
    gid_t *groups = (gid_t*)malloc((size_t)ngroups * sizeof(gid_t)); if (!groups) return 0;
    if (getgrouplist(uname, cred.gid, groups, &ngroups) < 0) {
        gid_t *resized = (gid_t*)realloc(groups, (size_t)ngroups * sizeof(gid_t));
        if (!resized) { free(groups); return 0; }
        groups = resized;
        if (getgrouplist(uname, cred.gid, groups, &ngroups) < 0) { free(groups); return 0; }
    }
    int ok = 0; for (int i = 0; i < ngroups; i++) if (groups[i] == i2c_grp->gr_gid) { ok = 1; break; }
    free(groups);
    return ok;
}

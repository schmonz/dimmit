#include "platform/logging/logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>

/* Under a per-user LaunchAgent, launchd captures no stdout and 10.9 has no
 * unified log, so the daemon redirects its own stdout/stderr to the user's
 * ~/Library/Logs/dimmitd.log. The plist stays user-agnostic (no path baked in),
 * which is also what a future /Library/LaunchAgents .pkg install needs: launchd
 * sets HOME per logged-in user, so we resolve the path here at runtime.
 *
 * Best-effort: failing to open the log never aborts the daemon. Skipped when
 * stdout is a tty (don't hijack interactive dev); DIMMIT_LOG overrides the path. */
int logging_init(void) {
    if (isatty(STDOUT_FILENO)) {
        return 0;
    }

    char path[1024];
    const char *override = getenv("DIMMIT_LOG");

    if (override && override[0]) {
        snprintf(path, sizeof(path), "%s", override);
    } else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) {
            struct passwd *pw = getpwuid(getuid());
            home = (pw && pw->pw_dir) ? pw->pw_dir : NULL;
        }
        if (!home) {
            fprintf(stderr, "logging_init: cannot resolve HOME; leaving stdio as-is\n");
            return -1;
        }

        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/Library/Logs", home);
        mkdir(dir, 0755);  /* ~/Library exists on macOS; ensure Logs/. EEXIST is fine. */

        snprintf(path, sizeof(path), "%s/Library/Logs/dimmitd.log", home);
    }

    if (!freopen(path, "a", stdout)) {
        fprintf(stderr, "logging_init: could not open %s; keeping stdout\n", path);
        return -1;
    }
    if (!freopen(path, "a", stderr)) {
        fprintf(stdout, "logging_init: could not redirect stderr to %s\n", path);
        /* A failed freopen() leaves stderr closed (C11 7.21.5.4p4); reopen it on
         * /dev/null so the caller can still safely fprintf(stderr) on our -1. */
        freopen("/dev/null", "a", stderr);
        return -1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);  /* line-buffer so `tail -f` is timely */
    setvbuf(stderr, NULL, _IONBF, 0);
    return 0;
}

#ifndef DIMMIT_PLATFORM_LOGGING_H
#define DIMMIT_PLATFORM_LOGGING_H

/* Per-platform log-destination policy, selected at build time (one <system>.c
 * backend per OS, exactly like platform/access-control). Called once at daemon
 * startup, before any output. Returns 0 on success, <0 on a non-fatal problem
 * the caller may warn about but must not abort on.
 *
 * Each platform expresses its own idiom: macOS redirects stdout/stderr to the
 * user's ~/Library/Logs (launchd captures nothing and 10.9 has no unified log);
 * Linux and NetBSD defer to journald / rc.d-syslog and do nothing. */
int logging_init(void);

#endif /* DIMMIT_PLATFORM_LOGGING_H */

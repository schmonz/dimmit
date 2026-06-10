#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

/* Per-platform privilege and socket-access policy for the daemon.
 *
 * The three hooks bracket the daemon's socket lifecycle; each platform
 * implements them in its own file (selected by the build):
 *   before_bind   -- acquire/verify the privilege needed to drive the hardware
 *   after_bind    -- expose the socket to the intended clients
 *   is_authorized -- decide whether a connecting client may drive brightness
 *
 * On Linux, after_bind and is_authorized share the "i2c" group: the socket is
 * handed to that group and clients must belong to it. */

/* Called once before bind(). Return 0 to proceed; -1 if a required privilege
 * is missing (fatal: the daemon prints an error and exits non-zero). */
int access_control_before_bind(void);

/* Called once after bind()+listen(), with the bound socket path. Adjust socket
 * ownership/permissions so intended clients can connect. Return 0 on success;
 * -1 on failure (non-fatal: the caller logs and continues). */
int access_control_after_bind(const char *sock_path);

/* Called per accepted connection. Return 1 if the client is authorized to
 * drive brightness, 0 otherwise. */
int access_control_is_authorized(int client_fd);

#endif /* ACCESS_CONTROL_H */

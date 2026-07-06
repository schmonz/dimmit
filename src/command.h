#ifndef COMMAND_H
#define COMMAND_H

#include "platform/compat/net.h"  /* dimmit_sock_t */

/* The wire command layer: how a client's textual request becomes a brightness
 * direction. Pure parsing here; socket reading is added alongside (read_command).
 * The magnitude is no longer decided here: displays have different maxes, so the
 * daemon converts a direction into a per-display fraction step (see dimmitd.c). */

/* Map a textual command to a direction: +1 (up), -1 (down), 0 (unrecognized). */
int parse_command(const char *cmd);

/* Read one command line from a connected socket and parse it to a direction.
 * Returns 0 on a closed/empty/unreadable connection or an unrecognized command
 * (+1 up, -1 down, 0 otherwise). Reads at most one short line; trailing newline
 * is ignored. */
int read_command(dimmit_sock_t fd);

#endif /* COMMAND_H */

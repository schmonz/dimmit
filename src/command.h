#ifndef COMMAND_H
#define COMMAND_H

/* The wire command layer: how a client's textual request becomes a brightness
 * delta. Pure parsing here; socket reading is added alongside (read_command). */

#define STEP 5

/* Map a textual command to a brightness delta; 0 means unrecognized. */
int parse_command(const char *cmd);

/* Read one command line from a connected socket and parse it to a brightness
 * delta. Returns 0 on a closed/empty/unreadable connection or an unrecognized
 * command. Reads at most one short line; trailing newline is ignored. */
int read_command(int fd);

#endif /* COMMAND_H */

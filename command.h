#ifndef COMMAND_H
#define COMMAND_H

/* The wire command layer: how a client's textual request becomes a brightness
 * delta. Pure parsing here; socket reading is added alongside (read_command). */

#define STEP 5

/* Map a textual command to a brightness delta; 0 means unrecognized. */
int parse_command(const char *cmd);

#endif /* COMMAND_H */

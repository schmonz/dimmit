#include "command.h"

#include <string.h>

int parse_command(const char *cmd) {
    if (strcmp(cmd, "up") == 0) return STEP;
    if (strcmp(cmd, "down") == 0) return -STEP;
    return 0;
}

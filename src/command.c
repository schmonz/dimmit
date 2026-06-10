#include "command.h"

#include <string.h>
#include <unistd.h>

int parse_command(const char *cmd) {
    if (strcmp(cmd, "up") == 0) return STEP;
    if (strcmp(cmd, "down") == 0) return -STEP;
    return 0;
}

int read_command(int fd) {
    char buf[16];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;

    buf[n] = '\0';
    if (buf[n-1] == '\n') buf[n-1] = '\0';

    return parse_command(buf);
}

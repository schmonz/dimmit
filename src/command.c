#include "command.h"

#include <string.h>

int parse_command(const char *cmd) {
    if (strcmp(cmd, "up") == 0) return STEP;
    if (strcmp(cmd, "down") == 0) return -STEP;
    return 0;
}

int read_command(dimmit_sock_t fd) {
    char buf[16];
    int n = (int)recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return 0;

    buf[n] = '\0';
    if (buf[n-1] == '\n') buf[n-1] = '\0';

    return parse_command(buf);
}

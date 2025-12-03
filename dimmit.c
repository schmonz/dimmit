#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "config.h"

static const char* get_sock_path(void) {
    const char *path = getenv("DIMMIT_SOCK");
    return path ? path : DIMMIT_SOCK_DEFAULT;
}

int main(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s (no arguments)\n", argv[0]);
        return 1;
    }

    // Determine desired action from the invoked program basename
    const char *prog = strrchr(argv[0], '/');
    prog = prog ? prog + 1 : argv[0];

    const char *msg = NULL;
    if (strcmp(prog, "dimmit-up") == 0) {
        msg = "up";
    } else if (strcmp(prog, "dimmit-down") == 0) {
        msg = "down";
    } else {
        fprintf(stderr, "%s: unknown invocation name (expected 'dimmit-up' or 'dimmit-down')\n", prog);
        return 1;
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, get_sock_path(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return 1;
    }
    size_t len = strlen(msg);
    if (write(sock, msg, len) < 0) perror("write");
    if (len == 0 || msg[len-1] != '\n') write(sock, "\n", 1);

    close(sock);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/compat/net.h"
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

    // Determine desired action from the invoked program basename. Handle both
    // path separators ('/' on POSIX, '\\' on Windows) and a trailing ".exe".
    const char *base = argv[0];
    for (const char *p = argv[0]; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    char prog[64];
    strncpy(prog, base, sizeof(prog) - 1);
    prog[sizeof(prog) - 1] = '\0';
    size_t plen = strlen(prog);
    if (plen >= 4 && strcmp(prog + plen - 4, ".exe") == 0) prog[plen - 4] = '\0';

    const char *msg = NULL;
    if (strcmp(prog, "dimmit-up") == 0) {
        msg = "up";
    } else if (strcmp(prog, "dimmit-down") == 0) {
        msg = "down";
    } else {
        fprintf(stderr, "%s: unknown invocation name (expected 'dimmit-up' or 'dimmit-down')\n", prog);
        return 1;
    }

    if (net_startup() != 0) { fprintf(stderr, "net_startup failed\n"); return 1; }

    dimmit_sock_t sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == DIMMIT_BAD_SOCK) { perror("socket"); net_cleanup(); return 1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, get_sock_path(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); net_close(sock); net_cleanup(); return 1;
    }
    int len = (int)strlen(msg);
    if (send(sock, msg, len, 0) < 0) perror("send");
    if ((len == 0 || msg[len-1] != '\n') && send(sock, "\n", 1, 0) < 0) perror("send");

    net_close(sock);
    net_cleanup();
    return 0;
}

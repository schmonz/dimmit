#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include "platform/compat/net.h"
#ifdef _WIN32
  #include <windows.h>
  #include <io.h>      /* _unlink */
  #define unlink _unlink
#else
  #include <unistd.h>
  #include <signal.h>
  #include <sys/stat.h>
  #include <sys/time.h>
#endif

#include "platform/ddc/abstraction.h"
#include "platform/access-control/access-control.h"
#include "platform/logging/logging.h"
#include "platform/input/input.h"
#include "dimmer.h"
#include "command.h"
#include "config.h"

#define ACCEPT_BACKLOG 5

/* The worker blocks on the condvar until a press signals it; this timeout is
 * only a periodic wakeup so shutdown (running=0) is noticed promptly. It does
 * not gate brightness latency -- presses wake the worker immediately. */
#define WORKER_POLL_MS 250

static const char* get_sock_path(void) {
    const char *path = getenv("DIMMIT_SOCK");
    return path ? path : DIMMIT_SOCK_DEFAULT;
}

static volatile int running = 1;
static ddc_handle_t *ddc = NULL;

/* All brightness/debounce state lives in the dimmer state machine (dimmer.c).
 * The mutex guards it against the worker thread; the slow DDC write happens
 * with the lock released. */
static dimmer_t dimmer;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

#ifdef _WIN32
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    (void)ctrl_type;   /* Ctrl-C/close/logoff/shutdown all mean: stop. */
    running = 0;
    return TRUE;
}
#else
static void sighandler(int sig) {
    (void)sig;
    running = 0;
}
#endif

static int init_monitor(void) {
    dimmer_init(&dimmer, 50, 100);  /* sane defaults until the display answers */

    ddc = ddc_open_display();
    if (!ddc) {
        fprintf(stderr, "Failed to open display\n");
        return -1;
    }

    int cur, max;
    if (ddc_get_brightness(ddc, &cur, &max) == 0) {
        dimmer_init(&dimmer, cur, max);
        printf("Monitor initialized: current=%d, max=%d\n", cur, max);
    } else {
        fprintf(stderr, "Warning: couldn't read initial brightness\n");
    }

    return 0;
}

/* Perform the (slow) DDC write with the lock released. Returns 0 on success.
 * The caller commits the new value into the dimmer under the lock so that
 * state is never written outside the mutex. */
static int do_set_brightness(int value) {
    if (ddc_set_brightness(ddc, value) == 0) {
        printf("Brightness: %d\n", value);
        return 0;
    }
    fprintf(stderr, "Failed to set brightness\n");
    return -1;
}

static void* brightness_worker(void* arg) {
    (void)arg;

    pthread_mutex_lock(&lock);
    while (running) {
        int target;
        if (dimmer_due(&dimmer, &target)) {
            /* Write with the lock released (it's slow). Presses that land during
             * the write accumulate into pending_delta; the loop drains them on
             * the next iteration before ever going back to wait. */
            pthread_mutex_unlock(&lock);
            int ok = do_set_brightness(target);
            pthread_mutex_lock(&lock);
            if (ok == 0)
                dimmer_commit(&dimmer, target);  /* keeps deltas that arrived mid-write */
            else
                dimmer_settled(&dimmer);         /* abandon the batch on failure */
            continue;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += WORKER_POLL_MS * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&cond, &lock, &ts);
    }
    pthread_mutex_unlock(&lock);

    return NULL;
}

static void adjust_brightness(int delta) {
    pthread_mutex_lock(&lock);
    dimmer_adjust(&dimmer, delta);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
}

/* Apply a signed fraction of the display's range -- the input subsystem's
 * platform-convention step (e.g. macOS 1/16). Matches input_adjust_fn. */
static void adjust_fraction(double frac) {
    pthread_mutex_lock(&lock);
    int max = dimmer_max(&dimmer);
    pthread_mutex_unlock(&lock);
    adjust_brightness(dimmer_delta_for_fraction(max, frac));
}

int main(void) {
    dimmit_sock_t sock = DIMMIT_BAD_SOCK, client;
    struct sockaddr_un addr;
    pthread_t worker;
    int worker_started = 0;
    int bound = 0;
    const char *sock_path = get_sock_path();

    if (logging_init() < 0) {
        fprintf(stderr, "Warning: logging not redirected; continuing\n");
    }

#ifdef _WIN32
    if (net_startup() != 0) {
        fprintf(stderr, "net_startup failed\n");
        return 1;
    }
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
#endif

    if (access_control_before_bind() < 0) {
        return 1;
    }

    if (init_monitor() < 0) {
        return 1;
    }

    if (pthread_create(&worker, NULL, brightness_worker, NULL) != 0) {
        perror("pthread_create");
        goto cleanup;
    }
    worker_started = 1;

    /* Optional in-process brightness-key capture (macOS HID); non-fatal -- the
     * socket clients remain the input if it's unsupported or denied. */
    input_start(adjust_fraction);

    unlink(sock_path);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == DIMMIT_BAD_SOCK) {
        perror("socket");
        goto cleanup;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        goto cleanup;
    }
    bound = 1;

    if (listen(sock, ACCEPT_BACKLOG) < 0) {
        perror("listen");
        goto cleanup;
    }

    /* Expose the socket to the intended clients per platform policy (e.g. on
     * Linux, hand it to the i2c group). Non-fatal: a failure here only affects
     * who can connect, not whether the daemon runs. */
    if (access_control_after_bind(sock_path) < 0) {
        fprintf(stderr, "Warning: could not apply socket access policy\n");
    }

    printf("Listening on %s\n", sock_path);

    while (running) {
        fd_set fds;
        struct timeval tv = {1, 0};

        FD_ZERO(&fds);
        FD_SET(sock, &fds);

        if (select(sock + 1, &fds, NULL, NULL, &tv) <= 0)
            continue;

        client = accept(sock, NULL, NULL);
        if (client == DIMMIT_BAD_SOCK) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            perror("accept");
            break;
        }

        if (!access_control_is_authorized((int)client)) {
            fprintf(stderr, "Access denied\n");
            net_close(client);
            continue;
        }

        int delta = read_command(client);
        if (delta != 0) {
            adjust_brightness(delta);
        } else {
            fprintf(stderr, "Ignoring empty or unknown command\n");
        }

        net_close(client);
    }

cleanup:
    /* Reached on normal shutdown and on every error path. Stop and join the
     * worker if it was started, then release the socket and display. */
    running = 0;
    input_stop();
    if (worker_started) {
        pthread_cond_signal(&cond);
        pthread_join(worker, NULL);
    }
    if (sock != DIMMIT_BAD_SOCK) net_close(sock);
    if (bound) unlink(sock_path);
    if (ddc) {
        ddc_close_display(ddc);
    }
    net_cleanup();

    return 0;
}

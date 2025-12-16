#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>

#include "ddc.h"
#include "config.h"

#define STEP 5
#define DEBOUNCE_MS 200

static const char* get_sock_path(void) {
    const char *path = getenv("DIMMIT_SOCK");
    return path ? path : DIMMIT_SOCK_DEFAULT;
}

static volatile int running = 1;
static int current_brightness = 50;
static int max_brightness = 100;
static ddc_handle_t *ddc = NULL;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int pending_delta = 0;
static struct timeval last_cmd;

void sighandler(int sig) {
    running = 0;
}

int init_monitor(void) {
    ddc = ddc_open_display();
    if (!ddc) {
        fprintf(stderr, "Failed to open display\n");
        return -1;
    }
    
    int cur, max;
    if (ddc_get_brightness(ddc, &cur, &max) == 0) {
        current_brightness = cur;
        max_brightness = max;
        printf("Monitor initialized: current=%d, max=%d\n", cur, max);
    } else {
        fprintf(stderr, "Warning: couldn't read initial brightness\n");
    }
    
    return 0;
}

void do_set_brightness(int value) {
    if (ddc_set_brightness(ddc, value) == 0) {
        current_brightness = value;
        printf("Brightness: %d\n", value);
    } else {
        fprintf(stderr, "Failed to set brightness\n");
    }
}

void* brightness_worker(void* arg) {
    struct timespec ts;
    
    while (running) {
        pthread_mutex_lock(&lock);
        
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += DEBOUNCE_MS * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        
        pthread_cond_timedwait(&cond, &lock, &ts);
        
        if (pending_delta != 0) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed = (now.tv_sec - last_cmd.tv_sec) * 1000 +
                          (now.tv_usec - last_cmd.tv_usec) / 1000;
            
            if (elapsed >= DEBOUNCE_MS) {
                int new_val = current_brightness + pending_delta;
                
                if (new_val < 0) new_val = 0;
                if (new_val > max_brightness) new_val = max_brightness;
                
                if (new_val != current_brightness) {
                    pthread_mutex_unlock(&lock);
                    do_set_brightness(new_val);
                    pthread_mutex_lock(&lock);
                }
                
                pending_delta = 0;
            }
        }
        
        pthread_mutex_unlock(&lock);
    }
    
    return NULL;
}

void adjust_brightness(int delta) {
    pthread_mutex_lock(&lock);
    
    gettimeofday(&last_cmd, NULL);
    
    int new_pending = pending_delta + delta;
    int projected = current_brightness + new_pending;
    
    if ((projected < 0 && delta < 0) ||
        (projected > max_brightness && delta > 0)) {
        pthread_mutex_unlock(&lock);
        return;
    }
    
    pending_delta = new_pending;
    pthread_cond_signal(&cond);
    
    pthread_mutex_unlock(&lock);
}

int main(void) {
    int sock, client;
    struct sockaddr_un addr;
    char buf[16];
    ssize_t n;
    pthread_t worker;
    
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    
    if (geteuid() != 0) {
        fprintf(stderr, "Warning: not running as root, DDC access may fail\n");
    }
    
    if (init_monitor() < 0) {
        return 1;
    }
    
    /* NOTE: We stay root because DDC implementations may reopen devices on writes.
     * If the backend keeps fds open, we could drop to 'nobody' here safely.
     * Tested behavior: TBD - if writes work after dropping privs, uncomment below.
     *
     * if (geteuid() == 0) {
     *     struct passwd *nobody = getpwnam("nobody");
     *     if (nobody && setgid(nobody->pw_gid) == 0 && setuid(nobody->pw_uid) == 0) {
     *         printf("Dropped privileges to nobody\n");
     *     }
     * }
     */
    
    if (pthread_create(&worker, NULL, brightness_worker, NULL) != 0) {
        perror("pthread_create");
        goto cleanup;
    }
    
    const char *sock_path = get_sock_path();
    unlink(sock_path);
    
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        goto cleanup;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        goto cleanup;
    }
    
    if (listen(sock, 5) < 0) {
        perror("listen");
        close(sock);
        goto cleanup;
    }
    
    struct group *i2c_grp = getgrnam("i2c");
    if (i2c_grp) {
        if (chown(sock_path, 0, i2c_grp->gr_gid) < 0) {
            perror("chown");
        }
    }
    chmod(sock_path, 0660);
    
    printf("Listening on %s\n", sock_path);
    
    while (running) {
        fd_set fds;
        struct timeval tv = {1, 0};
        
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        
        if (select(sock + 1, &fds, NULL, NULL, &tv) <= 0)
            continue;
        
        client = accept(sock, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        
        if (!ddc_is_authorized(client)) {
            fprintf(stderr, "Access denied\n");
            close(client);
            continue;
        }
        
        n = read(client, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            if (buf[n-1] == '\n') buf[n-1] = '\0';
            
            if (strcmp(buf, "up") == 0) {
                adjust_brightness(STEP);
            } else if (strcmp(buf, "down") == 0) {
                adjust_brightness(-STEP);
            } else {
                fprintf(stderr, "Unknown command: %s\n", buf);
            }
        }
        
        close(client);
    }
    
    pthread_cond_signal(&cond);
    pthread_join(worker, NULL);
    
    close(sock);
    unlink(sock_path);
    
cleanup:
    if (ddc) {
        ddc_close_display(ddc);
    }
    
    return 0;
}

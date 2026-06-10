#ifndef DIMMER_H
#define DIMMER_H

#include <sys/time.h>

/* Pure brightness/debounce state machine. No sockets, threads, stdio, or DDC:
 * the daemon owns those and drives this with an externally supplied clock
 * (every call that needs the time takes a `struct timeval now`), so the logic
 * is deterministically testable without real sleeps or the worker thread. */

#define DIMMER_DEBOUNCE_MS 200

typedef struct {
    int current;             /* last-applied brightness */
    int max;                 /* display maximum */
    int pending_delta;       /* accumulated, clamp-projected, not yet applied */
    struct timeval last_cmd; /* time of the most recent adjust */
} dimmer_t;

/* Initialize with the display's current/max brightness and no pending change. */
void dimmer_init(dimmer_t *d, int current, int max);

/* Record a command at time `now`: accumulate `delta` (clamp-projected into
 * [0, max] so holding a key can't run past a boundary) and stamp last_cmd. */
void dimmer_adjust(dimmer_t *d, int delta, struct timeval now);

/* Pure: is a write due? Returns 1 and sets *target_out to the clamped target
 * when the debounce window has elapsed since last_cmd, a delta is pending, and
 * the target differs from current; otherwise returns 0. Does not mutate. */
int dimmer_due(const dimmer_t *d, struct timeval now, int *target_out);

/* Record a successfully applied brightness (call only after a successful write). */
void dimmer_commit(dimmer_t *d, int applied);

/* Clear the consumed pending delta. Called after every due cycle, whether or
 * not the write succeeded -- matching the daemon's existing semantics. */
void dimmer_settled(dimmer_t *d);

#endif /* DIMMER_H */

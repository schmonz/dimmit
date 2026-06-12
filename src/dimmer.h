#ifndef DIMMER_H
#define DIMMER_H

/* Pure brightness state machine. No sockets, threads, stdio, DDC, or clock:
 * the daemon owns those and drives this, so the logic is deterministically
 * testable without real sleeps or the worker thread.
 *
 * There is no time-based debounce. The daemon's single worker writes to DDC
 * synchronously (a slow, self-rate-limiting bus), so presses that arrive while
 * a write is in flight accumulate into pending_delta and are coalesced into the
 * next write -- leading-edge response with natural backpressure, no timer. */

typedef struct {
    int current;             /* last-applied brightness */
    int max;                 /* display maximum */
    int pending_delta;       /* accumulated, clamp-projected, not yet applied */
} dimmer_t;

/* Initialize with the display's current/max brightness and no pending change. */
void dimmer_init(dimmer_t *d, int current, int max);

/* Accumulate `delta` into the pending change (clamp-projected into [0, max] so
 * holding a key can't run past a boundary). */
void dimmer_adjust(dimmer_t *d, int delta);

/* Pure: is a write due? Returns 1 and sets *target_out to the clamped target
 * when a delta is pending and the target differs from current; else 0. Does
 * not mutate. */
int dimmer_due(const dimmer_t *d, int *target_out);

/* Record a successfully applied brightness (call only after a successful write).
 * Subtracts just the applied step from pending_delta, so any presses that landed
 * during the write are preserved for the next cycle. */
void dimmer_commit(dimmer_t *d, int applied);

/* Drop the pending delta without applying it. Called when a write fails, to
 * abandon the batch rather than retry forever. */
void dimmer_settled(dimmer_t *d);

/* The display's maximum brightness (for input backends that step by a fraction
 * of the range). */
int dimmer_max(const dimmer_t *d);

/* Convert a signed fraction of the full range into an integer delta, rounding
 * to nearest; a nonzero fraction never collapses to a no-op (0) step. */
int dimmer_delta_for_fraction(int max, double fraction);

#endif /* DIMMER_H */

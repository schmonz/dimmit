#include "dimmer.h"

#include <math.h>

/* Clamp a brightness value into [0, max]. */
static int clamp_brightness(int value, int max) {
    if (value < 0) return 0;
    if (value > max) return max;
    return value;
}

/* Whole milliseconds elapsed from a to b. */
static long elapsed_ms(struct timeval a, struct timeval b) {
    return (b.tv_sec - a.tv_sec) * 1000 + (b.tv_usec - a.tv_usec) / 1000;
}

void dimmer_init(dimmer_t *d, int current, int max) {
    d->current = current;
    d->max = max;
    d->pending_delta = 0;
    d->last_cmd.tv_sec = 0;
    d->last_cmd.tv_usec = 0;
}

void dimmer_adjust(dimmer_t *d, int delta, struct timeval now) {
    d->last_cmd = now;

    /* Clamp the pending target into [0, max] so a step that would overshoot a
     * boundary still moves to the boundary (e.g. 3 - 5 -> 0, 98 + 5 -> 100)
     * rather than being rejected, and so holding a key can't accumulate a
     * runaway delta past the limits. */
    int projected = clamp_brightness(d->current + d->pending_delta + delta, d->max);
    d->pending_delta = projected - d->current;
}

int dimmer_due(const dimmer_t *d, struct timeval now, int *target_out) {
    if (elapsed_ms(d->last_cmd, now) < DIMMER_DEBOUNCE_MS) return 0;
    if (d->pending_delta == 0) return 0;

    int target = clamp_brightness(d->current + d->pending_delta, d->max);
    if (target == d->current) return 0;

    *target_out = target;
    return 1;
}

void dimmer_commit(dimmer_t *d, int applied) {
    d->current = applied;
}

void dimmer_settled(dimmer_t *d) {
    d->pending_delta = 0;
}

int dimmer_max(const dimmer_t *d) {
    return d->max;
}

int dimmer_delta_for_fraction(int max, double fraction) {
    int delta = (int)lround((double)max * fraction);
    /* Never let a nonzero fraction collapse to a no-op step. */
    if (delta == 0) delta = (fraction > 0) ? 1 : (fraction < 0 ? -1 : 0);
    return delta;
}

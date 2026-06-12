#include "dimmer.h"

#include <math.h>

/* Clamp a brightness value into [0, max]. */
static int clamp_brightness(int value, int max) {
    if (value < 0) return 0;
    if (value > max) return max;
    return value;
}

void dimmer_init(dimmer_t *d, int current, int max) {
    d->current = current;
    d->max = max;
    d->pending_delta = 0;
}

void dimmer_adjust(dimmer_t *d, int delta) {
    /* Clamp the pending target into [0, max] so a step that would overshoot a
     * boundary still moves to the boundary (e.g. 3 - 5 -> 0, 98 + 5 -> 100)
     * rather than being rejected, and so holding a key can't accumulate a
     * runaway delta past the limits. */
    int projected = clamp_brightness(d->current + d->pending_delta + delta, d->max);
    d->pending_delta = projected - d->current;
}

int dimmer_due(const dimmer_t *d, int *target_out) {
    if (d->pending_delta == 0) return 0;

    int target = clamp_brightness(d->current + d->pending_delta, d->max);
    if (target == d->current) return 0;

    *target_out = target;
    return 1;
}

void dimmer_commit(dimmer_t *d, int applied) {
    /* Subtract only what we actually applied; deltas accumulated during the
     * (slow, lock-released) write stay pending for the next cycle. */
    d->pending_delta -= (applied - d->current);
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

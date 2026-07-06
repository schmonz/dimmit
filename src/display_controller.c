#include "display_controller.h"
#include "dimmer.h"
#include <stdlib.h>
#include <string.h>

typedef struct { brightness_source src; dimmer_t dim; } managed_display;

struct display_controller {
    brightness_source *sources;   /* owned array from brightness_enumerate */
    int count;
    managed_display *displays;     /* parallel to sources */
};

display_controller *controller_open(void) {
    display_controller *c = (display_controller*)calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (brightness_enumerate(&c->sources, &c->count) != 0) { free(c); return NULL; }
    if (c->count > 0) {
        c->displays = (managed_display*)calloc((size_t)c->count, sizeof(managed_display));
        if (!c->displays) { brightness_free(c->sources, c->count); free(c); return NULL; }
        for (int i = 0; i < c->count; i++) {
            c->displays[i].src = c->sources[i];
            int cur = 0, max = 100;
            if (c->sources[i].ops->get(c->sources[i].ctx, &cur, &max) != 0) { cur = 0; max = 100; }
            dimmer_init(&c->displays[i].dim, cur, max);
        }
    }
    return c;
}

int controller_count(const display_controller *c) { return c ? c->count : 0; }

void controller_adjust(display_controller *c, double fraction) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        int delta = dimmer_delta_for_fraction(dimmer_max(&c->displays[i].dim), fraction);
        dimmer_adjust(&c->displays[i].dim, delta);
    }
}

int controller_service(display_controller *c) {
    if (!c) return 0;
    int applied = 0;
    for (int i = 0; i < c->count; i++) {
        int target = -1;
        if (!dimmer_due(&c->displays[i].dim, &target)) continue;
        if (c->displays[i].src.ops->set(c->displays[i].src.ctx, target) == 0) {
            dimmer_commit(&c->displays[i].dim, target);
            applied++;
        } else {
            dimmer_settled(&c->displays[i].dim);  /* isolate the failure, drop its batch */
        }
    }
    return applied;
}

void controller_reconcile(display_controller *c) {
    if (!c) return;
    brightness_source *fresh = NULL; int fresh_n = 0;
    if (brightness_enumerate(&fresh, &fresh_n) != 0) return;   /* keep current set on failure */

    managed_display *next = fresh_n > 0
        ? (managed_display*)calloc((size_t)fresh_n, sizeof(managed_display)) : NULL;
    if (fresh_n > 0 && !next) { brightness_free(fresh, fresh_n); return; }

    for (int i = 0; i < fresh_n; i++) {
        int matched = -1;
        for (int j = 0; j < c->count; j++) {
            if (strcmp(fresh[i].id, c->sources[j].id) == 0) { matched = j; break; }
        }
        next[i].src = fresh[i];
        if (matched >= 0) {
            next[i].dim = c->displays[matched].dim;   /* preserve level + pending */
        } else {
            int cur = 0, max = 100;
            if (fresh[i].ops->get(fresh[i].ctx, &cur, &max) != 0) { cur = 0; max = 100; }
            dimmer_init(&next[i].dim, cur, max);
        }
    }

    /* Close only the sources that did NOT survive into the new set. */
    for (int j = 0; j < c->count; j++) {
        int survived = 0;
        for (int i = 0; i < fresh_n; i++) if (strcmp(fresh[i].id, c->sources[j].id) == 0) { survived = 1; break; }
        if (!survived && c->sources[j].ops && c->sources[j].ops->close)
            c->sources[j].ops->close(c->sources[j].ctx);
    }
    free(c->sources);     /* free old array shell; surviving ctx now owned via `fresh` */
    free(c->displays);
    c->sources = fresh;   /* NOTE: fresh[i].ctx for a matched display is a *new* open;
                             the old matched ctx was closed above. */
    c->count = fresh_n;
    c->displays = next;
}

int controller_current(const display_controller *c, int i) {
    if (!c || i < 0 || i >= c->count) return -1;
    return c->displays[i].dim.current;
}

void controller_close(display_controller *c) {
    if (!c) return;
    brightness_free(c->sources, c->count);  /* calls ops->close on each */
    free(c->displays);
    free(c);
}

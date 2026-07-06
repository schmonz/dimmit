#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H

/* A generic, technology-independent handle to one controllable display's
 * brightness. Providers (DDC today; sysfs/WMI in Phase 2) each enumerate zero or
 * more of these; the controller treats them uniformly. Brightness is a plain
 * integer in [0, max] -- no VCP here (that lives inside the DDC provider). */

typedef struct {
    int  (*get)(void *ctx, int *current, int *max); /* 0 on success */
    int  (*set)(void *ctx, int value);              /* 0 on success */
    void (*close)(void *ctx);                        /* release ctx */
} brightness_ops;

typedef struct {
    const brightness_ops *ops;
    void *ctx;          /* provider-private per-display state */
    char  id[64];       /* stable-ish key for reconcile (provisional in Phase 1) */
    char  label[64];    /* human-readable, for logs */
} brightness_source;

/* Enumerate every controllable display across all registered providers.
 * Allocates *out (array of *count sources); free with brightness_free.
 * Returns 0 on success (count may be 0), -1 on allocation failure. */
int  brightness_enumerate(brightness_source **out, int *count);

/* Close every source (calls ops->close on each ctx) and free the array. */
void brightness_free(brightness_source *sources, int count);

#endif /* BRIGHTNESS_H */

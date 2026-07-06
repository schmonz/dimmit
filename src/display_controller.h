#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "brightness.h"

/* Owns the live set of controllable displays, one dimmer per display, and applies
 * a relative fraction step to all of them (each by that fraction of its own max,
 * so offsets between displays are preserved). Thread-agnostic: dimmitd owns the
 * mutex/worker and calls controller_service() to perform the (slow) writes. */

typedef struct display_controller display_controller;

/* Enumerate + open every controllable display and read its current brightness.
 * Returns NULL only on allocation failure; a valid controller with 0 displays is
 * fine (a monitor may appear later). */
display_controller *controller_open(void);

int  controller_count(const display_controller *c);

/* Fan a relative step out to every display: for display d,
 * dimmer_adjust(dimmer_delta_for_fraction(d.max, fraction)). */
void controller_adjust(display_controller *c, double fraction);

/* Apply every due write (dimmer_due -> source set -> dimmer_commit, or
 * dimmer_settled on failure). Returns the number of displays written. */
int  controller_service(display_controller *c);

/* Re-enumerate the display set. Displays whose id still matches keep their dimmer
 * (and level); new displays are opened and initialized from their own current;
 * vanished displays are closed and dropped. Phase 1 calls this on a timer; Phase 3
 * replaces the trigger with per-platform display-change events. */
void controller_reconcile(display_controller *c);

/* Test accessor: last-applied brightness of display i, or -1 if out of range. */
int  controller_current(const display_controller *c, int i);

void controller_close(display_controller *c);

#endif /* DISPLAY_CONTROLLER_H */

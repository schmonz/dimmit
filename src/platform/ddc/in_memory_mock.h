#ifndef DDC_IN_MEMORY_MOCK_H
#define DDC_IN_MEMORY_MOCK_H

/* Test-only controls for the in-memory mock DDC backend. Let a test stand up an
 * arbitrary set of simulated displays and inject a write failure on one of them. */

#define MOCK_MAX_DISPLAYS 8

/* Configure `n` displays with the given per-display current/max. Clears any
 * previously injected failures. Safe to call repeatedly between tests. */
void mock_reset(int n, const int *currents, const int *maxes);

/* Make ddc_implementation_set_* fail (return DDC_ERROR) for display `index`. */
void mock_set_fail(int index, int fail);

/* Read the simulated current brightness of display `index` (-1 if out of range). */
int  mock_current(int index);

#endif /* DDC_IN_MEMORY_MOCK_H */

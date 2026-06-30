#include "platform/input/input.h"

/* No in-process key capture on Windows yet -- the socket clients are the input.
 * (Most desktop keyboards lack brightness keys; capturing them via a low-level
 * keyboard hook is future work.) */
int  input_start(input_adjust_fn on_adjust) { (void)on_adjust; return -1; }
void input_stop(void) {}

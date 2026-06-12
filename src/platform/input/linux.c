#include "platform/input/input.h"

/* No in-process key capture on Linux yet (root daemon vs session, and Wayland
 * forbids global grab) -- the socket clients are the input. */
int  input_start(input_adjust_fn on_adjust) { (void)on_adjust; return -1; }
void input_stop(void) {}

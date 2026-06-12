#include "platform/input/input.h"

/* No in-process key capture on NetBSD yet -- the socket clients are the input. */
int  input_start(input_adjust_fn on_adjust) { (void)on_adjust; return -1; }
void input_stop(void) {}

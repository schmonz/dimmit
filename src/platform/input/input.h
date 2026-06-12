#ifndef DIMMIT_PLATFORM_INPUT_H
#define DIMMIT_PLATFORM_INPUT_H

/* Optional in-process capture of the platform's brightness keys. on_adjust
 * receives a SIGNED FRACTION of the display's full range; the platform backend
 * chooses the fraction per that platform's key convention (the daemon turns it
 * into a delta). Backends that can't capture (or aren't permitted to) are
 * no-ops -- the socket clients remain the input. */
typedef void (*input_adjust_fn)(double fraction);

int  input_start(input_adjust_fn on_adjust); /* 0 = capturing; <0 = unsupported/denied */
void input_stop(void);

#endif /* DIMMIT_PLATFORM_INPUT_H */

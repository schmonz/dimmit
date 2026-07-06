#ifndef DDC_ABSTRACTION_H
#define DDC_ABSTRACTION_H

#include "brightness.h"   /* brightness_source */

/* Enumerate every controllable DDC display (non-built-in and answering an initial
 * brightness read) as generic brightness sources. Contract matches
 * brightness_enumerate(). VCP packing lives in abstraction.c. */
int ddc_enumerate_sources(brightness_source **out, int *count);

/* VCP (VESA Control Panel) Feature Codes */
#define VCP_BRIGHTNESS 0x10
#define VCP_CONTRAST 0x12
#define VCP_POWER_MODE 0xD6

#endif

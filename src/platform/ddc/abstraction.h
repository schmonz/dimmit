#ifndef DDC_ABSTRACTION_H
#define DDC_ABSTRACTION_H

typedef struct ddc_handle ddc_handle_t;

/* Open first DDC-capable display, return NULL on failure */
ddc_handle_t* ddc_open_display(void);

/* Get current and max brightness, return 0 on success */
int ddc_get_brightness(ddc_handle_t *handle, int *current, int *max);

/* Set brightness, return 0 on success */
int ddc_set_brightness(ddc_handle_t *handle, int value);

/* Close display handle */
void ddc_close_display(ddc_handle_t *handle);

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

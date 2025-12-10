#ifndef DDC_H
#define DDC_H

typedef struct ddc_handle ddc_handle_t;

/* Open first DDC-capable display, return NULL on failure */
ddc_handle_t* ddc_open_display(void);

/* Check if connecting client is authorized, return 1 if yes, 0 if no */
int ddc_is_authorized(int client_fd);

/* Get current and max brightness, return 0 on success */
int ddc_get_brightness(ddc_handle_t *handle, int *current, int *max);

/* Set brightness, return 0 on success */
int ddc_set_brightness(ddc_handle_t *handle, int value);

/* Close display handle */
void ddc_close_display(ddc_handle_t *handle);

/* VCP (VESA Control Panel) Feature Codes */
#define VCP_BRIGHTNESS 0x10
#define VCP_CONTRAST 0x12
#define VCP_POWER_MODE 0xD6

/* DDC/CI I2C Addresses */
#define DDC_ADDR 0x6E
#define DDC_REPLY_ADDR 0x6F

#endif

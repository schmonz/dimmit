#ifndef DDCUTIL_MACOS_H
#define DDCUTIL_MACOS_H

#include <stdint.h>

/* Status codes matching libddcutil convention */
typedef int DDCM_Status;
#define DDCM_OK 0
#define DDCM_ERROR -1

/* Display handle - opaque type */
typedef struct DDCM_Display_Handle_s *DDCM_Display_Handle;

/* Display reference - opaque type */
typedef struct DDCM_Display_Ref_s *DDCM_Display_Ref;

/* Display info for enumeration */
typedef struct {
    DDCM_Display_Ref dref;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
} DDCM_Display_Info;

/* Display info list */
typedef struct {
    int ct;  /* count */
    DDCM_Display_Info *info;
} DDCM_Display_Info_List;

/* VCP (Virtual Control Panel) value structure */
typedef struct {
    uint8_t mh;  /* max high byte */
    uint8_t ml;  /* max low byte */
    uint8_t sh;  /* current (set) high byte */
    uint8_t sl;  /* current (set) low byte */
} DDCM_Non_Table_Vcp_Value;

/* Get list of detected displays */
DDCM_Status ddcm_get_display_info_list2(int flags, DDCM_Display_Info_List **list_out);

/* Free display info list */
void ddcm_free_display_info_list(DDCM_Display_Info_List *list);

/* Open a display for DDC communication */
DDCM_Status ddcm_open_display2(DDCM_Display_Ref dref, int flags, DDCM_Display_Handle *handle_out);

/* Close display handle */
DDCM_Status ddcm_close_display(DDCM_Display_Handle handle);

/* Get VCP feature value (non-table) */
DDCM_Status ddcm_get_non_table_vcp_value(DDCM_Display_Handle handle, uint8_t feature_code, DDCM_Non_Table_Vcp_Value *value_out);

/* Set VCP feature value (non-table) */
DDCM_Status ddcm_set_non_table_vcp_value(DDCM_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte);

#endif /* DDCUTIL_MACOS_H */

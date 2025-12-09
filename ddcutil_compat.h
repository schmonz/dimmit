#ifndef DDCUTIL_COMPAT_H
#define DDCUTIL_COMPAT_H

#include <stdint.h>

/* Status codes matching libddcutil convention */
typedef int DDC_Status;
#define DDC_OK 0
#define DDC_ERROR -1

/* Display handle - opaque type */
typedef struct DDC_Display_Handle_s *DDC_Display_Handle;

/* Display reference - opaque type */
typedef struct DDC_Display_Ref_s *DDC_Display_Ref;

/* Display info for enumeration */
typedef struct {
    DDC_Display_Ref dref;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
} DDC_Display_Info;

/* Display info list */
typedef struct {
    int ct;  /* count */
    DDC_Display_Info *info;
} DDC_Display_Info_List;

/* VCP (Virtual Control Panel) value structure */
typedef struct {
    uint8_t mh;  /* max high byte */
    uint8_t ml;  /* max low byte */
    uint8_t sh;  /* current (set) high byte */
    uint8_t sl;  /* current (set) low byte */
} DDC_Non_Table_Vcp_Value;

/* Get list of detected displays */
DDC_Status ddc_get_display_info_list2(int flags, DDC_Display_Info_List **list_out);

/* Free display info list */
void ddc_free_display_info_list(DDC_Display_Info_List *list);

/* Open a display for DDC communication */
DDC_Status ddc_open_display2(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out);

/* Close display handle */
DDC_Status ddc_close_display(DDC_Display_Handle handle);

/* Get VCP feature value (non-table) */
DDC_Status ddc_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out);

/* Set VCP feature value (non-table) */
DDC_Status ddc_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte);

#endif /* DDCUTIL_COMPAT_H */

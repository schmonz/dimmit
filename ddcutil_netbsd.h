#ifndef DDCUTIL_NETBSD_H
#define DDCUTIL_NETBSD_H

#include <stdint.h>

/* Status codes matching libddcutil convention */
typedef int DDCN_Status;
#define DDCN_OK 0
#define DDCN_ERROR -1

/* Display handle - opaque type */
typedef struct DDCN_Display_Handle_s *DDCN_Display_Handle;

/* Display reference - opaque type */
typedef struct DDCN_Display_Ref_s *DDCN_Display_Ref;

/* Display info for enumeration */
typedef struct {
    DDCN_Display_Ref dref;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
} DDCN_Display_Info;

/* Display info list */
typedef struct {
    int ct;  /* count */
    DDCN_Display_Info *info;
} DDCN_Display_Info_List;

/* VCP (Virtual Control Panel) value structure */
typedef struct {
    uint8_t mh;  /* max high byte */
    uint8_t ml;  /* max low byte */
    uint8_t sh;  /* current (set) high byte */
    uint8_t sl;  /* current (set) low byte */
} DDCN_Non_Table_Vcp_Value;

/* Get list of detected displays */
DDCN_Status ddcn_get_display_info_list2(int flags, DDCN_Display_Info_List **list_out);

/* Free display info list */
void ddcn_free_display_info_list(DDCN_Display_Info_List *list);

/* Open a display for DDC communication */
DDCN_Status ddcn_open_display2(DDCN_Display_Ref dref, int flags, DDCN_Display_Handle *handle_out);

/* Close display handle */
DDCN_Status ddcn_close_display(DDCN_Display_Handle handle);

/* Get VCP feature value (non-table) */
DDCN_Status ddcn_get_non_table_vcp_value(DDCN_Display_Handle handle, uint8_t feature_code, DDCN_Non_Table_Vcp_Value *value_out);

/* Set VCP feature value (non-table) */
DDCN_Status ddcn_set_non_table_vcp_value(DDCN_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte);

#endif /* DDCUTIL_NETBSD_H */

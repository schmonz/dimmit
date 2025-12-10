#ifndef DDC_IMPL_H
#define DDC_IMPL_H

#include <stdint.h>

typedef struct DDC_Display_Ref_s *DDC_Display_Ref;
typedef struct DDC_Display_Handle_s *DDC_Display_Handle;

typedef int DDC_Status;
#ifndef DDC_OK
#define DDC_OK 0
#endif
#ifndef DDC_ERROR
#define DDC_ERROR -1
#endif

typedef struct {
    DDC_Display_Ref dref;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
} DDC_Display_Info;

typedef struct {
    int ct;
    DDC_Display_Info *info;
} DDC_Display_Info_List;

typedef struct {
    uint8_t mh;
    uint8_t ml;
    uint8_t sh;
    uint8_t sl;
} DDC_Non_Table_Vcp_Value;

DDC_Status ddc_impl_get_display_info_list(int flags, DDC_Display_Info_List **list_out);
void ddc_impl_free_display_info_list(DDC_Display_Info_List *list);
DDC_Status ddc_impl_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out);
DDC_Status ddc_impl_close_display(DDC_Display_Handle handle);
DDC_Status ddc_impl_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out);
DDC_Status ddc_impl_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte);
int ddc_impl_is_authorized(int client_fd);

#endif /* DDC_IMPL_H */

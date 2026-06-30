#include "platform/ddc/implementation.h"
#include "platform/ddc/abstraction.h"

/* TEMPORARY STUB -- replaced in Task 7 with the dxva2 implementation. */
DDC_Status ddc_implementation_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    (void)flags; (void)list_out; return DDC_ERROR;
}
void ddc_implementation_free_display_info_list(DDC_Display_Info_List *list) { (void)list; }
DDC_Status ddc_implementation_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)dref; (void)flags; (void)handle_out; return DDC_ERROR;
}
DDC_Status ddc_implementation_close_display(DDC_Display_Handle handle) { (void)handle; return DDC_ERROR; }
DDC_Status ddc_implementation_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    (void)handle; (void)feature_code; (void)value_out; return DDC_ERROR;
}
DDC_Status ddc_implementation_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    (void)handle; (void)feature_code; (void)hi_byte; (void)lo_byte; return DDC_ERROR;
}

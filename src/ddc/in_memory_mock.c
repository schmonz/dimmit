/* In-memory mock DDC backend for unit tests.
 *
 * Implements the ddc/implementation.h interface against a simulated external display
 * (current=50, max=100) so the daemon logic and the ddc.c wrapper can be
 * exercised with no real hardware. Authorization is settable via the global
 * ddc_mock_authorized so tests can drive the accept/reject path. */
#include "ddc/implementation.h"
#include "ddc/abstraction.h"
#include <stdlib.h>

struct DDC_Display_Ref_s { int id; };
struct DDC_Display_Handle_s { int current; int max; };

int ddc_mock_authorized = 1;

static struct DDC_Display_Ref_s mock_ref = { 1 };

DDC_Status ddc_implementation_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    (void)flags;
    if (!list_out) return DDC_ERROR;
    DDC_Display_Info_List *list = (DDC_Display_Info_List*)malloc(sizeof(*list));
    if (!list) return DDC_ERROR;
    list->ct = 1;
    list->info = (DDC_Display_Info*)calloc(1, sizeof(DDC_Display_Info));
    if (!list->info) { free(list); return DDC_ERROR; }
    list->info[0].dref = (DDC_Display_Ref)&mock_ref;
    list->info[0].vendor_id = 0x1234;
    list->info[0].product_id = 0x5678;
    list->info[0].is_builtin = 0;
    *list_out = list;
    return DDC_OK;
}

void ddc_implementation_free_display_info_list(DDC_Display_Info_List *list) {
    if (!list) return;
    free(list->info);
    free(list);
}

DDC_Status ddc_implementation_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    if (!dref || !handle_out) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)malloc(sizeof(*h));
    if (!h) return DDC_ERROR;
    h->current = 50;
    h->max = 100;
    *handle_out = (DDC_Display_Handle)h;
    return DDC_OK;
}

DDC_Status ddc_implementation_close_display(DDC_Display_Handle handle) {
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_implementation_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    if (!h || !value_out) return DDC_ERROR;
    if (feature_code != VCP_BRIGHTNESS) return DDC_ERROR;
    value_out->mh = (uint8_t)((h->max >> 8) & 0xFF);
    value_out->ml = (uint8_t)(h->max & 0xFF);
    value_out->sh = (uint8_t)((h->current >> 8) & 0xFF);
    value_out->sl = (uint8_t)(h->current & 0xFF);
    return DDC_OK;
}

DDC_Status ddc_implementation_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    if (!h) return DDC_ERROR;
    if (feature_code != VCP_BRIGHTNESS) return DDC_ERROR;
    h->current = (hi_byte << 8) | lo_byte;
    return DDC_OK;
}

int ddc_implementation_is_authorized(int client_fd) {
    (void)client_fd;
    return ddc_mock_authorized;
}

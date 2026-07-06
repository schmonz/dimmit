/* In-memory mock DDC backend for unit tests: a configurable set of simulated
 * external displays. Implements platform/ddc/implementation.h with no hardware. */
#include "platform/ddc/implementation.h"
#include "platform/ddc/abstraction.h"
#include "platform/ddc/in_memory_mock.h"
#include <stdlib.h>

struct DDC_Display_Ref_s    { int index; };
struct DDC_Display_Handle_s { int index; };

static struct DDC_Display_Ref_s g_refs[MOCK_MAX_DISPLAYS];
static struct DDC_Display_Handle_s g_handles[MOCK_MAX_DISPLAYS];
static int g_current[MOCK_MAX_DISPLAYS];
static int g_max[MOCK_MAX_DISPLAYS];
static int g_fail[MOCK_MAX_DISPLAYS];
static int g_count = 1;   /* default: one display, matches historic behavior */

/* Default single display (current=50,max=100) so any test that doesn't call
 * mock_reset() sees the original mock. */
static void ensure_default(void) {
    static int inited = 0;
    if (inited) return;
    inited = 1;
    g_current[0] = 50; g_max[0] = 100; g_fail[0] = 0; g_count = 1;
}

void mock_reset(int n, const int *currents, const int *maxes) {
    if (n < 0) n = 0;
    if (n > MOCK_MAX_DISPLAYS) n = MOCK_MAX_DISPLAYS;
    g_count = n;
    for (int i = 0; i < n; i++) {
        g_current[i] = currents[i];
        g_max[i] = maxes[i];
        g_fail[i] = 0;
    }
}

void mock_set_fail(int index, int fail) {
    if (index >= 0 && index < MOCK_MAX_DISPLAYS) g_fail[index] = fail;
}

int mock_current(int index) {
    if (index < 0 || index >= g_count) return -1;
    return g_current[index];
}

DDC_Status ddc_implementation_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    (void)flags;
    ensure_default();
    if (!list_out || g_count == 0) return DDC_ERROR;
    DDC_Display_Info_List *list = (DDC_Display_Info_List*)malloc(sizeof(*list));
    if (!list) return DDC_ERROR;
    list->ct = g_count;
    list->info = (DDC_Display_Info*)calloc((size_t)g_count, sizeof(DDC_Display_Info));
    if (!list->info) { free(list); return DDC_ERROR; }
    for (int i = 0; i < g_count; i++) {
        g_refs[i].index = i;
        list->info[i].dref = (DDC_Display_Ref)&g_refs[i];
        list->info[i].vendor_id = 0x1234;
        list->info[i].product_id = (uint32_t)(0x5678 + i);
        list->info[i].is_builtin = 0;
    }
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
    struct DDC_Display_Ref_s *r = (struct DDC_Display_Ref_s*)dref;
    g_handles[r->index].index = r->index;
    *handle_out = (DDC_Display_Handle)&g_handles[r->index];
    return DDC_OK;
}

DDC_Status ddc_implementation_close_display(DDC_Display_Handle handle) {
    (void)handle;   /* handles are static; nothing to free */
    return DDC_OK;
}

DDC_Status ddc_implementation_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    if (!h || !value_out) return DDC_ERROR;
    if (feature_code != VCP_BRIGHTNESS) return DDC_ERROR;
    int i = h->index;
    value_out->mh = (uint8_t)((g_max[i] >> 8) & 0xFF);
    value_out->ml = (uint8_t)(g_max[i] & 0xFF);
    value_out->sh = (uint8_t)((g_current[i] >> 8) & 0xFF);
    value_out->sl = (uint8_t)(g_current[i] & 0xFF);
    return DDC_OK;
}

DDC_Status ddc_implementation_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    if (!h) return DDC_ERROR;
    if (feature_code != VCP_BRIGHTNESS) return DDC_ERROR;
    if (g_fail[h->index]) return DDC_ERROR;
    g_current[h->index] = (hi_byte << 8) | lo_byte;
    return DDC_OK;
}

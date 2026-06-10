#include "platform/ddc/abstraction.h"
#include "platform/ddc/implementation.h"
#include <stdlib.h>

struct ddc_handle { DDC_Display_Handle dh; };

ddc_handle_t* ddc_open_display(void) {
    DDC_Display_Info_List *dlist = NULL;
    if (ddc_implementation_get_display_info_list(0, &dlist) != DDC_OK || !dlist || dlist->ct == 0) {
        if (dlist) ddc_implementation_free_display_info_list(dlist);
        return NULL;
    }

    /* Prefer a non-built-in display if available (keeps macOS behavior); otherwise pick the first */
    DDC_Display_Ref dref = NULL;
    for (int i = 0; i < dlist->ct; i++) {
        if (!dlist->info[i].is_builtin) { dref = dlist->info[i].dref; break; }
    }
    if (!dref) dref = dlist->info[0].dref;

    ddc_handle_t *h = malloc(sizeof(*h));
    if (!h) {
        ddc_implementation_free_display_info_list(dlist);
        return NULL;
    }

    DDC_Display_Handle dh;
    if (ddc_implementation_open_display(dref, 0, &dh) != DDC_OK) {
        ddc_implementation_free_display_info_list(dlist);
        free(h);
        return NULL;
    }

    h->dh = dh;
    ddc_implementation_free_display_info_list(dlist);
    return h;
}

int ddc_get_brightness(ddc_handle_t *handle, int *current, int *max) {
    if (!handle || !current || !max) return -1;
    DDC_Non_Table_Vcp_Value v;
    if (ddc_implementation_get_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, &v) != DDC_OK) return -1;
    *current = (v.sh << 8) | v.sl;
    *max = (v.mh << 8) | v.ml;
    return 0;
}

int ddc_set_brightness(ddc_handle_t *handle, int value) {
    if (!handle) return -1;
    uint8_t hi = (value >> 8) & 0xFF;
    uint8_t lo = value & 0xFF;
    return ddc_implementation_set_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, hi, lo) == DDC_OK ? 0 : -1;
}

void ddc_close_display(ddc_handle_t *handle) {
    if (!handle) return;
    ddc_implementation_close_display(handle->dh);
    free(handle);
}

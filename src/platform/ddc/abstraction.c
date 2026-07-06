#include "platform/ddc/abstraction.h"
#include "platform/ddc/implementation.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ctx for a DDC-backed brightness source: the opened implementation handle. */
static int ddc_src_get(void *ctx, int *current, int *max) {
    DDC_Display_Handle h = (DDC_Display_Handle)ctx;
    DDC_Non_Table_Vcp_Value v;
    if (ddc_implementation_get_non_table_vcp_value(h, VCP_BRIGHTNESS, &v) != DDC_OK) return -1;
    *current = (v.sh << 8) | v.sl;
    *max     = (v.mh << 8) | v.ml;
    return 0;
}
static int ddc_src_set(void *ctx, int value) {
    DDC_Display_Handle h = (DDC_Display_Handle)ctx;
    uint8_t hi = (uint8_t)((value >> 8) & 0xFF), lo = (uint8_t)(value & 0xFF);
    return ddc_implementation_set_non_table_vcp_value(h, VCP_BRIGHTNESS, hi, lo) == DDC_OK ? 0 : -1;
}
static void ddc_src_close(void *ctx) {
    ddc_implementation_close_display((DDC_Display_Handle)ctx);
}
static const brightness_ops DDC_OPS = { ddc_src_get, ddc_src_set, ddc_src_close };

int ddc_enumerate_sources(brightness_source **out, int *count) {
    *out = NULL; *count = 0;
    DDC_Display_Info_List *dlist = NULL;
    if (ddc_implementation_get_display_info_list(0, &dlist) != DDC_OK || !dlist) return 0;

    brightness_source *arr = (brightness_source*)calloc((size_t)dlist->ct, sizeof(*arr));
    if (!arr) { ddc_implementation_free_display_info_list(dlist); return -1; }

    int n = 0;
    for (int i = 0; i < dlist->ct; i++) {
        if (dlist->info[i].is_builtin) continue;   /* OS owns the internal panel */
        DDC_Display_Handle h = NULL;
        if (ddc_implementation_open_display(dlist->info[i].dref, 0, &h) != DDC_OK) continue;

        /* Controllability rule: keep only displays that answer an initial read. */
        DDC_Non_Table_Vcp_Value probe;
        if (ddc_implementation_get_non_table_vcp_value(h, VCP_BRIGHTNESS, &probe) != DDC_OK) {
            ddc_implementation_close_display(h);
            continue;
        }
        arr[n].ops = &DDC_OPS;
        arr[n].ctx = h;
        snprintf(arr[n].id, sizeof(arr[n].id), "ddc:%04x:%04x:%d",
                 dlist->info[i].vendor_id, dlist->info[i].product_id, i);
        snprintf(arr[n].label, sizeof(arr[n].label), "DDC display %d (%04x:%04x)",
                 i, dlist->info[i].vendor_id, dlist->info[i].product_id);
        n++;
    }
    ddc_implementation_free_display_info_list(dlist);

    if (n == 0) { free(arr); return 0; }
    *out = arr; *count = n;
    return 0;
}

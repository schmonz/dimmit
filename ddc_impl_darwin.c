#include "ddc_impl.h"
#include "ddc_impl_darwin_arch.h"
#include <unistd.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdlib.h>

uint8_t ddc_checksum(uint8_t chk, uint8_t *data, int start, int end) {
    for (int i = start; i <= end; i++) {
        chk ^= data[i];
    }
    return chk;
}

DDC_Status ddc_impl_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    (void)flags;

    CGDirectDisplayID displays[16];
    uint32_t count;

    if (CGGetActiveDisplayList(16, displays, &count) != kCGErrorSuccess || count == 0) {
        return DDC_ERROR;
    }

    DDC_Display_Info_List *list = malloc(sizeof(DDC_Display_Info_List));
    if (!list) return DDC_ERROR;

    list->info = malloc(count * sizeof(DDC_Display_Info));
    if (!list->info) {
        free(list);
        return DDC_ERROR;
    }

    list->ct = 0;
    for (uint32_t i = 0; i < count; i++) {
        DDC_Display_Ref dref = malloc(sizeof(struct DDC_Display_Ref_s));
        if (!dref) continue;

        dref->display_id = (uint32_t)displays[i];
        dref->vendor_id = CGDisplayVendorNumber(displays[i]);
        dref->product_id = CGDisplayModelNumber(displays[i]);
        dref->is_builtin = CGDisplayIsBuiltin(displays[i]);

        list->info[list->ct].dref = dref;
        list->info[list->ct].vendor_id = dref->vendor_id;
        list->info[list->ct].product_id = dref->product_id;
        list->info[list->ct].is_builtin = dref->is_builtin;
        list->ct++;
    }

    *list_out = list;
    return DDC_OK;
}

void ddc_impl_free_display_info_list(DDC_Display_Info_List *list) {
    if (list) {
        if (list->info) {
            for (int i = 0; i < list->ct; i++) {
                free(list->info[i].dref);
            }
            free(list->info);
        }
        free(list);
    }
}

DDC_Status ddc_impl_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    if (!dref || !handle_out) return DDC_ERROR;
    if (dref->is_builtin) return DDC_ERROR;
    return ddc_open_arch(dref, flags, handle_out);
}

DDC_Status ddc_impl_close_display(DDC_Display_Handle handle) {
    if (!handle) return DDC_OK;
    return ddc_close_arch(handle);
}

DDC_Status ddc_impl_get_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !value_out) return DDC_ERROR;
    return ddc_get_vcp_arch(h, feature_code, value_out);
}

DDC_Status ddc_impl_set_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h) return DDC_ERROR;

    int value = (hi_byte << 8) | lo_byte;
    if (value < 0 || value > 100)
        return DDC_ERROR;

    return ddc_set_vcp_arch(h, feature_code, hi_byte, lo_byte);
}

int ddc_impl_is_authorized(int client_fd) {
    (void)client_fd;
    return 1;
}

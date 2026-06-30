#include "platform/ddc/implementation.h"
#include "platform/ddc/abstraction.h"

#include <windows.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <stdlib.h>
#include <string.h>

/* Windows DDC/CI via the Monitor Configuration API (dxva2). The shared
 * abstraction speaks VCP (mh/ml/sh/sl), so we use the low-level VCP calls:
 *   GetVCPFeatureAndVCPFeatureReply -> current + max for a feature code
 *   SetVCPFeature                   -> write a feature code
 * A physical monitor handle (HANDLE) is what we open/close.
 *
 * Builtin detection: dxva2 does not cleanly distinguish an internal panel, so
 * (like the Linux backend) every enumerated monitor is reported as external.
 * On a laptop with only its internal panel, that panel is selected and likely
 * ignores DDC -- an acceptable, expected outcome for the feasibility pass. */

/* A ref carries the physical-monitor description we enumerated; the handle is
 * the live HANDLE used for VCP I/O. */
struct DDC_Display_Ref_s    { HANDLE hmon; };
struct DDC_Display_Handle_s { HANDLE hmon; };

/* EnumDisplayMonitors callback: accumulate every physical monitor handle into a
 * growable array passed via lparam. */
typedef struct {
    HANDLE *handles;
    int count;
    int capacity;
} mon_accum_t;

static BOOL CALLBACK collect_monitors(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam) {
    (void)hdc; (void)rect;
    mon_accum_t *acc = (mon_accum_t *)lparam;

    DWORD n = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &n) || n == 0) return TRUE;

    PHYSICAL_MONITOR *pm = (PHYSICAL_MONITOR *)calloc(n, sizeof(PHYSICAL_MONITOR));
    if (!pm) return TRUE;
    if (!GetPhysicalMonitorsFromHMONITOR(hmon, n, pm)) { free(pm); return TRUE; }

    for (DWORD i = 0; i < n; i++) {
        if (acc->count == acc->capacity) {
            int newcap = acc->capacity ? acc->capacity * 2 : 4;
            HANDLE *grown = (HANDLE *)realloc(acc->handles, (size_t)newcap * sizeof(HANDLE));
            if (!grown) break;   /* keep what we have */
            acc->handles = grown;
            acc->capacity = newcap;
        }
        acc->handles[acc->count++] = pm[i].hPhysicalMonitor;
    }
    free(pm);
    return TRUE;
}

DDC_Status ddc_implementation_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    (void)flags;
    if (!list_out) return DDC_ERROR;

    mon_accum_t acc = {0};
    EnumDisplayMonitors(NULL, NULL, collect_monitors, (LPARAM)&acc);
    if (acc.count == 0) {
        free(acc.handles);
        return DDC_ERROR;
    }

    DDC_Display_Info_List *out = (DDC_Display_Info_List *)malloc(sizeof(*out));
    if (!out) { free(acc.handles); return DDC_ERROR; }
    out->ct = acc.count;
    out->info = (DDC_Display_Info *)calloc((size_t)acc.count, sizeof(DDC_Display_Info));
    if (!out->info) { free(out); free(acc.handles); return DDC_ERROR; }

    for (int i = 0; i < acc.count; i++) {
        struct DDC_Display_Ref_s *wr = (struct DDC_Display_Ref_s *)malloc(sizeof(*wr));
        if (!wr) {
            for (int j = 0; j < i; j++) free(out->info[j].dref);
            free(out->info); free(out); free(acc.handles);
            return DDC_ERROR;
        }
        wr->hmon = acc.handles[i];
        out->info[i].dref = (DDC_Display_Ref)wr;
        out->info[i].vendor_id = 0;
        out->info[i].product_id = 0;
        out->info[i].is_builtin = 0;  /* dxva2 can't tell; assume external */
    }
    free(acc.handles);
    *list_out = out;
    return DDC_OK;
}

void ddc_implementation_free_display_info_list(DDC_Display_Info_List *list) {
    if (!list) return;
    if (list->info) {
        for (int i = 0; i < list->ct; i++) {
            if (list->info[i].dref) free(list->info[i].dref);
        }
        free(list->info);
    }
    free(list);
}

DDC_Status ddc_implementation_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    if (!dref || !handle_out) return DDC_ERROR;
    struct DDC_Display_Ref_s *wr = (struct DDC_Display_Ref_s *)dref;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s *)malloc(sizeof(*h));
    if (!h) return DDC_ERROR;
    h->hmon = wr->hmon;
    *handle_out = (DDC_Display_Handle)h;
    return DDC_OK;
}

DDC_Status ddc_implementation_close_display(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s *)handle;
    DestroyPhysicalMonitor(h->hmon);  /* takes the physical-monitor HANDLE directly */
    free(h);
    return DDC_OK;
}

DDC_Status ddc_implementation_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!handle || !value_out) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s *)handle;
    DWORD current = 0, maximum = 0;
    MC_VCP_CODE_TYPE type;
    if (!GetVCPFeatureAndVCPFeatureReply(h->hmon, feature_code, &type, &current, &maximum))
        return DDC_ERROR;
    value_out->sh = (uint8_t)((current >> 8) & 0xFF);
    value_out->sl = (uint8_t)(current & 0xFF);
    value_out->mh = (uint8_t)((maximum >> 8) & 0xFF);
    value_out->ml = (uint8_t)(maximum & 0xFF);
    return DDC_OK;
}

DDC_Status ddc_implementation_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!handle) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s *)handle;
    DWORD value = ((DWORD)hi_byte << 8) | (DWORD)lo_byte;
    return SetVCPFeature(h->hmon, feature_code, value) ? DDC_OK : DDC_ERROR;
}

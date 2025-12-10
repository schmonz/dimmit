#define _GNU_SOURCE
#include "ddc_impl.h"
#include "ddc.h"
#include <ddcutil_c_api.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

/* Wrap libddcutil types inside our opaque structs */
struct DDC_Display_Ref_s { DDCA_Display_Ref dref; };
struct DDC_Display_Handle_s { DDCA_Display_Handle dh; };

DDC_Status ddc_impl_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    DDCA_Display_Info_List *dl = NULL;
    DDCA_Status st = ddca_get_display_info_list2(flags, &dl);
    if (st != 0 || !dl || dl->ct == 0) {
        if (dl) ddca_free_display_info_list(dl);
        return DDC_ERROR;
    }
    DDC_Display_Info_List *out = (DDC_Display_Info_List*)malloc(sizeof(*out));
    if (!out) { ddca_free_display_info_list(dl); return DDC_ERROR; }
    out->ct = dl->ct;
    out->info = (DDC_Display_Info*)calloc(out->ct, sizeof(DDC_Display_Info));
    if (!out->info) { free(out); ddca_free_display_info_list(dl); return DDC_ERROR; }
    for (int i = 0; i < dl->ct; i++) {
        struct DDC_Display_Ref_s *wr = (struct DDC_Display_Ref_s*)malloc(sizeof(*wr));
        if (!wr) { /* cleanup on failure */
            for (int j = 0; j < i; j++) free(out->info[j].dref);
            free(out->info); free(out); ddca_free_display_info_list(dl); return DDC_ERROR;
        }
        wr->dref = dl->info[i].dref;
        out->info[i].dref = (DDC_Display_Ref)wr;
        out->info[i].vendor_id = dl->info[i].mfg_id; /* best-effort mapping */
        out->info[i].product_id = dl->info[i].product_code;
        out->info[i].is_builtin = 0; /* libddcutil does not expose this; assume external */
    }
    /* Do NOT free ddca list yet; references held by wrappers.
       However, ddca docs advise freeing list after use. To avoid dangling, copy refs via ddca_dup_display_ref if available.
       In absence of that, keep the list allocated; we accept a small leak until process exit. */
    *list_out = out;
    return DDC_OK;
}

void ddc_impl_free_display_info_list(DDC_Display_Info_List *list) {
    if (!list) return;
    if (list->info) {
        for (int i = 0; i < list->ct; i++) {
            if (list->info[i].dref) free(list->info[i].dref);
        }
        free(list->info);
    }
    free(list);
}

DDC_Status ddc_impl_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    if (!dref || !handle_out) return DDC_ERROR;
    struct DDC_Display_Ref_s *wr = (struct DDC_Display_Ref_s*)dref;
    DDCA_Display_Handle dh = NULL;
    DDCA_Status st = ddca_open_display2(wr->dref, flags, &dh);
    if (st != 0) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)malloc(sizeof(*h));
    if (!h) { ddca_close_display(dh); return DDC_ERROR; }
    h->dh = dh;
    *handle_out = (DDC_Display_Handle)h;
    return DDC_OK;
}

DDC_Status ddc_impl_close_display(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    ddca_close_display(h->dh);
    free(h);
    return DDC_OK;
}

DDC_Status ddc_impl_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!handle || !value_out) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    DDCA_Non_Table_Vcp_Value v;
    DDCA_Status st = ddca_get_non_table_vcp_value(h->dh, feature_code, &v);
    if (st != 0) return DDC_ERROR;
    value_out->mh = v.mh; value_out->ml = v.ml; value_out->sh = v.sh; value_out->sl = v.sl;
    return DDC_OK;
}

DDC_Status ddc_impl_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!handle) return DDC_ERROR;
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    DDCA_Status st = ddca_set_non_table_vcp_value(h->dh, feature_code, hi_byte, lo_byte);
    return (st == 0) ? DDC_OK : DDC_ERROR;
}

int ddc_impl_is_authorized(int client_fd) {
    struct ucred cred; socklen_t len = sizeof(cred);
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) return 0;
    struct group *video_grp = getgrnam("video");
    if (!video_grp) return 1;
    struct passwd *pw = getpwuid(cred.uid);
    if (pw && pw->pw_gid == video_grp->gr_gid) return 1;
    int ngroups = 0; getgrouplist(pw ? pw->pw_name : "", cred.gid, NULL, &ngroups);
    gid_t *groups = (gid_t*)malloc((size_t)ngroups * sizeof(gid_t)); if (!groups) return 0;
    getgrouplist(pw ? pw->pw_name : "", cred.gid, groups, &ngroups);
    int ok = 0; for (int i = 0; i < ngroups; i++) if (groups[i] == video_grp->gr_gid) { ok = 1; break; }
    free(groups);
    return ok;
}

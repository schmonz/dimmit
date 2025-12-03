#include "ddc.h"
#include "ddc_constants.h"
#include <stdlib.h>

/* Platform-specific includes and API mappings */
#if defined(__linux__)
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <ddcutil_c_api.h>

#define DDC_PREFIX ddca
#define DDC_STATUS DDCA_Status
#define DDC_OK 0
#define DDC_Display_Handle DDCA_Display_Handle
#define DDC_Display_Info_List DDCA_Display_Info_List
#define DDC_Display_Ref DDCA_Display_Ref
#define DDC_Non_Table_Vcp_Value DDCA_Non_Table_Vcp_Value

#elif defined(__APPLE__)
#include "ddcutil_macos.h"

#define DDC_PREFIX ddcm
#define DDC_STATUS DDCM_Status
#define DDC_OK DDCM_OK
#define DDC_Display_Handle DDCM_Display_Handle
#define DDC_Display_Info_List DDCM_Display_Info_List
#define DDC_Display_Ref DDCM_Display_Ref
#define DDC_Non_Table_Vcp_Value DDCM_Non_Table_Vcp_Value

#elif defined(__NetBSD__)
#include <pwd.h>
#include <grp.h>
#include "ddcutil_netbsd.h"

#define DDC_PREFIX ddcn
#define DDC_STATUS DDCN_Status
#define DDC_OK DDCN_OK
#define DDC_Display_Handle DDCN_Display_Handle
#define DDC_Display_Info_List DDCN_Display_Info_List
#define DDC_Display_Ref DDCN_Display_Ref
#define DDC_Non_Table_Vcp_Value DDCN_Non_Table_Vcp_Value

#else
#error "Platform not yet supported"
#endif

/* Token pasting macros to create platform-specific function names */
#define DDC_CONCAT_IMPL(prefix, name) prefix##_##name
#define DDC_CONCAT(prefix, name) DDC_CONCAT_IMPL(prefix, name)
#define DDC_FUNC(name) DDC_CONCAT(DDC_PREFIX, name)

/* Unified handle structure */
struct ddc_handle {
    DDC_Display_Handle dh;
};

/* Unified ddc_open_display implementation for all platforms */
ddc_handle_t* ddc_open_display(void) {
    DDC_Display_Info_List *dlist;
    DDC_STATUS rc;

    rc = DDC_FUNC(get_display_info_list2)(0, &dlist);
    if (rc != DDC_OK || dlist->ct == 0) {
        if (rc == DDC_OK) DDC_FUNC(free_display_info_list)(dlist);
        return NULL;
    }

#if defined(__APPLE__)
    /* Find first external display on macOS */
    DDC_Display_Ref dref = NULL;
    for (int i = 0; i < dlist->ct; i++) {
        if (!dlist->info[i].is_builtin) {
            dref = dlist->info[i].dref;
            break;
        }
    }
    
    if (!dref) {
        DDC_FUNC(free_display_info_list)(dlist);
        return NULL;
    }
#else
    /* Use first display on other platforms */
    DDC_Display_Ref dref = dlist->info[0].dref;
#endif

    ddc_handle_t *handle = malloc(sizeof(ddc_handle_t));
    if (!handle) {
        DDC_FUNC(free_display_info_list)(dlist);
        return NULL;
    }

    rc = DDC_FUNC(open_display2)(dref, 0, &handle->dh);
    DDC_FUNC(free_display_info_list)(dlist);

    if (rc != DDC_OK) {
        free(handle);
        return NULL;
    }

    return handle;
}

/* Unified ddc_is_authorized implementation for all platforms */
int ddc_is_authorized(int client_fd) {
#if defined(__APPLE__)
    /* macOS: allow any local user */
    (void)client_fd;
    return 1;
    
#elif defined(__linux__)
    /* Linux: check for 'video' group membership */
    struct ucred cred;
    socklen_t len = sizeof(cred);

    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        perror("getsockopt SO_PEERCRED");
        return 0;
    }

    struct group *video_grp = getgrnam("video");
    if (!video_grp) {
        fprintf(stderr, "Warning: 'video' group not found, allowing access\n");
        return 1;
    }

    struct passwd *pw = getpwuid(cred.uid);
    if (pw && pw->pw_gid == video_grp->gr_gid) {
        return 1;
    }

    int ngroups = 0;
    getgrouplist(pw ? pw->pw_name : "", cred.gid, NULL, &ngroups);

    gid_t *groups = malloc(ngroups * sizeof(gid_t));
    if (!groups) return 0;

    getgrouplist(pw ? pw->pw_name : "", cred.gid, groups, &ngroups);

    int found = 0;
    for (int i = 0; i < ngroups; i++) {
        if (groups[i] == video_grp->gr_gid) {
            found = 1;
            break;
        }
    }

    free(groups);
    return found;
    
#elif defined(__NetBSD__)
    /* NetBSD: check for 'wheel' group membership */
    uid_t euid;
    gid_t egid;

    if (getpeereid(client_fd, &euid, &egid) < 0) {
        perror("getpeereid");
        return 0;
    }

    struct group *wheel_grp = getgrnam("wheel");
    if (!wheel_grp) {
        return 1;
    }

    struct passwd *pw = getpwuid(euid);
    if (pw && pw->pw_gid == wheel_grp->gr_gid) {
        return 1;
    }

    int ngroups = 0;
    getgrouplist(pw ? pw->pw_name : "", egid, NULL, &ngroups);

    gid_t *groups = malloc(ngroups * sizeof(gid_t));
    if (!groups) return 0;

    getgrouplist(pw ? pw->pw_name : "", egid, groups, &ngroups);

    int found = 0;
    for (int i = 0; i < ngroups; i++) {
        if (groups[i] == wheel_grp->gr_gid) {
            found = 1;
            break;
        }
    }

    free(groups);
    return found;
#endif
}

/* Unified ddc_get_brightness implementation for all platforms */
int ddc_get_brightness(ddc_handle_t *handle, int *current, int *max) {
    DDC_Non_Table_Vcp_Value valrec;
    DDC_STATUS rc = DDC_FUNC(get_non_table_vcp_value)(handle->dh, VCP_BRIGHTNESS, &valrec);
    
    if (rc != DDC_OK) return -1;
    
    *current = valrec.sh << 8 | valrec.sl;
    *max = valrec.mh << 8 | valrec.ml;
    return 0;
}

/* Unified ddc_set_brightness implementation for all platforms */
int ddc_set_brightness(ddc_handle_t *handle, int value) {
#if defined(__linux__)
    /* Linux uses single-byte value */
    DDC_STATUS rc = DDC_FUNC(set_non_table_vcp_value)(handle->dh, VCP_BRIGHTNESS, 0, value);
#else
    /* macOS and NetBSD use two-byte value */
    DDC_STATUS rc = DDC_FUNC(set_non_table_vcp_value)(handle->dh, VCP_BRIGHTNESS, 
                                                       (value >> 8) & 0xFF, value & 0xFF);
#endif
    return (rc == DDC_OK) ? 0 : -1;
}

/* Unified ddc_close_display implementation for all platforms */
void ddc_close_display(ddc_handle_t *handle) {
    if (handle) {
        DDC_FUNC(close_display)(handle->dh);
        free(handle);
    }
}

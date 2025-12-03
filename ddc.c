#include "ddc.h"

#if defined(__linux__)
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <stdio.h>
#include <ddcutil_c_api.h>

#define VCP_BRIGHTNESS 0x10

struct ddc_handle {
    DDCA_Display_Handle dh;
};

ddc_handle_t* ddc_open_display(void) {
    DDCA_Status rc;
    DDCA_Display_Info_List* dlist;

    rc = ddca_get_display_info_list2(0, &dlist);
    if (rc != 0 || dlist->ct == 0) {
        if (rc == 0) ddca_free_display_info_list(dlist);
        return NULL;
    }

    DDCA_Display_Ref dref = dlist->info[0].dref;

    ddc_handle_t *handle = malloc(sizeof(ddc_handle_t));
    if (!handle) {
        ddca_free_display_info_list(dlist);
        return NULL;
    }

    rc = ddca_open_display2(dref, 0, &handle->dh);
    ddca_free_display_info_list(dlist);

    if (rc != 0) {
        free(handle);
        return NULL;
    }

    return handle;
}

int ddc_is_authorized(int client_fd) {
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
}

int ddc_get_brightness(ddc_handle_t *handle, int *current, int *max) {
    DDCA_Non_Table_Vcp_Value valrec;
    DDCA_Status rc = ddca_get_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, &valrec);

    if (rc != 0) return -1;

    *current = valrec.sh << 8 | valrec.sl;
    *max = valrec.mh << 8 | valrec.ml;
    return 0;
}

int ddc_set_brightness(ddc_handle_t *handle, int value) {
    DDCA_Status rc = ddca_set_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, 0, value);
    return (rc == 0) ? 0 : -1;
}

void ddc_close_display(ddc_handle_t *handle) {
    if (handle) {
        ddca_close_display(handle->dh);
        free(handle);
    }
}

#elif defined(__APPLE__)
#include <stdlib.h>
#include "ddcutil_macos.h"

#define VCP_BRIGHTNESS 0x10

struct ddc_handle {
    DDCM_Display_Handle dh;
};

int ddc_is_authorized(int client_fd) {
    (void)client_fd;
    /* macOS: allow any local user */
    return 1;
}

ddc_handle_t* ddc_open_display(void) {
    DDCM_Display_Info_List *dlist;
    
    if (ddcm_get_display_info_list2(0, &dlist) != DDCM_OK || dlist->ct == 0) {
        if (dlist) ddcm_free_display_info_list(dlist);
        return NULL;
    }
    
    /* Find first external display */
    DDCM_Display_Ref dref = NULL;
    for (int i = 0; i < dlist->ct; i++) {
        if (!dlist->info[i].is_builtin) {
            dref = dlist->info[i].dref;
            break;
        }
    }
    
    if (!dref) {
        ddcm_free_display_info_list(dlist);
        return NULL;
    }
    
    ddc_handle_t *handle = malloc(sizeof(ddc_handle_t));
    if (!handle) {
        ddcm_free_display_info_list(dlist);
        return NULL;
    }
    
    if (ddcm_open_display2(dref, 0, &handle->dh) != DDCM_OK) {
        free(handle);
        ddcm_free_display_info_list(dlist);
        return NULL;
    }
    
    ddcm_free_display_info_list(dlist);
    return handle;
}

int ddc_get_brightness(ddc_handle_t *handle, int *current, int *max) {
    DDCM_Non_Table_Vcp_Value valrec;
    DDCM_Status rc = ddcm_get_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, &valrec);
    
    if (rc != DDCM_OK) return -1;
    
    *current = valrec.sh << 8 | valrec.sl;
    *max = valrec.mh << 8 | valrec.ml;
    return 0;
}

int ddc_set_brightness(ddc_handle_t *handle, int value) {
    DDCM_Status rc = ddcm_set_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, 
                                                    (value >> 8) & 0xFF, value & 0xFF);
    return (rc == DDCM_OK) ? 0 : -1;
}

void ddc_close_display(ddc_handle_t *handle) {
    if (handle) {
        ddcm_close_display(handle->dh);
        free(handle);
    }
}

#elif defined(__NetBSD__)
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include "ddcutil_netbsd.h"

#define VCP_BRIGHTNESS 0x10

struct ddc_handle {
    DDCN_Display_Handle dh;
};

int ddc_is_authorized(int client_fd) {
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
}

ddc_handle_t* ddc_open_display(void) {
    DDCN_Display_Info_List *dlist;
    
    if (ddcn_get_display_info_list2(0, &dlist) != DDCN_OK || dlist->ct == 0) {
        if (dlist) ddcn_free_display_info_list(dlist);
        return NULL;
    }
    
    DDCN_Display_Ref dref = dlist->info[0].dref;
    
    ddc_handle_t *handle = malloc(sizeof(ddc_handle_t));
    if (!handle) {
        ddcn_free_display_info_list(dlist);
        return NULL;
    }
    
    if (ddcn_open_display2(dref, 0, &handle->dh) != DDCN_OK) {
        free(handle);
        ddcn_free_display_info_list(dlist);
        return NULL;
    }
    
    ddcn_free_display_info_list(dlist);
    return handle;
}

int ddc_get_brightness(ddc_handle_t *handle, int *current, int *max) {
    DDCN_Non_Table_Vcp_Value valrec;
    DDCN_Status rc = ddcn_get_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, &valrec);
    
    if (rc != DDCN_OK) return -1;
    
    *current = valrec.sh << 8 | valrec.sl;
    *max = valrec.mh << 8 | valrec.ml;
    return 0;
}

int ddc_set_brightness(ddc_handle_t *handle, int value) {
    DDCN_Status rc = ddcn_set_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, 
                                                    (value >> 8) & 0xFF, value & 0xFF);
    return (rc == DDCN_OK) ? 0 : -1;
}

void ddc_close_display(ddc_handle_t *handle) {
    if (handle) {
        ddcn_close_display(handle->dh);
        free(handle);
    }
}

#else
#error "Platform not yet supported"
#endif

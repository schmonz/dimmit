#include "ddc_impl.h"
#include "ddc.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dev/i2c/i2c_io.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

#define DDC_ADDR_7BIT 0x37  /* 0x6E >> 1 for 7-bit addressing */

struct DDC_Display_Ref_s {
    char device_path[64];
};

struct DDC_Display_Handle_s {
    int fd;
    int max_brightness;
    int current_brightness;
};

static uint8_t ddc_checksum(uint8_t init, const uint8_t *data, int len) {
    uint8_t chk = init;
    for (int i = 0; i < len; i++) {
        chk ^= data[i];
    }
    return chk;
}

DDC_Status ddc_impl_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    (void)flags;
    if (!list_out) return DDC_ERROR;

    const char *paths[] = {
        "/dev/iic0", "/dev/iic1", "/dev/iic2", "/dev/iic3",
        NULL
    };

    DDC_Display_Info_List *list = (DDC_Display_Info_List*)malloc(sizeof(DDC_Display_Info_List));
    if (!list) return DDC_ERROR;
    list->ct = 0;
    list->info = NULL;

    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDWR);
        if (fd < 0) continue;

        uint8_t test_cmd[] = {0x51, 0x82, 0x01, VCP_BRIGHTNESS, 0x00, 0x00};
        test_cmd[5] = ddc_checksum(0x6E, test_cmd, (int)sizeof(test_cmd) - 1);

        i2c_ioctl_exec_t iie;
        memset(&iie, 0, sizeof(iie));
        iie.iie_op = I2C_OP_WRITE_WITH_STOP;
        iie.iie_addr = DDC_ADDR_7BIT;
        iie.iie_cmd = test_cmd;
        iie.iie_cmdlen = sizeof(test_cmd);

        if (ioctl(fd, I2C_IOCTL_EXEC, &iie) == 0) {
            DDC_Display_Info *newinfo = (DDC_Display_Info*)realloc(list->info, (size_t)(list->ct + 1) * sizeof(DDC_Display_Info));
            if (!newinfo) { close(fd); free(list->info); free(list); return DDC_ERROR; }
            list->info = newinfo;

            DDC_Display_Ref dref = (DDC_Display_Ref)malloc(sizeof(struct DDC_Display_Ref_s));
            if (!dref) { close(fd); free(list->info); free(list); return DDC_ERROR; }
            strncpy(dref->device_path, paths[i], sizeof(dref->device_path) - 1);
            dref->device_path[sizeof(dref->device_path) - 1] = '\0';

            list->info[list->ct].dref = dref;
            list->info[list->ct].vendor_id = 0;
            list->info[list->ct].product_id = 0;
            list->info[list->ct].is_builtin = 0;
            list->ct++;
        }

        close(fd);
    }

    if (list->ct == 0) { free(list); return DDC_ERROR; }
    *list_out = list;
    return DDC_OK;
}

void ddc_impl_free_display_info_list(DDC_Display_Info_List *list) {
    if (!list) return;
    if (list->info) { for (int i = 0; i < list->ct; i++) { free(list->info[i].dref); } free(list->info); }
    free(list);
}

DDC_Status ddc_impl_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    if (!dref || !handle_out) return DDC_ERROR;
    int fd = open(dref->device_path, O_RDWR);
    if (fd < 0) return DDC_ERROR;
    DDC_Display_Handle h = (DDC_Display_Handle)malloc(sizeof(*h));
    if (!h) { close(fd); return DDC_ERROR; }
    h->fd = fd; h->max_brightness = 100; h->current_brightness = 50; *handle_out = h; return DDC_OK;
}

DDC_Status ddc_impl_close_display(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;
    if (handle->fd >= 0) close(handle->fd);
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_impl_get_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !value_out) return DDC_ERROR;
    uint8_t cmd[] = {0x51, 0x82, 0x01, feature_code, 0x00, 0x00};
    cmd[5] = ddc_checksum(0x6E, cmd, (int)sizeof(cmd) - 1);
    i2c_ioctl_exec_t iie; memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP; iie.iie_addr = DDC_ADDR_7BIT; iie.iie_cmd = cmd; iie.iie_cmdlen = sizeof(cmd);
    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) return DDC_ERROR;
    usleep(40000);
    uint8_t reply[12]; memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_READ_WITH_STOP; iie.iie_addr = DDC_ADDR_7BIT; iie.iie_buf = reply; iie.iie_buflen = sizeof(reply);
    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) return DDC_ERROR;
    if (reply[0] != 0x6F || reply[2] != 0x02 || reply[4] != feature_code) return DDC_ERROR;
    value_out->mh = reply[6]; value_out->ml = reply[7]; value_out->sh = reply[8]; value_out->sl = reply[9];
    h->max_brightness = (reply[6] << 8) | reply[7]; h->current_brightness = (reply[8] << 8) | reply[9];
    return DDC_OK;
}

DDC_Status ddc_impl_set_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h) return DDC_ERROR;
    uint8_t cmd[] = {0x51, 0x84, 0x03, feature_code, hi_byte, lo_byte, 0x00};
    cmd[6] = ddc_checksum(0x6E, cmd, (int)sizeof(cmd) - 1);
    i2c_ioctl_exec_t iie; memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP; iie.iie_addr = DDC_ADDR_7BIT; iie.iie_cmd = cmd; iie.iie_cmdlen = sizeof(cmd);
    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) return DDC_ERROR;
    h->current_brightness = (hi_byte << 8) | lo_byte;
    return DDC_OK;
}

int ddc_impl_is_authorized(int client_fd) {
    uid_t euid; gid_t egid; if (getpeereid(client_fd, &euid, &egid) < 0) return 0;
    struct group *wheel = getgrnam("wheel"); if (!wheel) return 1;
    struct passwd *pw = getpwuid(euid); if (pw && pw->pw_gid == wheel->gr_gid) return 1;
    int ng = 0; getgrouplist(pw ? pw->pw_name : "", egid, NULL, &ng);
    gid_t *gs = (gid_t*)malloc((size_t)ng * sizeof(gid_t)); if (!gs) return 0;
    getgrouplist(pw ? pw->pw_name : "", egid, gs, &ng);
    int ok = 0; for (int i = 0; i < ng; i++) if (gs[i] == wheel->gr_gid) { ok = 1; break; }
    free(gs);
    return ok;
}

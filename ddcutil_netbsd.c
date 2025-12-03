#include "ddcutil_netbsd.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dev/i2c/i2c_io.h>

#define VCP_BRIGHTNESS 0x10
#define DDC_ADDR 0x37  /* 0x6E >> 1 for 7-bit addressing */

/* Internal display reference structure */
struct DDCN_Display_Ref_s {
    char device_path[64];
};

/* Internal display handle structure */
struct DDCN_Display_Handle_s {
    int fd;
    int max_brightness;
    int current_brightness;
};

/* Helper function to compute DDC checksum */
static uint8_t ddc_checksum(uint8_t init, const uint8_t *data, int len) {
    uint8_t chk = init;
    for (int i = 0; i < len; i++) {
        chk ^= data[i];
    }
    return chk;
}

DDCN_Status ddcn_get_display_info_list2(int flags, DDCN_Display_Info_List **list_out) {
    (void)flags;
    
    if (!list_out) return DDCN_ERROR;
    
    /* Try common i2c device paths */
    const char *paths[] = {
        "/dev/iic0", "/dev/iic1", "/dev/iic2", "/dev/iic3",
        NULL
    };
    
    DDCN_Display_Info_List *list = malloc(sizeof(DDCN_Display_Info_List));
    if (!list) return DDCN_ERROR;
    
    list->ct = 0;
    list->info = NULL;
    
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDWR);
        if (fd < 0) continue;
        
        /* Test if DDC is available by attempting to write a test command */
        uint8_t test_cmd[] = {0x51, 0x82, 0x01, VCP_BRIGHTNESS, 0x00, 0x00};
        test_cmd[5] = ddc_checksum(0x6E, test_cmd, sizeof(test_cmd) - 1);
        
        i2c_ioctl_exec_t iie;
        memset(&iie, 0, sizeof(iie));
        iie.iie_op = I2C_OP_WRITE_WITH_STOP;
        iie.iie_addr = DDC_ADDR;
        iie.iie_cmd = test_cmd;
        iie.iie_cmdlen = sizeof(test_cmd);
        
        if (ioctl(fd, I2C_IOCTL_EXEC, &iie) == 0) {
            /* Found a working DDC device */
            list->info = realloc(list->info, (list->ct + 1) * sizeof(DDCN_Display_Info));
            if (!list->info) {
                close(fd);
                free(list);
                return DDCN_ERROR;
            }
            
            DDCN_Display_Ref dref = malloc(sizeof(struct DDCN_Display_Ref_s));
            if (!dref) {
                close(fd);
                free(list->info);
                free(list);
                return DDCN_ERROR;
            }
            
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
    
    if (list->ct == 0) {
        free(list);
        return DDCN_ERROR;
    }
    
    *list_out = list;
    return DDCN_OK;
}

void ddcn_free_display_info_list(DDCN_Display_Info_List *list) {
    if (!list) return;
    
    if (list->info) {
        for (int i = 0; i < list->ct; i++) {
            if (list->info[i].dref) {
                free(list->info[i].dref);
            }
        }
        free(list->info);
    }
    
    free(list);
}

DDCN_Status ddcn_open_display2(DDCN_Display_Ref dref, int flags, DDCN_Display_Handle *handle_out) {
    (void)flags;
    
    if (!dref || !handle_out) return DDCN_ERROR;
    
    int fd = open(dref->device_path, O_RDWR);
    if (fd < 0) return DDCN_ERROR;
    
    DDCN_Display_Handle handle = malloc(sizeof(struct DDCN_Display_Handle_s));
    if (!handle) {
        close(fd);
        return DDCN_ERROR;
    }
    
    handle->fd = fd;
    handle->max_brightness = 100;
    handle->current_brightness = 50;
    
    *handle_out = handle;
    return DDCN_OK;
}

DDCN_Status ddcn_close_display(DDCN_Display_Handle handle) {
    if (!handle) return DDCN_ERROR;
    
    if (handle->fd >= 0) {
        close(handle->fd);
    }
    
    free(handle);
    return DDCN_OK;
}

DDCN_Status ddcn_get_non_table_vcp_value(DDCN_Display_Handle h, uint8_t feature_code, DDCN_Non_Table_Vcp_Value *value_out) {
    if (!h || !value_out) return DDCN_ERROR;
    
    /* Send DDC get command */
    uint8_t cmd[] = {0x51, 0x82, 0x01, feature_code, 0x00, 0x00};
    cmd[5] = ddc_checksum(0x6E, cmd, sizeof(cmd) - 1);
    
    i2c_ioctl_exec_t iie;
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP;
    iie.iie_addr = DDC_ADDR;
    iie.iie_cmd = cmd;
    iie.iie_cmdlen = sizeof(cmd);
    
    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) {
        return DDCN_ERROR;
    }
    
    /* Wait for monitor to process */
    usleep(40000);
    
    /* Read reply */
    uint8_t reply[12];
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_READ_WITH_STOP;
    iie.iie_addr = DDC_ADDR;
    iie.iie_buf = reply;
    iie.iie_buflen = sizeof(reply);
    
    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) {
        return DDCN_ERROR;
    }
    
    /* Validate reply structure */
    if (reply[0] != 0x6F || reply[2] != 0x02 || reply[4] != feature_code) {
        return DDCN_ERROR;
    }
    
    /* Extract values */
    value_out->mh = reply[6];
    value_out->ml = reply[7];
    value_out->sh = reply[8];
    value_out->sl = reply[9];
    
    /* Update cached values */
    h->max_brightness = (reply[6] << 8) | reply[7];
    h->current_brightness = (reply[8] << 8) | reply[9];
    
    return DDCN_OK;
}

DDCN_Status ddcn_set_non_table_vcp_value(DDCN_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h) return DDCN_ERROR;
    
    /* Build DDC set command */
    uint8_t cmd[] = {0x51, 0x84, 0x03, feature_code, hi_byte, lo_byte, 0x00};
    cmd[6] = ddc_checksum(0x6E, cmd, sizeof(cmd) - 1);
    
    i2c_ioctl_exec_t iie;
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP;
    iie.iie_addr = DDC_ADDR;
    iie.iie_cmd = cmd;
    iie.iie_cmdlen = sizeof(cmd);
    
    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) {
        return DDCN_ERROR;
    }
    
    /* Update cached value */
    h->current_brightness = (hi_byte << 8) | lo_byte;
    
    return DDCN_OK;
}

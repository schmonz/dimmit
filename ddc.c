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
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/i2c/IOI2CInterface.h>
#include <ApplicationServices/ApplicationServices.h>

#define VCP_BRIGHTNESS 0x10
#define DDC_ADDR 0x6E
#define DDC_REPLY_ADDR 0x6F

struct ddc_handle {
    io_service_t framebuffer;
    IOI2CConnectRef i2c;
};

int ddc_is_authorized(int client_fd) {
    (void)client_fd;
    /* macOS: allow any local user */
    return 1;
}

static int ddc_write(ddc_handle_t *h, const uint8_t *data, size_t len) {
    IOI2CRequest request;
    memset(&request, 0, sizeof(request));

    request.commFlags = 0;
    request.sendAddress = DDC_ADDR;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)data;
    request.sendBytes = len;

    IOReturn ret = IOI2CSendRequest(h->i2c, kNilOptions, &request);
    return (ret == kIOReturnSuccess) ? 0 : -1;
}

static int ddc_read(ddc_handle_t *h, const uint8_t *data, size_t len) {
    IOI2CRequest request;
    memset(&request, 0, sizeof(request));

    request.commFlags = 0;
    request.replyAddress = DDC_REPLY_ADDR;
    request.replyTransactionType = kIOI2CSimpleTransactionType;
    request.replyBuffer = (vm_address_t)data;
    request.replyBytes = len;

    IOReturn ret = IOI2CSendRequest(h->i2c, kNilOptions, &request);
    return (ret == kIOReturnSuccess) ? 0 : -1;
}

ddc_handle_t* ddc_open_display(void) {
    CGDirectDisplayID displays[16];
    uint32_t count;

    if (CGGetActiveDisplayList(16, displays, &count) != kCGErrorSuccess || count == 0) {
        return NULL;
    }

    /* Find first external display */
    for (uint32_t i = 0; i < count; i++) {
        if (CGDisplayIsBuiltin(displays[i])) continue;

        /* Get IOService for this specific display using modern API */
        CFMutableDictionaryRef matching = IOServiceMatching("IOFramebuffer");
        if (!matching) continue;

        /* Add the display's vendor/product to match criteria */
        CFNumberRef vendorID = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(uint32_t){CGDisplayVendorNumber(displays[i])});
        CFNumberRef productID = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &(uint32_t){CGDisplayModelNumber(displays[i])});
        
        if (vendorID && productID) {
            CFDictionarySetValue(matching, CFSTR("IODisplayVendorID"), vendorID);
            CFDictionarySetValue(matching, CFSTR("IODisplayProductID"), productID);
        }
        if (vendorID) CFRelease(vendorID);
        if (productID) CFRelease(productID);

        io_iterator_t iterator;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != kIOReturnSuccess)
            continue;

        io_service_t framebuffer = 0;
        io_service_t service;
        while ((service = IOIteratorNext(iterator)) != 0) {
            /* Verify this framebuffer has I2C interface */
            IOItemCount busCount;
            if (IOFBGetI2CInterfaceCount(service, &busCount) == kIOReturnSuccess && busCount > 0) {
                framebuffer = service;
                break;
            }
            IOObjectRelease(service);
        }
        IOObjectRelease(iterator);

        if (!framebuffer) continue;

        io_service_t i2cService;
        if (IOFBCopyI2CInterfaceForBus(framebuffer, 0, &i2cService) != kIOReturnSuccess) {
            IOObjectRelease(framebuffer);
            continue;
        }

        IOI2CConnectRef i2c;
        if (IOI2CInterfaceOpen(i2cService, kNilOptions, &i2c) != kIOReturnSuccess) {
            IOObjectRelease(i2cService);
            IOObjectRelease(framebuffer);
            continue;
        }

        IOObjectRelease(i2cService);

        ddc_handle_t *handle = malloc(sizeof(ddc_handle_t));
        if (!handle) {
            IOI2CInterfaceClose(i2c, kNilOptions);
            IOObjectRelease(framebuffer);
            continue;
        }

        handle->framebuffer = framebuffer;
        handle->i2c = i2c;
        return handle;
    }

    return NULL;
}

int ddc_get_brightness(ddc_handle_t *h, int *current, int *max) {
    uint8_t cmd[] = {0x51, 0x82, 0x01, VCP_BRIGHTNESS, 0x00, 0x00};
    cmd[5] = 0x6E ^ cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3]; /* Checksum */

    if (ddc_write(h, cmd, sizeof(cmd)) != 0)
        return -1;

    usleep(40000); /* DDC needs time to respond */

    uint8_t reply[12];
    if (ddc_read(h, reply, sizeof(reply)) != 0)
        return -1;

    if (reply[0] != 0x6F || reply[2] != 0x02 || reply[4] != VCP_BRIGHTNESS)
        return -1;

    *max = (reply[6] << 8) | reply[7];
    *current = (reply[8] << 8) | reply[9];
    return 0;
}

int ddc_set_brightness(ddc_handle_t *h, int value) {
    uint8_t cmd[] = {0x51, 0x84, 0x03, VCP_BRIGHTNESS,
                     (value >> 8) & 0xFF, value & 0xFF, 0x00};
    cmd[6] = 0x6E ^ cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3] ^ cmd[4] ^ cmd[5];

    return ddc_write(h, cmd, sizeof(cmd));
}

void ddc_close_display(ddc_handle_t *h) {
    if (h) {
        if (h->i2c) IOI2CInterfaceClose(h->i2c, kNilOptions);
        free(h);
    }
}

#elif defined(__NetBSD__)
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dev/i2c/i2c_io.h>

#define VCP_BRIGHTNESS 0x10
#define DDC_ADDR 0x37  /* 0x6E >> 1 for 7-bit addressing */

struct ddc_handle {
    int fd;
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
    /* Try common i2c device paths */
    const char *paths[] = {
        "/dev/iic0", "/dev/iic1", "/dev/iic2", "/dev/iic3",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDWR);
        if (fd < 0) continue;

        /* Test if DDC is available by attempting to write a command */
        uint8_t test_cmd[] = {0x51, 0x82, 0x01, VCP_BRIGHTNESS, 0x00, 0x00};
        test_cmd[5] = 0x6E ^ test_cmd[0] ^ test_cmd[1] ^ test_cmd[2] ^ test_cmd[3];

        i2c_ioctl_exec_t iie;
        memset(&iie, 0, sizeof(iie));
        iie.iie_op = I2C_OP_WRITE_WITH_STOP;
        iie.iie_addr = DDC_ADDR;
        iie.iie_cmd = test_cmd;
        iie.iie_cmdlen = sizeof(test_cmd);

        if (ioctl(fd, I2C_IOCTL_EXEC, &iie) == 0) {
            ddc_handle_t *handle = malloc(sizeof(ddc_handle_t));
            if (handle) {
                handle->fd = fd;
                return handle;
            }
        }

        close(fd);
    }

    return NULL;
}

int ddc_get_brightness(ddc_handle_t *h, int *current, int *max) {
    uint8_t cmd[] = {0x51, 0x82, 0x01, VCP_BRIGHTNESS, 0x00, 0x00};
    cmd[5] = 0x6E ^ cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3];

    i2c_ioctl_exec_t iie;
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP;
    iie.iie_addr = DDC_ADDR;
    iie.iie_cmd = cmd;
    iie.iie_cmdlen = sizeof(cmd);

    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0)
        return -1;

    usleep(40000);

    uint8_t reply[12];
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_READ_WITH_STOP;
    iie.iie_addr = DDC_ADDR;
    iie.iie_buf = reply;
    iie.iie_buflen = sizeof(reply);

    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0)
        return -1;

    if (reply[0] != 0x6F || reply[2] != 0x02 || reply[4] != VCP_BRIGHTNESS)
        return -1;

    *max = (reply[6] << 8) | reply[7];
    *current = (reply[8] << 8) | reply[9];
    return 0;
}

int ddc_set_brightness(ddc_handle_t *h, int value) {
    uint8_t cmd[] = {0x51, 0x84, 0x03, VCP_BRIGHTNESS,
                     (value >> 8) & 0xFF, value & 0xFF, 0x00};
    cmd[6] = 0x6E ^ cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3] ^ cmd[4] ^ cmd[5];

    i2c_ioctl_exec_t iie;
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP;
    iie.iie_addr = DDC_ADDR;
    iie.iie_cmd = cmd;
    iie.iie_cmdlen = sizeof(cmd);

    return (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) == 0) ? 0 : -1;
}

void ddc_close_display(ddc_handle_t *h) {
    if (h) {
        close(h->fd);
        free(h);
    }
}

#else
#error "Platform not yet supported"
#endif

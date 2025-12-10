/* Ensure GNU extensions for Linux-specific APIs */
#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include "ddc.h"
#include <stdlib.h>

/* On Linux, map compat-like names to libddcutil's ddca_* symbols. */
#if defined(__linux__)
#define ddc_get_display_info_list2   ddca_get_display_info_list2
#define ddc_free_display_info_list   ddca_free_display_info_list
#define ddc_open_display2            ddca_open_display2
#define ddc_close_display2           ddca_close_display
#define ddc_get_non_table_vcp_value  ddca_get_non_table_vcp_value
#define ddc_set_non_table_vcp_value  ddca_set_non_table_vcp_value
#endif

/* Types and non-Linux implementations */
#include <stdint.h>

#if defined(__linux__)
#include <ddcutil_c_api.h>
/* On Linux, keep types compatible with libddcutil */
typedef DDCA_Status DDC_Status;
typedef DDCA_Display_Handle DDC_Display_Handle;
typedef DDCA_Display_Ref DDC_Display_Ref;
typedef DDCA_Display_Info_List DDC_Display_Info_List;
typedef DDCA_Non_Table_Vcp_Value DDC_Non_Table_Vcp_Value;

#ifndef DDC_OK
#define DDC_OK 0
#endif
#ifndef DDC_ERROR
#define DDC_ERROR -1
#endif

#else /* non-Linux: provide a uniform interface implemented per-OS */

/* Status codes matching libddcutil convention */
typedef int DDC_Status;
#define DDC_OK 0
#define DDC_ERROR -1

/* Display handle - opaque type */
typedef struct DDC_Display_Handle_s *DDC_Display_Handle;

/* Display reference - opaque type */
typedef struct DDC_Display_Ref_s *DDC_Display_Ref;

/* Display info for enumeration */
typedef struct {
    DDC_Display_Ref dref;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
} DDC_Display_Info;

/* Display info list */
typedef struct {
    int ct;  /* count */
    DDC_Display_Info *info;
} DDC_Display_Info_List;

/* VCP (Virtual Control Panel) value structure */
typedef struct {
    uint8_t mh;  /* max high byte */
    uint8_t ml;  /* max low byte */
    uint8_t sh;  /* current (set) high byte */
    uint8_t sl;  /* current (set) low byte */
} DDC_Non_Table_Vcp_Value;

/* Get list of detected displays */
DDC_Status ddc_get_display_info_list2(int flags, DDC_Display_Info_List **list_out);

/* Free display info list */
void ddc_free_display_info_list(DDC_Display_Info_List *list);

/* Open a display for DDC communication */
DDC_Status ddc_open_display2(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out);

/* Close display handle */
DDC_Status ddc_close_display2(DDC_Display_Handle handle);

/* Get VCP feature value (non-table) */
DDC_Status ddc_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out);

/* Set VCP feature value (non-table) */
DDC_Status ddc_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte);

#endif /* __linux__ */

#if !defined(__linux__)

#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
/* Linux: delegate to libddcutil */
#include <ddcutil_c_api.h>

DDC_Status ddc_get_display_info_list2(int flags, DDC_Display_Info_List **list_out) {
    return ddca_get_display_info_list2(flags, list_out);
}

void ddc_free_display_info_list(DDC_Display_Info_List *list) {
    ddca_free_display_info_list(list);
}

DDC_Status ddc_open_display2(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    return ddca_open_display2(dref, flags, handle_out);
}

DDC_Status ddc_close_display2(DDC_Display_Handle handle) {
    return ddca_close_display(handle);
}

DDC_Status ddc_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    return ddca_get_non_table_vcp_value(handle, feature_code, value_out);
}

DDC_Status ddc_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    return ddca_set_non_table_vcp_value(handle, feature_code, hi_byte, lo_byte);
}

#elif defined(__APPLE__)
/* macOS: inline previous implementation */

#include <unistd.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/i2c/IOI2CInterface.h>
#include <IOKit/graphics/IOGraphicsLib.h>

/* Private IOAVService APIs for Apple Silicon */
typedef struct __IOAVService *IOAVServiceRef;
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern int IOAVServiceReadI2C(IOAVServiceRef service, uint32_t chipAddress, uint32_t offset, void *buffer, uint32_t length);
extern int IOAVServiceWriteI2C(IOAVServiceRef service, uint32_t chipAddress, uint32_t dataAddress, void *buffer, uint32_t length);

#define ARM64_DDC_7BIT_ADDRESS 0x37
#define ARM64_DDC_DATA_ADDRESS 0x51

typedef enum {
    DDC_METHOD_INTEL,
    DDC_METHOD_ARM64
} ddc_method_t;

struct DDC_Display_Ref_s {
    CGDirectDisplayID display_id;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
};

struct DDC_Display_Handle_s {
    ddc_method_t method;
    union {
        struct {
            io_service_t framebuffer;
            IOI2CConnectRef i2c;
        } intel;
        struct {
            IOAVServiceRef avservice;
        } arm64;
    } data;
    int max_brightness;
    int current_brightness;
};

static int ddc_write_intel(DDC_Display_Handle h, const uint8_t *data, size_t len) {
    IOI2CRequest request;
    memset(&request, 0, sizeof(request));
    request.commFlags = 0;
    request.sendAddress = DDC_ADDR;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)data;
    request.sendBytes = len;
    IOReturn ret = IOI2CSendRequest(h->data.intel.i2c, kNilOptions, &request);
    return (ret == kIOReturnSuccess) ? 0 : -1;
}

static int ddc_read_intel(DDC_Display_Handle h, const uint8_t *data, size_t len) {
    IOI2CRequest request;
    memset(&request, 0, sizeof(request));
    request.commFlags = 0;
    request.replyAddress = DDC_REPLY_ADDR;
    request.replyTransactionType = kIOI2CSimpleTransactionType;
    request.replyBuffer = (vm_address_t)data;
    request.replyBytes = len;
    IOReturn ret = IOI2CSendRequest(h->data.intel.i2c, kNilOptions, &request);
    return (ret == kIOReturnSuccess) ? 0 : -1;
}

static uint8_t ddc_checksum(uint8_t chk, uint8_t *data, int start, int end) {
    for (int i = start; i <= end; i++) {
        chk ^= data[i];
    }
    return chk;
}

DDC_Status ddc_get_display_info_list2(int flags, DDC_Display_Info_List **list_out) {
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

        dref->display_id = displays[i];
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

void ddc_free_display_info_list(DDC_Display_Info_List *list) {
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

DDC_Status ddc_open_display2(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;

    if (!dref || !handle_out) return DDC_ERROR;

    /* Skip built-in displays */
    if (dref->is_builtin) return DDC_ERROR;

    /* Try Intel Mac method (IOFramebuffer) */
    CFMutableDictionaryRef matching = IOServiceMatching("IOFramebuffer");
    if (matching) {
        CFNumberRef vendorID = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &dref->vendor_id);
        CFNumberRef productID = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &dref->product_id);

        if (vendorID && productID) {
            CFDictionarySetValue(matching, CFSTR("IODisplayVendorID"), vendorID);
            CFDictionarySetValue(matching, CFSTR("IODisplayProductID"), productID);
        }
        if (vendorID) CFRelease(vendorID);
        if (productID) CFRelease(productID);

        io_iterator_t iterator;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == kIOReturnSuccess) {
            io_service_t framebuffer = 0;
            io_service_t service;
            while ((service = IOIteratorNext(iterator)) != 0) {
                IOItemCount busCount;
                if (IOFBGetI2CInterfaceCount(service, &busCount) == kIOReturnSuccess && busCount > 0) {
                    framebuffer = service;
                    break;
                }
                IOObjectRelease(service);
            }
            IOObjectRelease(iterator);

            if (framebuffer) {
                io_service_t i2cService;
                if (IOFBCopyI2CInterfaceForBus(framebuffer, 0, &i2cService) == kIOReturnSuccess) {
                    IOI2CConnectRef i2c;
                    if (IOI2CInterfaceOpen(i2cService, kNilOptions, &i2c) == kIOReturnSuccess) {
                        IOObjectRelease(i2cService);
                        DDC_Display_Handle handle = malloc(sizeof(struct DDC_Display_Handle_s));
                        if (handle) {
                            handle->method = DDC_METHOD_INTEL;
                            handle->data.intel.framebuffer = framebuffer;
                            handle->data.intel.i2c = i2c;
                            handle->max_brightness = 100;
                            handle->current_brightness = 50;
                            *handle_out = handle;
                            return DDC_OK;
                        }
                        IOI2CInterfaceClose(i2c, kNilOptions);
                    }
                    IOObjectRelease(i2cService);
                }
                IOObjectRelease(framebuffer);
            }
        }
    }

    /* Fall back to Apple Silicon method (IOAVService via IORegistry) */
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    if (root) {
        io_iterator_t iterator;
        if (IORegistryEntryCreateIterator(root, "IOService", kIORegistryIterateRecursively, &iterator) == KERN_SUCCESS) {
            io_service_t service;
            IOAVServiceRef avservice = NULL;

            while ((service = IOIteratorNext(iterator))) {
                char name[128];
                if (IORegistryEntryGetName(service, name) == KERN_SUCCESS) {
                    if (strcmp(name, "DCPAVServiceProxy") == 0) {
                        CFTypeRef location_ref = IORegistryEntryCreateCFProperty(service, CFSTR("Location"),
                            kCFAllocatorDefault, kIORegistryIterateRecursively);
                        if (location_ref) {
                            if (CFGetTypeID(location_ref) == CFStringGetTypeID()) {
                                CFStringRef location = (CFStringRef)location_ref;
                                if (CFStringCompare(location, CFSTR("External"), 0) == kCFCompareEqualTo) {
                                    avservice = IOAVServiceCreateWithService(kCFAllocatorDefault, service);
                                    CFRelease(location_ref);
                                    IOObjectRelease(service);
                                    break;
                                }
                            }
                            CFRelease(location_ref);
                        }
                    }
                }
                IOObjectRelease(service);
            }

            IOObjectRelease(iterator);

            if (avservice) {
                IOObjectRelease(root);
                DDC_Display_Handle handle = malloc(sizeof(struct DDC_Display_Handle_s));
                if (!handle) {
                    CFRelease(avservice);
                    return DDC_ERROR;
                }
                handle->method = DDC_METHOD_ARM64;
                handle->data.arm64.avservice = avservice;
                handle->max_brightness = 100;
                handle->current_brightness = 50;
                *handle_out = handle;
                return DDC_OK;
            }
        }
        IOObjectRelease(root);
    }

    return DDC_ERROR;
}

DDC_Status ddc_close_display2(DDC_Display_Handle handle) {
    if (handle) {
        if (handle->method == DDC_METHOD_INTEL) {
            if (handle->data.intel.i2c) {
                IOI2CInterfaceClose(handle->data.intel.i2c, kNilOptions);
            }
            if (handle->data.intel.framebuffer) {
                IOObjectRelease(handle->data.intel.framebuffer);
            }
        } else {
            if (handle->data.arm64.avservice) {
                CFRelease(handle->data.arm64.avservice);
            }
        }
        free(handle);
    }
    return DDC_OK;
}

DDC_Status ddc_get_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !value_out) return DDC_ERROR;

    if (h->method == DDC_METHOD_INTEL) {
        /* Intel Mac - read via DDC/CI */
        uint8_t cmd[] = {0x51, 0x82, 0x01, feature_code, 0x00, 0x00};
        cmd[5] = 0x6E ^ cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3];

        if (ddc_write_intel(h, cmd, sizeof(cmd)) != 0)
            return DDC_ERROR;

        usleep(40000);

        uint8_t reply[12];
        if (ddc_read_intel(h, reply, sizeof(reply)) != 0)
            return DDC_ERROR;

        if (reply[0] != 0x6F || reply[2] != 0x02 || reply[4] != feature_code)
            return DDC_ERROR;

        value_out->mh = reply[6];
        value_out->ml = reply[7];
        value_out->sh = reply[8];
        value_out->sl = reply[9];

        h->max_brightness = (reply[6] << 8) | reply[7];
        h->current_brightness = (reply[8] << 8) | reply[9];
        return DDC_OK;
    } else {
        /* Apple Silicon - read via IOAVService */
        uint8_t cmd[1] = {feature_code};

        uint8_t packet[8];
        packet[0] = 0x80 | (sizeof(cmd) + 1);
        packet[1] = sizeof(cmd);
        packet[2] = cmd[0];
        packet[3] = ddc_checksum(ARM64_DDC_7BIT_ADDRESS << 1 ^ ARM64_DDC_DATA_ADDRESS,
                                 packet, 0, 2);

        int ret = IOAVServiceWriteI2C(h->data.arm64.avservice, ARM64_DDC_7BIT_ADDRESS,
                                      ARM64_DDC_DATA_ADDRESS, packet, sizeof(packet));
        if (ret != 0)
            return DDC_ERROR;

        usleep(40000);

        uint8_t reply[12];
        ret = IOAVServiceReadI2C(h->data.arm64.avservice, ARM64_DDC_7BIT_ADDRESS,
                                 ARM64_DDC_DATA_ADDRESS, reply, sizeof(reply));
        if (ret != 0)
            return DDC_ERROR;

        if (reply[0] != DDC_ADDR || reply[2] != 0x02 || reply[4] != feature_code)
            return DDC_ERROR;

        value_out->mh = reply[6];
        value_out->ml = reply[7];
        value_out->sh = reply[8];
        value_out->sl = reply[9];

        h->max_brightness = (reply[6] << 8) | reply[7];
        h->current_brightness = (reply[8] << 8) | reply[9];
        return DDC_OK;
    }
}

DDC_Status ddc_set_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h) return DDC_ERROR;

    int value = (hi_byte << 8) | lo_byte;
    if (value < 0 || value > 100)
        return DDC_ERROR;

    if (h->method == DDC_METHOD_INTEL) {
        /* Intel Mac - write via DDC/CI */
        uint8_t cmd[] = {0x51, 0x84, 0x03, feature_code, hi_byte, lo_byte, 0x00};
        cmd[6] = 0x6E ^ cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3] ^ cmd[4] ^ cmd[5];

        int ret = ddc_write_intel(h, cmd, sizeof(cmd));
        if (ret == 0) {
            h->current_brightness = value;
        }
        return (ret == 0) ? DDC_OK : DDC_ERROR;
    } else {
        /* Apple Silicon - write via IOAVService */
        uint8_t send[3] = {feature_code, hi_byte, lo_byte};

        uint8_t packet[8];
        packet[0] = 0x80 | (sizeof(send) + 1);
        packet[1] = sizeof(send);
        packet[2] = send[0];
        packet[3] = send[1];
        packet[4] = send[2];
        packet[5] = ddc_checksum(ARM64_DDC_7BIT_ADDRESS << 1 ^ ARM64_DDC_DATA_ADDRESS,
                                 packet, 0, 4);

        int ret = IOAVServiceWriteI2C(h->data.arm64.avservice, ARM64_DDC_7BIT_ADDRESS,
                                      ARM64_DDC_DATA_ADDRESS, packet, sizeof(packet));

        if (ret == 0) {
            h->current_brightness = value;
            return DDC_OK;
        }
        return DDC_ERROR;
    }
}

#elif defined(__NetBSD__)
/* NetBSD: inline previous implementation */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dev/i2c/i2c_io.h>

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

DDC_Status ddc_get_display_info_list2(int flags, DDC_Display_Info_List **list_out) {
    (void)flags;

    if (!list_out) return DDC_ERROR;

    const char *paths[] = {
        "/dev/iic0", "/dev/iic1", "/dev/iic2", "/dev/iic3",
        NULL
    };

    DDC_Display_Info_List *list = malloc(sizeof(DDC_Display_Info_List));
    if (!list) return DDC_ERROR;

    list->ct = 0;
    list->info = NULL;

    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDWR);
        if (fd < 0) continue;

        uint8_t test_cmd[] = {0x51, 0x82, 0x01, VCP_BRIGHTNESS, 0x00, 0x00};
        test_cmd[5] = ddc_checksum(0x6E, test_cmd, sizeof(test_cmd) - 1);

        i2c_ioctl_exec_t iie;
        memset(&iie, 0, sizeof(iie));
        iie.iie_op = I2C_OP_WRITE_WITH_STOP;
        iie.iie_addr = DDC_ADDR_7BIT;
        iie.iie_cmd = test_cmd;
        iie.iie_cmdlen = sizeof(test_cmd);

        if (ioctl(fd, I2C_IOCTL_EXEC, &iie) == 0) {
            list->info = realloc(list->info, (list->ct + 1) * sizeof(DDC_Display_Info));
            if (!list->info) {
                close(fd);
                free(list);
                return DDC_ERROR;
            }

            DDC_Display_Ref dref = malloc(sizeof(struct DDC_Display_Ref_s));
            if (!dref) {
                close(fd);
                free(list->info);
                free(list);
                return DDC_ERROR;
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
        return DDC_ERROR;
    }

    *list_out = list;
    return DDC_OK;
}

void ddc_free_display_info_list(DDC_Display_Info_List *list) {
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

DDC_Status ddc_open_display2(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;

    if (!dref || !handle_out) return DDC_ERROR;

    int fd = open(dref->device_path, O_RDWR);
    if (fd < 0) return DDC_ERROR;

    DDC_Display_Handle handle = malloc(sizeof(struct DDC_Display_Handle_s));
    if (!handle) {
        close(fd);
        return DDC_ERROR;
    }

    handle->fd = fd;
    handle->max_brightness = 100;
    handle->current_brightness = 50;

    *handle_out = handle;
    return DDC_OK;
}

DDC_Status ddc_close_display2(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;

    if (handle->fd >= 0) {
        close(handle->fd);
    }

    free(handle);
    return DDC_OK;
}

DDC_Status ddc_get_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !value_out) return DDC_ERROR;

    uint8_t cmd[] = {0x51, 0x82, 0x01, feature_code, 0x00, 0x00};
    cmd[5] = ddc_checksum(0x6E, cmd, sizeof(cmd) - 1);

    i2c_ioctl_exec_t iie;
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP;
    iie.iie_addr = DDC_ADDR_7BIT;
    iie.iie_cmd = cmd;
    iie.iie_cmdlen = sizeof(cmd);

    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) {
        return DDC_ERROR;
    }

    usleep(40000);

    uint8_t reply[12];
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_READ_WITH_STOP;
    iie.iie_addr = DDC_ADDR_7BIT;
    iie.iie_buf = reply;
    iie.iie_buflen = sizeof(reply);

    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) {
        return DDC_ERROR;
    }

    if (reply[0] != 0x6F || reply[2] != 0x02 || reply[4] != feature_code) {
        return DDC_ERROR;
    }

    value_out->mh = reply[6];
    value_out->ml = reply[7];
    value_out->sh = reply[8];
    value_out->sl = reply[9];

    h->max_brightness = (reply[6] << 8) | reply[7];
    h->current_brightness = (reply[8] << 8) | reply[9];

    return DDC_OK;
}

DDC_Status ddc_set_non_table_vcp_value(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h) return DDC_ERROR;

    uint8_t cmd[] = {0x51, 0x84, 0x03, feature_code, hi_byte, lo_byte, 0x00};
    cmd[6] = ddc_checksum(0x6E, cmd, sizeof(cmd) - 1);

    i2c_ioctl_exec_t iie;
    memset(&iie, 0, sizeof(iie));
    iie.iie_op = I2C_OP_WRITE_WITH_STOP;
    iie.iie_addr = DDC_ADDR_7BIT;
    iie.iie_cmd = cmd;
    iie.iie_cmdlen = sizeof(cmd);

    if (ioctl(h->fd, I2C_IOCTL_EXEC, &iie) < 0) {
        return DDC_ERROR;
    }

    h->current_brightness = (hi_byte << 8) | lo_byte;
    return DDC_OK;
}

#else
#error "Platform not yet supported"
#endif
#endif

/* Platform-specific includes used only for authorization checks */
#if defined(__linux__)
#include <sys/socket.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#elif defined(__NetBSD__)
#include <pwd.h>
#include <grp.h>
#endif

/* Unified handle structure */
struct ddc_handle {
    DDC_Display_Handle dh;
};

/* Unified ddc_open_display implementation for all platforms */
ddc_handle_t* ddc_open_display(void) {
    DDC_Display_Info_List *dlist;
    DDC_Status rc;

    rc = ddc_get_display_info_list2(0, &dlist);
    if (rc != DDC_OK || dlist->ct == 0) {
        if (rc == DDC_OK) ddc_free_display_info_list(dlist);
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
        ddc_free_display_info_list(dlist);
        return NULL;
    }
#else
    /* Use first display on other platforms */
    DDC_Display_Ref dref = dlist->info[0].dref;
#endif

    ddc_handle_t *handle = malloc(sizeof(ddc_handle_t));
    if (!handle) {
        ddc_free_display_info_list(dlist);
        return NULL;
    }

    rc = ddc_open_display2(dref, 0, &handle->dh);
    ddc_free_display_info_list(dlist);

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
    DDC_Status rc = ddc_get_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, &valrec);
    
    if (rc != DDC_OK) return -1;
    
    *current = valrec.sh << 8 | valrec.sl;
    *max = valrec.mh << 8 | valrec.ml;
    return 0;
}

/* Unified ddc_set_brightness implementation for all platforms */
int ddc_set_brightness(ddc_handle_t *handle, int value) {
#if defined(__linux__)
    /* Linux uses single-byte value */
    DDC_Status rc = ddc_set_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS, 0, value);
#else
    /* macOS and NetBSD use two-byte value */
    DDC_Status rc = ddc_set_non_table_vcp_value(handle->dh, VCP_BRIGHTNESS,
                                                       (value >> 8) & 0xFF, value & 0xFF);
#endif
    return (rc == DDC_OK) ? 0 : -1;
}

/* Unified ddc_close_display implementation for all platforms */
void ddc_close_display(ddc_handle_t *handle) {
    if (handle) {
        ddc_close_display2(handle->dh);
        free(handle);
    }
}
#include "ddcutil_compat.h"
#include "ddc_constants.h"

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

DDC_Status ddc_close_display(DDC_Display_Handle handle) {
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

DDC_Status ddc_close_display(DDC_Display_Handle handle) {
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

DDC_Status ddc_close_display(DDC_Display_Handle handle) {
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

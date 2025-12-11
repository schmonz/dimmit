#include "ddc_impl.h"
#include "ddc.h"
#include <unistd.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <stdlib.h>
#include <string.h>

struct DDC_Display_Ref_s {
    CGDirectDisplayID display_id;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
};

/*
 * Shared helpers and types
 */

static uint8_t ddc_checksum(uint8_t chk, uint8_t *data, int start, int end) {
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

/*
 * Architecture-specific implementation: single top-level conditional
 */

#if defined(__x86_64__)

#include <IOKit/i2c/IOI2CInterface.h>
#include <IOKit/graphics/IOGraphicsLib.h>

struct DDC_Display_Handle_s {
    union {
        struct {
            io_service_t framebuffer;
            IOI2CConnectRef i2c;
        } intel;
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

static DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
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
    return DDC_ERROR;
}

static DDC_Status ddc_close_arch(DDC_Display_Handle handle) {
    if (handle->data.intel.i2c) {
        IOI2CInterfaceClose(handle->data.intel.i2c, kNilOptions);
    }
    if (handle->data.intel.framebuffer) {
        IOObjectRelease(handle->data.intel.framebuffer);
    }
    free(handle);
    return DDC_OK;
}

static DDC_Status ddc_get_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
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
}

static DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    (void)feature_code;
    uint8_t cmd[] = {0x51, 0x84, 0x03, feature_code, hi_byte, lo_byte, 0x00};
    cmd[6] = 0x6E ^ cmd[0] ^ cmd[1] ^ cmd[2] ^ cmd[3] ^ cmd[4] ^ cmd[5];

    int ret = ddc_write_intel(h, cmd, sizeof(cmd));
    if (ret == 0) {
        int value = (hi_byte << 8) | lo_byte;
        h->current_brightness = value;
    }
    return (ret == 0) ? DDC_OK : DDC_ERROR;
}

#elif defined(__arm64__)

/* Private IOAVService APIs for Apple Silicon */
typedef struct __IOAVService *IOAVServiceRef;
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern int IOAVServiceReadI2C(IOAVServiceRef service, uint32_t chipAddress, uint32_t offset, void *buffer, uint32_t length);
extern int IOAVServiceWriteI2C(IOAVServiceRef service, uint32_t chipAddress, uint32_t dataAddress, void *buffer, uint32_t length);

#define ARM64_DDC_7BIT_ADDRESS 0x37
#define ARM64_DDC_DATA_ADDRESS 0x51

struct DDC_Display_Handle_s {
    union {
        struct {
            IOAVServiceRef avservice;
        } arm64;
    } data;
    int max_brightness;
    int current_brightness;
};

static DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    (void)dref; /* currently unused for arm64 discovery */
    /* Apple Silicon method (IOAVService via IORegistry) */
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

static DDC_Status ddc_close_arch(DDC_Display_Handle handle) {
    if (handle->data.arm64.avservice) {
        CFRelease(handle->data.arm64.avservice);
    }
    free(handle);
    return DDC_OK;
}

static DDC_Status ddc_get_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
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

static DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    (void)feature_code;
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
        int value = (hi_byte << 8) | lo_byte;
        h->current_brightness = value;
        return DDC_OK;
    }
    return DDC_ERROR;
}

#else

#error Unsupported macOS architecture for dimmit

#endif

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

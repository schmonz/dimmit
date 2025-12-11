#if defined(__arm64__)

#include "ddc_impl_darwin_arch.h"
#include "ddc.h"

#include <stdlib.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

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

DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    (void)dref; /* currently unused for arm64 discovery */
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

DDC_Status ddc_close_arch(DDC_Display_Handle handle) {
    if (handle->data.arm64.avservice) {
        CFRelease(handle->data.arm64.avservice);
    }
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_get_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
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

DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
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

#endif

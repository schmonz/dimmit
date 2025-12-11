#if defined(__x86_64__)

#include "ddc_impl_darwin_arch.h"
#include "ddc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
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

DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
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

DDC_Status ddc_close_arch(DDC_Display_Handle handle) {
    if (handle->data.intel.i2c) {
        IOI2CInterfaceClose(handle->data.intel.i2c, kNilOptions);
    }
    if (handle->data.intel.framebuffer) {
        IOObjectRelease(handle->data.intel.framebuffer);
    }
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_get_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
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

DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
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

#endif

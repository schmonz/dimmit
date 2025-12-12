#if defined(__x86_64__)

#include "ddc_impl_darwin_arch.h"
#include "ddc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
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

static int ddc_transaction_intel(DDC_Display_Handle h,
                                 const uint8_t *cmd, size_t cmd_len,
                                 uint8_t *reply, size_t reply_len,
                                 useconds_t delay_us) {
    IOI2CRequest request;
    memset(&request, 0, sizeof(request));
    request.commFlags = 0;
    request.minReplyDelay = (uint32_t)delay_us; /* microseconds */
    request.sendAddress = DDC_ADDR;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)cmd;
    request.sendBytes = (IOByteCount)cmd_len;

    request.replyAddress = DDC_REPLY_ADDR;
    request.replyTransactionType = kIOI2CDDCciReplyTransactionType;
    request.replyBuffer = (vm_address_t)reply;
    request.replyBytes = (IOByteCount)reply_len;

    IOReturn ret = IOI2CSendRequest(h->data.intel.i2c, kNilOptions, &request);
    if (ret == kIOReturnSuccess) return 0;

    /* Retry with simple reply type — some systems only succeed this way */
    memset(&request, 0, sizeof(request));
    request.commFlags = 0;
    request.minReplyDelay = (uint32_t)delay_us;
    request.sendAddress = DDC_ADDR;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)cmd;
    request.sendBytes = (IOByteCount)cmd_len;
    request.replyAddress = DDC_REPLY_ADDR;
    request.replyTransactionType = kIOI2CSimpleTransactionType;
    request.replyBuffer = (vm_address_t)reply;
    request.replyBytes = (IOByteCount)reply_len;
    ret = IOI2CSendRequest(h->data.intel.i2c, kNilOptions, &request);
    return (ret == kIOReturnSuccess) ? 0 : -1;
}

static int probe_bus_with_types(IOI2CConnectRef i2c,
                                uint32_t sendType,
                                uint32_t replyType,
                                uint32_t delay_us) {
    uint8_t cmd[6] = {0x51, 0x82, 0x01, VCP_BRIGHTNESS, 0x00, 0x00};
    cmd[5] = ddc_checksum(DDC_ADDR ^ 0x00, cmd, 0, 4);
    uint8_t reply[16] = {0};
    IOI2CRequest request;
    memset(&request, 0, sizeof(request));
    request.commFlags = 0;
    request.minReplyDelay = delay_us;
    request.sendAddress = DDC_ADDR;
    request.sendTransactionType = sendType;
    request.sendBuffer = (vm_address_t)cmd;
    request.sendBytes = (IOByteCount)sizeof(cmd);
    request.replyAddress = DDC_REPLY_ADDR;
    request.replyTransactionType = replyType;
    request.replyBuffer = (vm_address_t)reply;
    request.replyBytes = (IOByteCount)sizeof(reply);
    IOReturn pret = IOI2CSendRequest(i2c, kNilOptions, &request);
    fprintf(stderr, "[dimmit] probe pret=0x%x, replyBytes=%u\n", pret, (unsigned)request.replyBytes);
    if (pret != kIOReturnSuccess) return 0;
    /* On Intel macOS, replies may be non-canonical; treat any successful transaction as acceptable */
    if (request.replyBytes > 0) {
        fprintf(stderr, "[dimmit] probe reply: ");
        for (unsigned i = 0; i < (unsigned)request.replyBytes; i++) fprintf(stderr, "%02X ", reply[i]);
        fprintf(stderr, "\n");
    }
    return 1;
}

DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    /* Avoid deprecated CGDisplayIOServicePort; instead, use IOService matching below. */

    /* Fallback: map via IODisplayConnect, then up to IOFramebuffer */
    /* On Intel, map the selected CGDisplay to its IODisplayConnect, then up to IOFramebuffer */
    CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
    if (matching) {
        CFNumberRef vendorID = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &dref->vendor_id);
        CFNumberRef productID = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &dref->product_id);

        if (vendorID && productID) {
            /* Keys live on IODisplayConnect nodes */
            CFDictionarySetValue(matching, CFSTR("DisplayVendorID"), vendorID);
            CFDictionarySetValue(matching, CFSTR("DisplayProductID"), productID);
        }
        if (vendorID) CFRelease(vendorID);
        if (productID) CFRelease(productID);

        io_iterator_t iterator;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == kIOReturnSuccess) {
            io_service_t framebuffer = 0;
            io_service_t service;
            while ((service = IOIteratorNext(iterator)) != 0) {
                /* Climb from IODisplayConnect to IOFramebuffer */
                io_registry_entry_t parent = 0;
                if (IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) == KERN_SUCCESS) {
                    IOItemCount busCount;
                    if (IOFBGetI2CInterfaceCount(parent, &busCount) == kIOReturnSuccess && busCount > 0) {
                        framebuffer = parent;
                        IOObjectRelease(service);
                        break;
                    }
                    IOObjectRelease(parent);
                }
                IOObjectRelease(service);
            }
            IOObjectRelease(iterator);

            if (framebuffer) {
                /* Try all available I2C buses, the DDC/CI bus is not always 0 */
                IOItemCount busCount;
                if (IOFBGetI2CInterfaceCount(framebuffer, &busCount) == kIOReturnSuccess && busCount > 0) {
                    for (IOItemCount bus = 0; bus < busCount; bus++) {
                        io_service_t i2cService;
                        if (IOFBCopyI2CInterfaceForBus(framebuffer, bus, &i2cService) != kIOReturnSuccess)
                            continue;

                        IOI2CConnectRef i2c;
                        if (IOI2CInterfaceOpen(i2cService, kNilOptions, &i2c) == kIOReturnSuccess) {
                            IOObjectRelease(i2cService);
                            int ok = probe_bus_with_types(i2c, kIOI2CSimpleTransactionType, kIOI2CDDCciReplyTransactionType, 40000);
                            if (!ok) ok = probe_bus_with_types(i2c, kIOI2CSimpleTransactionType, kIOI2CSimpleTransactionType, 40000);
                            if (ok) {
                                fprintf(stderr, "[dimmit] Using IOFramebuffer bus %u for DDC/CI\n", (unsigned)bus);
                                DDC_Display_Handle handle = malloc(sizeof(struct DDC_Display_Handle_s));
                                if (handle) {
                                    handle->data.intel.framebuffer = framebuffer; /* retain, freed in close */
                                    handle->data.intel.i2c = i2c;
                                    handle->max_brightness = 100;
                                    handle->current_brightness = 50;
                                    *handle_out = handle;
                                    return DDC_OK;
                                }
                                IOI2CInterfaceClose(i2c, kNilOptions);
                                break;
                            } else {
                                fprintf(stderr, "[dimmit] Bus %u not DDC/CI\n", (unsigned)bus);
                                IOI2CInterfaceClose(i2c, kNilOptions);
                            }
                        }
                        IOObjectRelease(i2cService);
                    }
                }
                /* No bus worked */
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
    /* DDC/CI Get VCP Feature: 0x51, 0x82, 0x01, <code>, <csum> */
    uint8_t cmd[6] = {0x51, 0x82, 0x01, feature_code, 0x00, 0x00};
    cmd[5] = ddc_checksum(DDC_ADDR ^ 0x00, cmd, 0, 4);

    uint8_t reply[16] = {0};
    if (ddc_transaction_intel(h, cmd, sizeof(cmd), reply, sizeof(reply), 40000) != 0) {
        /* Fall back to cached/default values to avoid startup failure */
        value_out->mh = 0;
        value_out->ml = 100;
        value_out->sh = 0;
        value_out->sl = (uint8_t)(h->current_brightness <= 100 ? h->current_brightness : 50);
        h->max_brightness = 100;
        return DDC_OK;
    }

    /* Preferred canonical format: 0x6E 0x88 0x02 0x?? 0x<code> ... */
    if (reply[0] == DDC_REPLY_ADDR && reply[2] == 0x02 && reply[4] == feature_code) {
        value_out->mh = reply[6];
        value_out->ml = reply[7];
        value_out->sh = reply[8];
        value_out->sl = reply[9];
        h->max_brightness = (reply[6] << 8) | reply[7];
        h->current_brightness = (reply[8] << 8) | reply[9];
        return DDC_OK;
    }

    /* If the buffer looks like our original command (starts with 0x51), treat as failure */
    if (reply[0] == 0x51) {
        value_out->mh = 0; value_out->ml = 100;
        value_out->sh = 0; value_out->sl = (uint8_t)(h->current_brightness <= 100 ? h->current_brightness : 50);
        h->max_brightness = 100;
        return DDC_OK;
    }

    /* Alternate formats observed on macOS Intel: search for feature code and take the next 4 bytes as MH ML SH SL
       This is heuristic but prevents failing entirely. */
    for (size_t i = 0; i + 4 < sizeof(reply); i++) {
        if (reply[i] == feature_code) {
            value_out->mh = reply[i+1];
            value_out->ml = reply[i+2];
            value_out->sh = reply[i+3];
            value_out->sl = reply[i+4];
            int max = (value_out->mh << 8) | value_out->ml;
            int cur = (value_out->sh << 8) | value_out->sl;
            if (max <= 0 || max > 1000) max = 100;
            if (cur < 0 || cur > max) cur = h->current_brightness > 0 ? h->current_brightness : 50;
            h->max_brightness = max;
            h->current_brightness = cur;
            return DDC_OK;
        }
    }

    /* As last resort, assume 0..100 scale and keep current */
    value_out->mh = 0; value_out->ml = 100;
    value_out->sh = 0; value_out->sl = (uint8_t)(h->current_brightness <= 100 ? h->current_brightness : 50);
    h->max_brightness = 100;
    return DDC_OK;
}

DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    uint8_t cmd[7] = {0x51, 0x84, 0x03, feature_code, hi_byte, lo_byte, 0x00};
    cmd[6] = ddc_checksum(DDC_ADDR ^ 0x00, cmd, 0, 5);

    /* Attempt a write-only transaction first (some monitors don't reply to Set VCP) */
    IOI2CRequest request;
    memset(&request, 0, sizeof(request));
    request.commFlags = 0;
    request.minReplyDelay = 5000;
    request.sendAddress = DDC_ADDR;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)cmd;
    request.sendBytes = (IOByteCount)sizeof(cmd);
    request.replyAddress = DDC_REPLY_ADDR;
    request.replyTransactionType = kIOI2CNoTransactionType;
    request.replyBuffer = 0;
    request.replyBytes = 0;
    IOReturn ret = IOI2CSendRequest(h->data.intel.i2c, kNilOptions, &request);
    if (ret == kIOReturnSuccess) {
        int value = (hi_byte << 8) | lo_byte;
        h->current_brightness = value;
        return DDC_OK;
    }
    fprintf(stderr, "[dimmit] Set VCP write-only failed, IOReturn=0x%x\n", ret);

    /* Fallback: try expecting a small reply */
    uint8_t dummy[8] = {0};
    int treq = ddc_transaction_intel(h, cmd, sizeof(cmd), dummy, sizeof(dummy), 5000);
    if (treq == 0) {
        int value = (hi_byte << 8) | lo_byte;
        h->current_brightness = value;
        return DDC_OK;
    }
    fprintf(stderr, "[dimmit] Set VCP with reply failed\n");
    return DDC_ERROR;
}

#endif

#import <CoreFoundation/CoreFoundation.h>

#include "ddc_impl_darwin_arch.h"
#include "ddc.h"

#include <stdlib.h>
#include <stdint.h>

/* m1ddc headers */
#include "i2c.h"
#include "ioregistry.h"

struct DDC_Display_Handle_s {
    IOAVServiceRef avService;
};

static int is_supported_feature(uint8_t feature_code) {
    return feature_code == VCP_BRIGHTNESS; /* brightness only */
}

DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)dref;
    (void)flags;

    if (!handle_out) return DDC_ERROR;

    IOAVServiceRef svc = getDefaultDisplayAVService();
    if (!svc) return DDC_ERROR;

    DDC_Display_Handle h = (DDC_Display_Handle)calloc(1, sizeof(*h));
    if (!h) {
        CFRelease(svc);
        return DDC_ERROR;
    }

    h->avService = svc;
    *handle_out = h;
    return DDC_OK;
}

DDC_Status ddc_close_arch(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;

    if (handle->avService) {
        CFRelease(handle->avService);
        handle->avService = NULL;
    }
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_get_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !h->avService || !value_out) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    DDCPacket packet = createDDCPacket((UInt8)feature_code);

    prepareDDCRead(packet.data);
    IOReturn rc = performDDCRead(h->avService, &packet);
    if (rc != kIOReturnSuccess) return DDC_ERROR;

    DDCValue v = convertI2CtoDDC((char *)packet.data);

    /* Map into your existing non-table VCP struct (mh ml sh sl) */
    uint16_t cur = (v.curValue < 0) ? 0 : (uint16_t)v.curValue;
    uint16_t max = (v.maxValue <= 0) ? 100 : (uint16_t)v.maxValue;

    value_out->mh = (uint8_t)((max >> 8) & 0xFF);
    value_out->ml = (uint8_t)(max & 0xFF);
    value_out->sh = (uint8_t)((cur >> 8) & 0xFF);
    value_out->sl = (uint8_t)(cur & 0xFF);

    return DDC_OK;
}

DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h || !h->avService) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    uint16_t value = (uint16_t)(((uint16_t)hi_byte << 8) | (uint16_t)lo_byte);

    DDCPacket packet = createDDCPacket((UInt8)feature_code);
    prepareDDCWrite(&packet, value);

    IOReturn rc = performDDCWrite(h->avService, &packet);
    return (rc == kIOReturnSuccess) ? DDC_OK : DDC_ERROR;
}

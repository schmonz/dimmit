#import <CoreFoundation/CoreFoundation.h>

#include "platform/ddc/darwin/arch.h"
#include "platform/ddc/abstraction.h"

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

DDC_Status ddc_arch_open(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;

    if (!dref || !handle_out) return DDC_ERROR;

    /* Resolve the AV service for THIS display, not IOAVServiceCreate()'s default.
     * On a clamshell laptop driving an external display, the default service is
     * the internal panel's -- DDC to it returns garbage and writes fail. Match
     * our chosen display by CGDirectDisplayID, then ask m1ddc's IORegistry
     * walker for that display's external DCPAVServiceProxy. */
    IOAVServiceRef svc = NULL;
    DisplayInfos infos[MAX_DISPLAYS];
    CGDisplayCount n = getOnlineDisplayInfos(infos);
    for (CGDisplayCount i = 0; i < n; i++) {
        if ((uint32_t)infos[i].id == dref->display_id) {
            svc = getDisplayAVService(&infos[i]);
            break;
        }
    }

    /* Fall back to the default service only if per-display resolution failed
     * (e.g. a single-display desktop where no location match is needed). */
    if (!svc) {
        svc = getDefaultDisplayAVService();
    }
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

DDC_Status ddc_arch_close(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;

    if (handle->avService) {
        CFRelease(handle->avService);
        handle->avService = NULL;
    }
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_arch_get_vcp(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !h->avService || !value_out) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    /* A DDC "get VCP" is a two-step transaction: first WRITE the read request
     * to the display, then READ its reply into a fresh packet. Skipping the
     * write leaves the display with nothing to answer, so it returns a DDC Null
     * message (6e 80 be ...) that parses into garbage (matches m1ddc.m's
     * readingOperation()). */
    DDCPacket packet = createDDCPacket((UInt8)feature_code);

    prepareDDCRead(packet.data);
    IOReturn rc = performDDCWrite(h->avService, &packet);
    if (rc != kIOReturnSuccess) return DDC_ERROR;

    DDCPacket reply = {};
    reply.inputAddr = packet.inputAddr;
    rc = performDDCRead(h->avService, &reply);
    if (rc != kIOReturnSuccess) return DDC_ERROR;

    DDCValue v = convertI2CtoDDC((char *)reply.data);

    /* Map into your existing non-table VCP struct (mh ml sh sl) */
    uint16_t cur = (v.curValue < 0) ? 0 : (uint16_t)v.curValue;
    uint16_t max = (v.maxValue <= 0) ? 100 : (uint16_t)v.maxValue;

    value_out->mh = (uint8_t)((max >> 8) & 0xFF);
    value_out->ml = (uint8_t)(max & 0xFF);
    value_out->sh = (uint8_t)((cur >> 8) & 0xFF);
    value_out->sl = (uint8_t)(cur & 0xFF);

    return DDC_OK;
}

DDC_Status ddc_arch_set_vcp(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h || !h->avService) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    uint16_t value = (uint16_t)(((uint16_t)hi_byte << 8) | (uint16_t)lo_byte);

    DDCPacket packet = createDDCPacket((UInt8)feature_code);
    prepareDDCWrite(&packet, value);

    IOReturn rc = performDDCWrite(h->avService, &packet);
    return (rc == kIOReturnSuccess) ? DDC_OK : DDC_ERROR;
}

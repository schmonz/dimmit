#include "ddc_impl_darwin_arch.h"
#include "ddc.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/IOKitLib.h>

/* ddcctl header (must be unambiguous on case-insensitive filesystems) */
#include "vendor/ddcctl/src/DDC.h"

struct DDC_Display_Handle_s {
    CGDirectDisplayID display_id;
    io_service_t framebuffer;
};

static int is_supported_feature(uint8_t feature_code) {
    return feature_code == VCP_BRIGHTNESS; /* brightness only */
}

DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;

    if (!dref || !handle_out) return DDC_ERROR;

    DDC_Display_Handle h = (DDC_Display_Handle)calloc(1, sizeof(struct DDC_Display_Handle_s));
    if (!h) return DDC_ERROR;

    h->display_id = (CGDirectDisplayID)dref->display_id;

    /* Deprecated, but fine for Intel-only builds; matches ddcctl’s approach. */
    h->framebuffer = CGDisplayIOServicePort(h->display_id);
    if (!h->framebuffer) {
        free(h);
        return DDC_ERROR;
    }

    *handle_out = h;
    return DDC_OK;
}

DDC_Status ddc_close_arch(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_get_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !value_out) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    struct DDCReadCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.control_id = (UInt8)feature_code;
    cmd.success = false;

    if (!DDCRead(h->framebuffer, &cmd) || !cmd.success) {
        return DDC_ERROR;
    }

    value_out->mh = 0;
    value_out->ml = cmd.max_value;
    value_out->sh = 0;
    value_out->sl = cmd.current_value;
    return DDC_OK;
}

DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    unsigned int value16 = ((unsigned int)hi_byte << 8) | (unsigned int)lo_byte;
    UInt8 value8 = (value16 > 255u) ? 255u : (UInt8)value16;

    struct DDCWriteCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.control_id = (UInt8)feature_code;
    cmd.new_value = value8;

    if (!DDCWrite(h->framebuffer, &cmd)) {
        return DDC_ERROR;
    }

    return DDC_OK;
}

/* ddc_checksum is provided elsewhere (ddc_impl_darwin.c). Do not define it here. */
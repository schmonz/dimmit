#include "platform/ddc/darwin/arch.h"
#include "platform/ddc/abstraction.h"

#include <stdio.h>
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
    CGDirectDisplayID display_id;  /* stable anchor; the port is re-resolved from it
                                      per operation (see resolve_framebuffer). */
    io_service_t last_seen;        /* diagnostic only: last resolved port, to detect
                                      and log a republished framebuffer. Never used
                                      to drive DDC. */
};

static int is_supported_feature(uint8_t feature_code) {
    return feature_code == VCP_BRIGHTNESS; /* brightness only */
}

/* Resolve the IOFramebuffer service for this handle's display, fresh on every
 * call rather than caching it: CGDisplayIOServicePort can in principle return a
 * stale port after a display reconfiguration (sleep/wake/resolution/replug). The
 * lookup is an IORegistry hit, negligible next to the I2C transaction, and the
 * port is a borrowed reference (Get Rule: retain count stays flat across calls,
 * CG owns it) so re-resolving neither leaks nor needs IOObjectRelease.
 *
 * Diagnostic: if the port differs from the one we last resolved, the framebuffer
 * was republished between commands and the cached port would have been stale --
 * i.e. the per-command re-resolve just earned its keep. We have not observed this
 * on the verified hardware, so we log only on change to find out if it ever
 * happens; normal use stays silent. Single-writer in practice, so last_seen needs
 * no locking: the one init-time resolve (via get_vcp, from init_monitor) runs
 * before the worker thread is created; thereafter only the worker resolves (via
 * set_vcp). open() validates the display but deliberately does not prime
 * last_seen, so the first resolve never logs a change. */
static io_service_t resolve_framebuffer(DDC_Display_Handle h) {
    io_service_t fb = CGDisplayIOServicePort(h->display_id);
    if (fb && h->last_seen && fb != h->last_seen) {
        printf("DDC: framebuffer port for display %u changed %u -> %u "
               "(reconfigured; cached port would have been stale)\n",
               (unsigned)h->display_id, (unsigned)h->last_seen, (unsigned)fb);
    }
    if (fb) h->last_seen = fb;
    return fb;
}

DDC_Status ddc_arch_open(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;

    if (!dref || !handle_out) return DDC_ERROR;

    DDC_Display_Handle h = (DDC_Display_Handle)calloc(1, sizeof(struct DDC_Display_Handle_s));
    if (!h) return DDC_ERROR;

    h->display_id = (CGDirectDisplayID)dref->display_id;

    /* Validate that the display resolves now (so a bogus/builtin display fails
     * early), but do not cache the result — it is re-resolved per operation.
     * Deprecated, but fine for Intel-only builds; matches ddcctl’s approach. */
    if (!CGDisplayIOServicePort(h->display_id)) {
        free(h);
        return DDC_ERROR;
    }

    *handle_out = h;
    return DDC_OK;
}

DDC_Status ddc_arch_close(DDC_Display_Handle handle) {
    if (!handle) return DDC_ERROR;
    free(handle);
    return DDC_OK;
}

DDC_Status ddc_arch_get_vcp(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    if (!h || !value_out) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    io_service_t framebuffer = resolve_framebuffer(h);
    if (!framebuffer) return DDC_ERROR;

    struct DDCReadCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.control_id = (UInt8)feature_code;
    cmd.success = false;

    if (!DDCRead(framebuffer, &cmd) || !cmd.success) {
        return DDC_ERROR;
    }

    value_out->mh = 0;
    value_out->ml = cmd.max_value;
    value_out->sh = 0;
    value_out->sl = cmd.current_value;
    return DDC_OK;
}

DDC_Status ddc_arch_set_vcp(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    if (!h) return DDC_ERROR;
    if (!is_supported_feature(feature_code)) return DDC_ERROR;

    unsigned int value16 = ((unsigned int)hi_byte << 8) | (unsigned int)lo_byte;
    UInt8 value8 = (value16 > 255u) ? 255u : (UInt8)value16;

    io_service_t framebuffer = resolve_framebuffer(h);
    if (!framebuffer) return DDC_ERROR;

    struct DDCWriteCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.control_id = (UInt8)feature_code;
    cmd.new_value = value8;

    if (!DDCWrite(framebuffer, &cmd)) {
        return DDC_ERROR;
    }

    return DDC_OK;
}

/* ddc_arch_checksum is provided elsewhere (ddc/darwin.c). Do not define it here. */

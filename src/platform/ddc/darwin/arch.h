#ifndef DDC_DARWIN_ARCH_H
#define DDC_DARWIN_ARCH_H

#include <stdint.h>
#include "platform/ddc/implementation.h"

struct DDC_Display_Ref_s {
    uint32_t display_id;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
};

DDC_Status ddc_arch_open(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out);
DDC_Status ddc_arch_close(DDC_Display_Handle handle);
DDC_Status ddc_arch_get_vcp(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out);
DDC_Status ddc_arch_set_vcp(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte);

uint8_t ddc_arch_checksum(uint8_t chk, uint8_t *data, int start, int end);

#endif /* DDC_DARWIN_ARCH_H */

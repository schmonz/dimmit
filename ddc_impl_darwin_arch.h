#ifndef DDC_IMPL_DARWIN_ARCH_H
#define DDC_IMPL_DARWIN_ARCH_H

#include <stdint.h>
#include "ddc_impl.h"

struct DDC_Display_Ref_s {
    uint32_t display_id;
    uint32_t vendor_id;
    uint32_t product_id;
    int is_builtin;
};

DDC_Status ddc_open_arch(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out);
DDC_Status ddc_close_arch(DDC_Display_Handle handle);
DDC_Status ddc_get_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out);
DDC_Status ddc_set_vcp_arch(DDC_Display_Handle h, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte);

uint8_t ddc_checksum(uint8_t chk, uint8_t *data, int start, int end);

#endif /* DDC_IMPL_DARWIN_ARCH_H */

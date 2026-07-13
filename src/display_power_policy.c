#include "walldisplay/display_power_policy.h"

uint8_t display_power_policy_brightness(uint32_t elapsed_s,
                                        uint32_t dim_after_s,
                                        uint32_t off_after_s,
                                        uint8_t dim_percent) {
    if (dim_after_s == 0 || elapsed_s < dim_after_s) {
        return 100;
    }
    if (off_after_s > 0 && elapsed_s - dim_after_s >= off_after_s) {
        return 0;
    }
    return dim_percent;
}

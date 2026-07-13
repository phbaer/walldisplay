#pragma once

#include <stdint.h>

/**
 * Return the requested backlight percentage for an inactivity duration.
 * A dim timeout of zero disables both dimming and display-off behavior.
 */
uint8_t display_power_policy_brightness(uint32_t elapsed_s,
                                        uint32_t dim_after_s,
                                        uint32_t off_after_s,
                                        uint8_t dim_percent);

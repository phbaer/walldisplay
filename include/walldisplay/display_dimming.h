#pragma once

#include "esp_err.h"

#include <stdint.h>

/** Start inactivity-based display dimming with sensible defaults. */
esp_err_t display_dimming_init(void);

/** Apply timeouts in seconds. A value of zero disables the corresponding step. */
esp_err_t display_dimming_set_config(uint32_t dim_after_s, uint32_t off_after_s, uint8_t dim_percent);

/** Reset the inactivity timer and restore full display brightness. */
void display_dimming_wake(void);

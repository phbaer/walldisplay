#pragma once

#include "esp_err.h"

esp_err_t media_artwork_init(void);
esp_err_t media_artwork_request(const char *url);

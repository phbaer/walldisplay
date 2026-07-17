#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t media_artwork_init(void);
esp_err_t media_artwork_request(const char *url);

#ifdef __cplusplus
}
#endif

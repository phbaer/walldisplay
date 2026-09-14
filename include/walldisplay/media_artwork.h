#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t media_artwork_init(void);
esp_err_t media_artwork_request(const char *url);
/* Cancel an in-flight download before a memory-intensive operation. */
esp_err_t media_artwork_cancel(void);

#ifdef __cplusplus
}
#endif

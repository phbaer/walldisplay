#pragma once

#include "esp_err.h"
#include "esp_netif.h"

/** Start the HTTP setup portal and captive DNS for clients of ap_netif.
 * Starting an already running portal only resets its request rate limit. */
esp_err_t setup_portal_start(esp_netif_t *ap_netif);
/** Stop the portal and its captive DNS. Safe when the portal is not running. */
void setup_portal_stop(void);

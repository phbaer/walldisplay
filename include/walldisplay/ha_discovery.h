#pragma once

#include "esp_err.h"
#include "mqtt_client.h"

esp_err_t ha_discovery_publish_all(esp_mqtt_client_handle_t client);

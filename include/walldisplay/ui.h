#pragma once

#include "esp_err.h"
#include "display_board.h"

#include <stddef.h>

esp_err_t ui_init(const display_board_handle_t *board);
esp_err_t ui_set_connection_status(const char *status_text);
esp_err_t ui_set_title_text(const char *title_text);
esp_err_t ui_set_weather_text(const char *weather_text);
esp_err_t ui_set_media_text(const char *media_text);
esp_err_t ui_set_clock_text(const char *clock_text);
esp_err_t ui_set_date_text(const char *date_text);
esp_err_t ui_set_wifi_state(const char *state_text);
esp_err_t ui_set_mqtt_state(const char *state_text);
esp_err_t ui_set_ha_state(const char *state_text);
esp_err_t ui_set_button_label(size_t index, const char *label_text);
esp_err_t ui_set_button_state(size_t index, const char *state_text);
esp_err_t ui_set_measurement_chip(size_t index, const char *chip_text);
esp_err_t ui_set_measurement_chip_color(size_t index, const char *color_text);

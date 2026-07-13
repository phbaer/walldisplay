#include "walldisplay/display_dimming.h"

#include "walldisplay/display_board.h"
#include "walldisplay/display_power_policy.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#define DISPLAY_DIM_DEFAULT_AFTER_S (5 * 60)
#define DISPLAY_DIM_DEFAULT_OFF_AFTER_S (10 * 60)
#define DISPLAY_DIM_DEFAULT_PERCENT 20

static const char *TAG = "display_dimming";
static esp_timer_handle_t s_timer;
static int64_t s_last_activity_us;
static uint32_t s_dim_after_s = DISPLAY_DIM_DEFAULT_AFTER_S;
static uint32_t s_off_after_s = DISPLAY_DIM_DEFAULT_OFF_AFTER_S;
static uint8_t s_dim_percent = DISPLAY_DIM_DEFAULT_PERCENT;
static uint8_t s_applied_percent = 100;

static void apply_brightness(uint8_t percent) {
    if (percent == s_applied_percent) {
        return;
    }
    if (display_board_set_backlight(percent) == ESP_OK) {
        s_applied_percent = percent;
        ESP_LOGI(TAG, "Backlight set to %u%%", (unsigned) percent);
    }
}

static void inactivity_timer_cb(void *arg) {
    (void) arg;
    const int64_t elapsed_s = (esp_timer_get_time() - s_last_activity_us) / 1000000;
    apply_brightness(display_power_policy_brightness((uint32_t) elapsed_s,
                                                     s_dim_after_s,
                                                     s_off_after_s,
                                                     s_dim_percent));
}

esp_err_t display_dimming_init(void) {
    s_last_activity_us = esp_timer_get_time();
    const esp_timer_create_args_t timer_args = {
        .callback = inactivity_timer_cb,
        .name = "display_dimming",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_timer), TAG, "timer create failed");
    return esp_timer_start_periodic(s_timer, 1000000);
}

esp_err_t display_dimming_set_config(uint32_t dim_after_s, uint32_t off_after_s, uint8_t dim_percent) {
    if (dim_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    s_dim_after_s = dim_after_s;
    s_off_after_s = off_after_s;
    s_dim_percent = dim_percent;
    display_dimming_wake();
    return ESP_OK;
}

void display_dimming_wake(void) {
    s_last_activity_us = esp_timer_get_time();
    apply_brightness(100);
}

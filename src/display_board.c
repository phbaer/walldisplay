#include "walldisplay/display_board.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_lcd_st7701.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"

static const char *TAG = "display_board";

#define BOARD_LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)
#define BOARD_LCD_HSYNC_PULSE 8
#define BOARD_LCD_HSYNC_FRONT_PORCH 10
#define BOARD_LCD_HSYNC_BACK_PORCH 20
#define BOARD_LCD_VSYNC_PULSE 8
#define BOARD_LCD_VSYNC_FRONT_PORCH 10
#define BOARD_LCD_VSYNC_BACK_PORCH 10
#define BOARD_LCD_DATA_WIDTH 16
#define BOARD_LCD_BITS_PER_PIXEL 16
#define BOARD_LCD_BOUNCE_BUFFER_HEIGHT 40
#define BOARD_BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define BOARD_BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define BOARD_BACKLIGHT_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define BOARD_BACKLIGHT_LEDC_MAX_DUTY ((1U << 10) - 1U)

static const uint8_t s_cmd_ff_10[] = {0x77, 0x01, 0x00, 0x00, 0x10};
static const uint8_t s_cmd_c0[] = {0x3B, 0x00};
static const uint8_t s_cmd_c1[] = {0x0D, 0x02};
static const uint8_t s_cmd_c2[] = {0x31, 0x05};
static const uint8_t s_cmd_cd[] = {0x00};
static const uint8_t s_cmd_b0[] = {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18};
static const uint8_t s_cmd_b1[] = {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18};
static const uint8_t s_cmd_ff_11[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static const uint8_t s_cmd_b0_11[] = {0x60};
static const uint8_t s_cmd_b1_11[] = {0x32};
static const uint8_t s_cmd_b2_11[] = {0x07};
static const uint8_t s_cmd_b3_11[] = {0x80};
static const uint8_t s_cmd_b5_11[] = {0x49};
static const uint8_t s_cmd_b7_11[] = {0x85};
static const uint8_t s_cmd_b8_11[] = {0x21};
static const uint8_t s_cmd_c1_11[] = {0x78};
static const uint8_t s_cmd_c2_11[] = {0x78};
static const uint8_t s_cmd_e0[] = {0x00, 0x1B, 0x02};
static const uint8_t s_cmd_e1[] = {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44};
static const uint8_t s_cmd_e2[] = {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00};
static const uint8_t s_cmd_e3[] = {0x00, 0x00, 0x11, 0x11};
static const uint8_t s_cmd_e4[] = {0x44, 0x44};
static const uint8_t s_cmd_e5[] = {0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0};
static const uint8_t s_cmd_e6[] = {0x00, 0x00, 0x11, 0x11};
static const uint8_t s_cmd_e7[] = {0x44, 0x44};
static const uint8_t s_cmd_e8[] = {0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0};
static const uint8_t s_cmd_eb[] = {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40};
static const uint8_t s_cmd_ec[] = {0x3C, 0x00};
static const uint8_t s_cmd_ed[] = {0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA};
static const uint8_t s_cmd_ff_13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
static const uint8_t s_cmd_e5_13[] = {0xE4};
static const uint8_t s_cmd_ff_00[] = {0x77, 0x01, 0x00, 0x00, 0x00};
static const uint8_t s_cmd_3a[] = {0x60};

static const st7701_lcd_init_cmd_t s_st7701_init_cmds[] = {
    {.cmd = 0xFF, .data = s_cmd_ff_10, .data_bytes = sizeof(s_cmd_ff_10), .delay_ms = 0},
    {.cmd = 0xC0, .data = s_cmd_c0, .data_bytes = sizeof(s_cmd_c0), .delay_ms = 0},
    {.cmd = 0xC1, .data = s_cmd_c1, .data_bytes = sizeof(s_cmd_c1), .delay_ms = 0},
    {.cmd = 0xC2, .data = s_cmd_c2, .data_bytes = sizeof(s_cmd_c2), .delay_ms = 0},
    {.cmd = 0xCD, .data = s_cmd_cd, .data_bytes = sizeof(s_cmd_cd), .delay_ms = 0},
    {.cmd = 0xB0, .data = s_cmd_b0, .data_bytes = sizeof(s_cmd_b0), .delay_ms = 0},
    {.cmd = 0xB1, .data = s_cmd_b1, .data_bytes = sizeof(s_cmd_b1), .delay_ms = 0},
    {.cmd = 0xFF, .data = s_cmd_ff_11, .data_bytes = sizeof(s_cmd_ff_11), .delay_ms = 0},
    {.cmd = 0xB0, .data = s_cmd_b0_11, .data_bytes = sizeof(s_cmd_b0_11), .delay_ms = 0},
    {.cmd = 0xB1, .data = s_cmd_b1_11, .data_bytes = sizeof(s_cmd_b1_11), .delay_ms = 0},
    {.cmd = 0xB2, .data = s_cmd_b2_11, .data_bytes = sizeof(s_cmd_b2_11), .delay_ms = 0},
    {.cmd = 0xB3, .data = s_cmd_b3_11, .data_bytes = sizeof(s_cmd_b3_11), .delay_ms = 0},
    {.cmd = 0xB5, .data = s_cmd_b5_11, .data_bytes = sizeof(s_cmd_b5_11), .delay_ms = 0},
    {.cmd = 0xB7, .data = s_cmd_b7_11, .data_bytes = sizeof(s_cmd_b7_11), .delay_ms = 0},
    {.cmd = 0xB8, .data = s_cmd_b8_11, .data_bytes = sizeof(s_cmd_b8_11), .delay_ms = 0},
    {.cmd = 0xC1, .data = s_cmd_c1_11, .data_bytes = sizeof(s_cmd_c1_11), .delay_ms = 0},
    {.cmd = 0xC2, .data = s_cmd_c2_11, .data_bytes = sizeof(s_cmd_c2_11), .delay_ms = 0},
    {.cmd = 0xE0, .data = s_cmd_e0, .data_bytes = sizeof(s_cmd_e0), .delay_ms = 0},
    {.cmd = 0xE1, .data = s_cmd_e1, .data_bytes = sizeof(s_cmd_e1), .delay_ms = 0},
    {.cmd = 0xE2, .data = s_cmd_e2, .data_bytes = sizeof(s_cmd_e2), .delay_ms = 0},
    {.cmd = 0xE3, .data = s_cmd_e3, .data_bytes = sizeof(s_cmd_e3), .delay_ms = 0},
    {.cmd = 0xE4, .data = s_cmd_e4, .data_bytes = sizeof(s_cmd_e4), .delay_ms = 0},
    {.cmd = 0xE5, .data = s_cmd_e5, .data_bytes = sizeof(s_cmd_e5), .delay_ms = 0},
    {.cmd = 0xE6, .data = s_cmd_e6, .data_bytes = sizeof(s_cmd_e6), .delay_ms = 0},
    {.cmd = 0xE7, .data = s_cmd_e7, .data_bytes = sizeof(s_cmd_e7), .delay_ms = 0},
    {.cmd = 0xE8, .data = s_cmd_e8, .data_bytes = sizeof(s_cmd_e8), .delay_ms = 0},
    {.cmd = 0xEB, .data = s_cmd_eb, .data_bytes = sizeof(s_cmd_eb), .delay_ms = 0},
    {.cmd = 0xEC, .data = s_cmd_ec, .data_bytes = sizeof(s_cmd_ec), .delay_ms = 0},
    {.cmd = 0xED, .data = s_cmd_ed, .data_bytes = sizeof(s_cmd_ed), .delay_ms = 0},
    {.cmd = 0xFF, .data = s_cmd_ff_13, .data_bytes = sizeof(s_cmd_ff_13), .delay_ms = 0},
    {.cmd = 0xE5, .data = s_cmd_e5_13, .data_bytes = sizeof(s_cmd_e5_13), .delay_ms = 0},
    {.cmd = 0xFF, .data = s_cmd_ff_00, .data_bytes = sizeof(s_cmd_ff_00), .delay_ms = 0},
    {.cmd = 0x21, .data = NULL, .data_bytes = 0, .delay_ms = 0},
    {.cmd = 0x3A, .data = s_cmd_3a, .data_bytes = sizeof(s_cmd_3a), .delay_ms = 0},
    {.cmd = 0x11, .data = NULL, .data_bytes = 0, .delay_ms = 120},
    {.cmd = 0x29, .data = NULL, .data_bytes = 0, .delay_ms = 20},
    {.cmd = 0x20, .data = NULL, .data_bytes = 0, .delay_ms = 0},
};

static esp_lcd_panel_io_handle_t s_panel_io_handle;
static i2c_master_bus_handle_t s_touch_i2c_bus;
static bool s_lvgl_port_initialized;

static esp_err_t init_lvgl_port(void) {
    if (s_lvgl_port_initialized) {
        return ESP_OK;
    }

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");
    s_lvgl_port_initialized = true;
    return ESP_OK;
}

static esp_err_t init_backlight(void) {
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BOARD_BACKLIGHT_LEDC_DUTY_RES,
        .timer_num = BOARD_BACKLIGHT_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel_config = {
        .gpio_num = BOARD_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BOARD_BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "backlight timer config failed");
    return ledc_channel_config(&channel_config);
}

esp_err_t display_board_set_backlight(uint8_t percent) {
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t duty = (BOARD_BACKLIGHT_LEDC_MAX_DUTY * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL, duty), TAG, "backlight duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL);
}

static esp_err_t init_panel(display_board_handle_t *handle) {
    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_GPIO,
        .cs_gpio_num = BOARD_LCD_SPI_CS,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = BOARD_LCD_SPI_SCK,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = BOARD_LCD_SPI_MOSI,
        .io_expander = NULL,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(&io_config, &s_panel_io_handle), TAG, "3-wire panel IO init failed");

    static esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
            .h_res = BOARD_LCD_WIDTH,
            .v_res = BOARD_LCD_HEIGHT,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .pclk_active_neg = false,
            },
        },
        .data_width = BOARD_LCD_DATA_WIDTH,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .de_gpio_num = BOARD_LCD_DE,
        .pclk_gpio_num = BOARD_LCD_PCLK,
        .vsync_gpio_num = BOARD_LCD_VSYNC,
        .hsync_gpio_num = BOARD_LCD_HSYNC,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            BOARD_LCD_DATA0,
            BOARD_LCD_DATA1,
            BOARD_LCD_DATA2,
            BOARD_LCD_DATA3,
            BOARD_LCD_DATA4,
            BOARD_LCD_DATA5,
            BOARD_LCD_DATA6,
            BOARD_LCD_DATA7,
            BOARD_LCD_DATA8,
            BOARD_LCD_DATA9,
            BOARD_LCD_DATA10,
            BOARD_LCD_DATA11,
            BOARD_LCD_DATA12,
            BOARD_LCD_DATA13,
            BOARD_LCD_DATA14,
            BOARD_LCD_DATA15,
        },
        .flags = {
            .fb_in_psram = 1,
        },
        .num_fbs = 2,
        .bounce_buffer_size_px = BOARD_LCD_WIDTH * BOARD_LCD_BOUNCE_BUFFER_HEIGHT,
        .dma_burst_size = 64,
    };

    static st7701_vendor_config_t vendor_config = {
        .init_cmds = s_st7701_init_cmds,
        .init_cmds_size = sizeof(s_st7701_init_cmds) / sizeof(s_st7701_init_cmds[0]),
        .rgb_config = &rgb_config,
        .flags = {
            .mirror_by_cmd = 0,
            .enable_io_multiplex = 0,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(s_panel_io_handle, &panel_config, &handle->panel_handle), TAG, "ST7701 panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(handle->panel_handle), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(handle->panel_handle), TAG, "panel hw init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(handle->panel_handle, true), TAG, "panel on failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = handle->panel_handle,
        .buffer_size = BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT,
        .hres = BOARD_LCD_WIDTH,
        .vres = BOARD_LCD_HEIGHT,
        .monochrome = false,
        /* Render both RGB frame buffers in the panel's byte order. */
        .color_format = LV_COLOR_FORMAT_RGB565_SWAPPED,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            /* The swapped color format keeps both frame buffers consistent. */
            .swap_bytes = false,
            /*
             * With the RGB panel's two frame buffers, direct mode redraws only
             * changed regions and flips at a frame boundary.  Full refresh
             * repaints the entire screen for each label update and flashes.
             */
            .direct_mode = true,
            .full_refresh = false,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_port_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };

    handle->display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_port_cfg);
    ESP_RETURN_ON_FALSE(handle->display != NULL, ESP_FAIL, TAG, "LVGL display registration failed");

    return ESP_OK;
}

static esp_err_t init_touch(display_board_handle_t *handle) {
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BOARD_TOUCH_SDA,
        .scl_io_num = BOARD_TOUCH_SCL,
        .i2c_port = I2C_NUM_0,
        .glitch_ignore_cnt = 7,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &s_touch_i2c_bus), TAG, "touch i2c bus init failed");

    esp_lcd_panel_io_handle_t touch_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_touch_i2c_bus, &tp_io_config, &touch_io_handle), TAG, "touch panel IO init failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_WIDTH,
        .y_max = BOARD_LCD_HEIGHT,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(touch_io_handle, &tp_cfg, &handle->touch_handle), TAG, "GT911 touch init failed");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = handle->display,
        .handle = handle->touch_handle,
        .scale = {
            .x = 1.0f,
            .y = 1.0f,
        },
    };
    handle->touch = lvgl_port_add_touch(&touch_cfg);
    ESP_RETURN_ON_FALSE(handle->touch != NULL, ESP_FAIL, TAG, "LVGL touch registration failed");
    return ESP_OK;
}

esp_err_t display_board_init(display_board_handle_t *handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    handle->display = NULL;
    handle->touch = NULL;
    handle->panel_handle = NULL;
    handle->touch_handle = NULL;

    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight init failed");
    ESP_RETURN_ON_ERROR(init_lvgl_port(), TAG, "lvgl init failed");
    ESP_RETURN_ON_ERROR(init_panel(handle), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(init_touch(handle), TAG, "touch init failed");

    ESP_RETURN_ON_ERROR(display_board_set_backlight(100), TAG, "backlight enable failed");
    ESP_LOGI(TAG, "Board initialized: ST7701 display and GT911 touch are registered with LVGL");
    return ESP_OK;
}

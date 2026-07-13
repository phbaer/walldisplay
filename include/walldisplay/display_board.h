#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#define BOARD_LCD_HSYNC GPIO_NUM_16
#define BOARD_LCD_VSYNC GPIO_NUM_17
#define BOARD_LCD_DE GPIO_NUM_18
#define BOARD_LCD_PCLK GPIO_NUM_21
#define BOARD_LCD_BACKLIGHT GPIO_NUM_38
#define BOARD_LCD_SPI_CS GPIO_NUM_39
#define BOARD_LCD_SPI_SCK GPIO_NUM_48
#define BOARD_LCD_SPI_MOSI GPIO_NUM_47

#define BOARD_LCD_DATA0 GPIO_NUM_46
#define BOARD_LCD_DATA1 GPIO_NUM_9
#define BOARD_LCD_DATA2 GPIO_NUM_10
#define BOARD_LCD_DATA3 GPIO_NUM_11
#define BOARD_LCD_DATA4 GPIO_NUM_12
#define BOARD_LCD_DATA5 GPIO_NUM_13
#define BOARD_LCD_DATA6 GPIO_NUM_14
#define BOARD_LCD_DATA7 GPIO_NUM_0
#define BOARD_LCD_DATA8 GPIO_NUM_4
#define BOARD_LCD_DATA9 GPIO_NUM_5
#define BOARD_LCD_DATA10 GPIO_NUM_6
#define BOARD_LCD_DATA11 GPIO_NUM_7
#define BOARD_LCD_DATA12 GPIO_NUM_15
#define BOARD_LCD_DATA13 GPIO_NUM_8
#define BOARD_LCD_DATA14 GPIO_NUM_20
#define BOARD_LCD_DATA15 GPIO_NUM_3

#define BOARD_TOUCH_SCL GPIO_NUM_45
#define BOARD_TOUCH_SDA GPIO_NUM_19
#define BOARD_TOUCH_RST GPIO_NUM_NC
#define BOARD_TOUCH_INT GPIO_NUM_NC

#define BOARD_LCD_WIDTH 480
#define BOARD_LCD_HEIGHT 480

typedef struct {
    lv_disp_t *display;
    lv_indev_t *touch;
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_touch_handle_t touch_handle;
} display_board_handle_t;

esp_err_t display_board_init(display_board_handle_t *handle);

#pragma once
#include "lvgl.h"
#include "esp_err.h"
#define LCD_MOSI 17
#define LCD_SCLK 16
#define LCD_CS 5
#define LCD_DC 7
#define LCD_RST 6
#define LCD_BL 8
#define TOUCH_RST 9
#define TOUCH_INT 12
#define TOUCH_SDA 10
#define TOUCH_SCL 13
#define MIC_WS 2
#define MIC_SD 1
#define MIC_SCK 42
#define LED_PIN 18
// Portrait coordinates. Adjust only if the particular panel is mounted differently.
#define TOUCH_SWAP_XY 0
#define TOUCH_MIRROR_X 0
#define TOUCH_MIRROR_Y 0
void board_display_init(void);
esp_err_t board_mic_init(void);
esp_err_t board_mic_read(int32_t *samples, size_t count);


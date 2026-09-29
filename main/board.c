#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_lcd_panel_handle_t panel;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static i2s_chan_handle_t mic;
static i2c_master_dev_handle_t touch;
static bool touch_available;

static bool lcd_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t *)ctx);
    return false;
}

static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels)
{
    // ST7789 expects the high byte first; LVGL's native RGB565 is little endian.
    size_t count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    for (size_t i = 0; i < count; ++i) pixels[i].full = __builtin_bswap16(pixels[i].full);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                            area->x2 + 1, area->y2 + 1, pixels));
}

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_REL;
    if (!touch_available) return;
    uint8_t reg = 2, buf[5];
    if (i2c_master_transmit_receive(touch, &reg, 1, buf, sizeof(buf), 15) != ESP_OK) return;
    if ((buf[0] & 0x0f) == 0 || (buf[0] & 0x0f) > 2) return;
    int x = ((buf[1] & 0x0f) << 8) | buf[2];
    int y = ((buf[3] & 0x0f) << 8) | buf[4];
    if (TOUCH_SWAP_XY) { int tmp = x; x = y; y = tmp; }
    if (TOUCH_MIRROR_X) x = 239 - x;
    if (TOUCH_MIRROR_Y) y = 319 - y;
    if (x < 0 || x >= 240 || y < 0 || y >= 320) return;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PR;
}

void board_display_init(void)
{
    gpio_config_t outputs = {.pin_bit_mask = (1ULL << LCD_BL) | (1ULL << TOUCH_RST),
                             .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&outputs));
    gpio_set_level(LCD_BL, 0);
    gpio_set_level(TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    i2c_master_bus_config_t i2c = {.i2c_port = I2C_NUM_0, .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true};
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c, &bus));
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                              .device_address = 0x38, .scl_speed_hz = 100000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev, &touch));
    uint8_t reg = 0xa8, id = 0;
    touch_available = i2c_master_transmit_receive(touch, &reg, 1, &id, 1, 50) == ESP_OK;
    ESP_LOGI("board", "FT6336 touch %s, vendor=0x%02x", touch_available ? "OK" : "not found", id);
    spi_bus_config_t spi = {.sclk_io_num = LCD_SCLK, .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 32 * 2};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO));
    lv_init();
    lv_disp_drv_init(&disp_drv);
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {.dc_gpio_num = LCD_DC, .cs_gpio_num = LCD_CS,
        .pclk_hz = 40000000, .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0,
        .trans_queue_depth = 10, .on_color_trans_done = lcd_done, .user_ctx = &disp_drv};
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));
    esp_lcd_panel_dev_config_t cfg = {.reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16};
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    // This panel uses normal polarity. INVON made red/cyan and blue/yellow complements.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    lv_color_t *a = heap_caps_malloc(240 * 32 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *b = heap_caps_malloc(240 * 32 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(a && b);
    lv_disp_draw_buf_init(&draw_buf, a, b, 240 * 32);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    disp_drv.flush_cb = flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = touch_read;
    lv_indev_drv_register(&input);
    gpio_set_level(LCD_BL, 1);
}

esp_err_t board_mic_init(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 8;
    chan.dma_frame_num = 256;
    esp_err_t err = i2s_new_channel(&chan, NULL, &mic);
    if (err != ESP_OK) return err;
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = MIC_SCK, .ws = MIC_WS,
                     .dout = I2S_GPIO_UNUSED, .din = MIC_SD},
    };
    cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    err = i2s_channel_init_std_mode(mic, &cfg);
    return err == ESP_OK ? i2s_channel_enable(mic) : err;
}

esp_err_t board_mic_read(int32_t *samples, size_t count)
{
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(mic, samples, count * sizeof(*samples), &bytes, 1000);
    return err == ESP_OK && bytes != count * sizeof(*samples) ? ESP_ERR_INVALID_SIZE : err;
}


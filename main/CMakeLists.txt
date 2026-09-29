idf_component_register(SRCS "main.c" "board.c" "light.c" "ui.c" "voice.c" "ui_font.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c esp_driver_i2s
             esp_timer esp_psram esp_hw_support esp-sr led_strip lvgl)


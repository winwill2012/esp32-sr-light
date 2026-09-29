#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "light.h"
#include "ui.h"
#include "voice.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"

void app_main(void)
{
    ESP_LOGI("app", "ESP32-S3 offline voice light; PSRAM=%u bytes", (unsigned)esp_psram_get_size());
    ESP_ERROR_CHECK(light_init());
    board_display_init();
    ui_init();
    voice_start();
    int64_t previous_ms = esp_timer_get_time() / 1000;
    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        lv_tick_inc((uint32_t)(now_ms - previous_ms));
        previous_ms = now_ms;
        light_command_t command;
        while (xQueueReceive(voice_commands, &command, 0) == pdTRUE) ui_execute(command);
        voice_status_t state;
        if (xQueueReceive(voice_states, &state, 0) == pdTRUE) ui_voice_status(state);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


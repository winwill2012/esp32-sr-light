#include "light.h"
#include "board.h"
#include "led_strip.h"
#include "esp_random.h"

const light_color_t light_colors[9] = {
    {"红色", 0xff0000}, {"黄色", 0xffff00}, {"绿色", 0x00ff00},
    {"蓝色", 0x0000ff}, {"紫色", 0x8000ff}, {"白色", 0xffffff},
    {"橙色", 0xff6800}, {"青色", 0x00ffff}, {"粉色", 0xff459d},
};
static led_strip_handle_t strip;
static light_state_t state = {.on = false, .color = 5, .brightness = 35};

esp_err_t light_init(void)
{
    led_strip_config_t cfg = {.strip_gpio_num = LED_PIN, .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, .led_model = LED_MODEL_WS2812};
    led_strip_rmt_config_t rmt = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10000000,
                                .flags.with_dma = true};
    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt, &strip);
    return err == ESP_OK ? led_strip_clear(strip) : err;
}

light_state_t light_get(void) { return state; }

// This and the UI run only on the main task. Voice requests cross a FreeRTOS queue.
esp_err_t light_apply(light_command_t command)
{
    light_state_t next = state;
    switch (command) {
    case CMD_ON: next.on = true; break;
    case CMD_OFF: next.on = false; break;
    case CMD_TOGGLE: next.on = !next.on; break;
    case CMD_NEXT: next.color = (next.color + 1) % 9; next.on = true; break;
    case CMD_BRIGHTER: next.brightness = next.brightness > 90 ? 100 : next.brightness + 10; next.on = true; break;
    case CMD_DIMMER: next.brightness = next.brightness < 20 ? 10 : next.brightness - 10; break;
    case CMD_MAX_BRIGHTNESS: next.brightness = 100; next.on = true; break;
    case CMD_MIN_BRIGHTNESS: next.brightness = 10; next.on = true; break;
    case CMD_RANDOM:
        // Eight equally likely offsets select any palette color except the current one.
        next.color = (next.color + 1 + (esp_random() % 8)) % 9;
        next.on = true;
        break;
    default:
        if (command < CMD_RED || command > CMD_PINK) return ESP_ERR_INVALID_ARG;
        next.color = command - CMD_RED; next.on = true; break;
    }
    uint32_t rgb = light_colors[next.color].rgb;
    unsigned level = next.on ? next.brightness : 0;
    esp_err_t err = led_strip_set_pixel(strip, 0, ((rgb >> 16) & 255) * level / 100,
        ((rgb >> 8) & 255) * level / 100, (rgb & 255) * level / 100);
    if (err == ESP_OK) err = led_strip_refresh(strip);
    if (err == ESP_OK) state = next;
    return err;
}

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { CMD_ON = 1, CMD_OFF, CMD_NEXT, CMD_RED, CMD_YELLOW, CMD_GREEN,
               CMD_BLUE, CMD_PURPLE, CMD_WHITE, CMD_ORANGE, CMD_CYAN, CMD_PINK,
               CMD_BRIGHTER, CMD_DIMMER, CMD_TOGGLE,
               CMD_MAX_BRIGHTNESS, CMD_MIN_BRIGHTNESS, CMD_RANDOM } light_command_t;
typedef struct { const char *name; uint32_t rgb; } light_color_t;
extern const light_color_t light_colors[9];
typedef struct { bool on; uint8_t color; uint8_t brightness; } light_state_t;
esp_err_t light_init(void);
esp_err_t light_apply(light_command_t command);
light_state_t light_get(void);


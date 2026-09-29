#pragma once
#include "light.h"
typedef enum { VOICE_STARTING, VOICE_IDLE, VOICE_LISTENING, VOICE_ERROR } voice_status_t;
void ui_init(void);
void ui_refresh_light(void);
void ui_voice_status(voice_status_t status);
void ui_execute(light_command_t command);


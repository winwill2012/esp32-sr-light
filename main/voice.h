#pragma once
#include "ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
extern QueueHandle_t voice_commands;
extern QueueHandle_t voice_states;
void voice_start(void);


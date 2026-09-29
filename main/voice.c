#include "voice.h"
#include "board.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "model_path.h"
#include <stdatomic.h>
#include <math.h>

#define CONVERSATION_IDLE_MS 30000
#define COMMAND_TIMEOUT_MS 8000
#define COMMAND_THRESHOLD 0.60f
#define MIN_BRIGHTNESS_THRESHOLD 0.50f
#define MIC_GAIN_SHIFT 14 // ICS43434 24-bit samples occupy bits 31:8; 16-bit conversion with 4x gain.
QueueHandle_t voice_commands;
QueueHandle_t voice_states;
static const char *TAG = "voice";
static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *afe_data;
static const esp_mn_iface_t *mn;
static model_iface_data_t *mn_data;
static atomic_bool audio_failed;

typedef struct { light_command_t id; const char *phrase; } command_phrase_t;
// Chinese MultiNet accepts syllables separated by spaces (no tone marks).
static const command_phrase_t phrases[] = {
    {CMD_ON, "kai deng"}, {CMD_ON, "da kai deng"}, {CMD_ON, "da kai tai deng"},
    {CMD_OFF, "guan deng"}, {CMD_OFF, "guan bi deng"}, {CMD_OFF, "guan bi tai deng"},
    {CMD_NEXT, "bian se"},
    {CMD_RANDOM, "sui ji"},
    {CMD_RED, "hong deng"}, {CMD_RED, "hong se"}, {CMD_RED, "da kai hong deng"}, {CMD_RED, "qie huan hong se"},
    {CMD_YELLOW, "huang deng"}, {CMD_YELLOW, "huang se"}, {CMD_YELLOW, "da kai huang deng"},
    {CMD_GREEN, "lv deng"}, {CMD_GREEN, "lv se"}, {CMD_GREEN, "da kai lv deng"},
    {CMD_BLUE, "lan deng"}, {CMD_BLUE, "lan se"}, {CMD_BLUE, "da kai lan deng"},
    {CMD_PURPLE, "zi deng"}, {CMD_PURPLE, "zi se"}, {CMD_PURPLE, "da kai zi deng"},
    {CMD_WHITE, "bai deng"}, {CMD_WHITE, "bai se"}, {CMD_WHITE, "da kai bai deng"},
    {CMD_ORANGE, "cheng deng"}, {CMD_ORANGE, "cheng se"}, {CMD_ORANGE, "da kai cheng deng"},
    {CMD_CYAN, "qing deng"}, {CMD_CYAN, "qing se"}, {CMD_CYAN, "da kai qing deng"},
    {CMD_PINK, "fen se"}, {CMD_PINK, "fen se deng"}, {CMD_PINK, "da kai fen se deng"},
    {CMD_BRIGHTER, "tiao liang"}, {CMD_BRIGHTER, "liang yi dian"}, {CMD_BRIGHTER, "tiao liang yi dian"}, {CMD_BRIGHTER, "zeng jia liang du"},
    {CMD_DIMMER, "tiao an"}, {CMD_DIMMER, "an yi dian"}, {CMD_DIMMER, "tiao an yi dian"}, {CMD_DIMMER, "jiang di liang du"},
    {CMD_MAX_BRIGHTNESS, "zui liang"}, {CMD_MIN_BRIGHTNESS, "zui an"},
};

static void status(voice_status_t state) { xQueueOverwrite(voice_states, &state); }

static float command_threshold(light_command_t command)
{
    // The short, vowel-ending phrase "zui an" needs a lower acceptance threshold.
    return command == CMD_MIN_BRIGHTNESS ? MIN_BRIGHTNESS_THRESHOLD : COMMAND_THRESHOLD;
}

static void feed_task(void *arg)
{
    int count = afe->get_feed_chunksize(afe_data);
    int32_t *raw = heap_caps_malloc(count * sizeof(*raw), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *pcm = heap_caps_malloc(count * sizeof(*pcm), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!raw || !pcm) {
        free(raw); free(pcm);
        atomic_store(&audio_failed, true);
        status(VOICE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    int frames = 0, failures = 0;
    double energy = 0;
    int peak = 0;
    for (;;) {
        esp_err_t err = board_mic_read(raw, count);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S read: %s", esp_err_to_name(err));
            if (++failures < 3) continue;
            atomic_store(&audio_failed, true);
            status(VOICE_ERROR);
            break;
        }
        failures = 0;
        for (int i = 0; i < count; ++i) {
            int32_t sample = raw[i] >> MIC_GAIN_SHIFT;
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            pcm[i] = sample;
            if (frames < 160) {
                energy += (double)sample * sample;
                if (abs(sample) > peak) peak = abs(sample);
            }
        }
        if (++frames == 160) ESP_LOGI(TAG, "Microphone initial RMS=%.1f peak=%d", sqrt(energy / (160 * count)), peak);
        if (frames > 160) frames = 161;
        afe->feed(afe_data, pcm);
    }
    free(raw); free(pcm);
    vTaskDelete(NULL);
}

static void recognize_task(void *arg)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) goto failed;
    char *wake_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    char *command_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (!wake_name || !command_name) {
        ESP_LOGE(TAG, "Missing wake/command models in flash");
        goto failed;
    }
    ESP_LOGI(TAG, "Wake=%s MultiNet=%s", wake_name, command_name);
    afe_config_t *cfg = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!cfg) goto failed;
    cfg->aec_init = false;
    cfg->se_init = false;
    cfg->afe_perferred_core = 0;
    cfg->afe_perferred_priority = 5;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe = esp_afe_handle_from_config(cfg);
    afe_data = afe ? afe->create_from_config(cfg) : NULL;
    afe_config_free(cfg);
    if (!afe_data) goto failed;
    mn = esp_mn_handle_from_name(command_name);
    mn_data = mn ? mn->create(command_name, COMMAND_TIMEOUT_MS) : NULL;
    if (!mn_data) goto failed;
    if (mn->get_samp_chunksize(mn_data) != afe->get_fetch_chunksize(afe_data) ||
        afe->get_feed_channel_num(afe_data) != 1) {
        ESP_LOGE(TAG, "Unexpected audio frame format");
        goto failed;
    }
    if (esp_mn_commands_alloc(mn, mn_data) != ESP_OK) goto failed;
    for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i) {
        if (esp_mn_commands_add(phrases[i].id, phrases[i].phrase) != ESP_OK) goto failed;
        esp_mn_phrase_t *phrase = esp_mn_commands_get_from_string(phrases[i].phrase);
        if (!phrase) goto failed;
        phrase->threshold = command_threshold(phrases[i].id);
    }
    esp_mn_error_t *invalid = esp_mn_commands_update();
    if (invalid && invalid->num) {
        for (int i = 0; i < invalid->num; ++i) ESP_LOGE(TAG, "Invalid phrase: %s", invalid->phrases[i]->string);
        goto failed;
    }
    mn->set_det_threshold(mn_data, COMMAND_THRESHOLD);
    if (board_mic_init() != ESP_OK) goto failed;
    if (xTaskCreatePinnedToCore(feed_task, "mic_feed", 4096, NULL, 6, NULL, 0) != pdPASS) goto failed;
    ESP_LOGI(TAG, "Ready: %u phrases, wake word: hi le xin", (unsigned)(sizeof(phrases) / sizeof(phrases[0])));
    status(VOICE_IDLE);
    bool listening = false;
    int64_t deadline = 0;
    for (;;) {
        afe_fetch_result_t *result = afe->fetch(afe_data);
        if (atomic_load(&audio_failed)) goto failed;
        if (!result || result->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "AFE audio stream unavailable");
            status(VOICE_ERROR);
            continue;
        }
        if (!listening && result->wakeup_state == WAKENET_DETECTED) {
            mn->clean(mn_data);
            afe->disable_wakenet(afe_data);
            listening = true;
            deadline = esp_timer_get_time() + CONVERSATION_IDLE_MS * 1000LL;
            status(VOICE_LISTENING);
            ESP_LOGI(TAG, "Wake detected; session expires after 30 seconds without a command");
            continue;
        }
        if (!listening) continue;
        // Only a valid command renews the idle deadline; noise and decoder timeouts do not.
        if (esp_timer_get_time() >= deadline) {
            listening = false;
            mn->clean(mn_data);
            afe->enable_wakenet(afe_data);
            status(VOICE_IDLE);
            ESP_LOGI(TAG, "Conversation ended: no accepted command for 30 seconds");
            continue;
        }
        esp_mn_state_t state = mn->detect(mn_data, result->data);
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *r = mn->get_results(mn_data);
            if (r && r->num > 0) {
                light_command_t command = r->command_id[0];
                if (command >= CMD_ON && command <= CMD_RANDOM && command != CMD_TOGGLE &&
                    r->prob[0] >= command_threshold(command)) {
                    if (xQueueSend(voice_commands, &command, 0) == pdPASS) {
                        deadline = esp_timer_get_time() + CONVERSATION_IDLE_MS * 1000LL;
                        ESP_LOGI(TAG, "Recognized command=%d confidence=%.3f phrase=%s; idle timer renewed",
                                 command, r->prob[0], r->string);
                    } else {
                        ESP_LOGW(TAG, "Command queue full");
                    }
                } else {
                    ESP_LOGI(TAG, "Rejected command=%d confidence=%.3f phrase=%s", command, r->prob[0], r->string);
                }
            }
        }
        // Reset only the utterance decoder after a result or its internal timeout.
        // WakeNet stays disabled until no accepted command has arrived for 30s.
        if (state == ESP_MN_STATE_DETECTED || state == ESP_MN_STATE_TIMEOUT) {
            mn->clean(mn_data);
        }
    }
failed:
    ESP_LOGE(TAG, "Voice unavailable; touch control remains active");
    status(VOICE_ERROR);
    vTaskDelete(NULL);
}

void voice_start(void)
{
    voice_commands = xQueueCreate(8, sizeof(light_command_t));
    voice_states = xQueueCreate(1, sizeof(voice_status_t));
    assert(voice_commands && voice_states);
    if (xTaskCreatePinnedToCore(recognize_task, "speech", 12288, NULL, 4, NULL, 1) != pdPASS)
        status(VOICE_ERROR);
}

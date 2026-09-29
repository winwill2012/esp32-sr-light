#include "ui.h"
#include "lvgl.h"
#include "esp_log.h"
LV_FONT_DECLARE(ui_font);

static lv_obj_t *power_label, *color_label, *brightness_label, *status_label, *status_dot;
static lv_obj_t *lamp, *lamp_base, *power_btn, *swatches[9];
static const uint32_t BG = 0x0b1628, CARD = 0x142840, TEXT = 0xe3f3ff, MUTED = 0x94adc7;
static const uint32_t ACCENT = 0x38d9ff, EDGE = 0x294b6d;

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

void ui_execute(light_command_t command)
{
    esp_err_t err = light_apply(command);
    if (err != ESP_OK) {
        ESP_LOGE("light", "Update failed: %s", esp_err_to_name(err));
        lv_label_set_text(status_label, "灯光暂不可用");
        return;
    }
    ui_refresh_light();
    light_state_t state = light_get();
    ESP_LOGI("light", "command=%d on=%d color=%u brightness=%u", command, state.on, state.color, state.brightness);
}

static void button_event(lv_event_t *event)
{
    ui_execute((light_command_t)(intptr_t)lv_event_get_user_data(event));
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                        uint32_t color, light_command_t command)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(EDGE), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(TEXT), 0);
    lv_obj_t *text_obj = lv_label_create(btn);
    lv_label_set_text(text_obj, text);
    lv_obj_center(text_obj);
    lv_obj_add_event_cb(btn, button_event, LV_EVENT_CLICKED, (void *)(intptr_t)command);
    return btn;
}

void ui_refresh_light(void)
{
    light_state_t state = light_get();
    uint32_t color = state.on ? light_colors[state.color].rgb : 0x536178;
    lv_obj_set_style_bg_color(lamp, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(lamp_base, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_color(lamp, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(lamp, state.on ? 20 : 0, 0);
    lv_label_set_text(power_label, state.on ? "灯光已开启" : "灯光已关闭");
    lv_label_set_text(color_label, light_colors[state.color].name);
    lv_label_set_text_fmt(brightness_label, "亮度 %u%%", state.brightness);
    lv_obj_t *btn_label = lv_obj_get_child(power_btn, 0);
    lv_label_set_text(btn_label, state.on ? LV_SYMBOL_POWER "  关灯" : LV_SYMBOL_POWER "  开灯");
    lv_obj_center(btn_label);
    for (int i = 0; i < 9; ++i) {
        // Thin cyan selection ring on the dark canvas; avoid heavy dark borders.
        lv_obj_set_style_border_width(swatches[i], 1, 0);
        lv_obj_set_style_border_color(swatches[i], lv_color_hex(state.color == i ? ACCENT : EDGE), 0);
    }
}

void ui_voice_status(voice_status_t status)
{
    const char *text = "语音准备中";
    uint32_t color = 0xffd166;
    if (status == VOICE_IDLE) { text = "离线语音已就绪"; color = 0x46e0b8; }
    if (status == VOICE_LISTENING) { text = "请说指令 · 自动续期"; color = ACCENT; }
    if (status == VOICE_ERROR) { text = "语音暂不可用"; color = 0xff6e84; }
    lv_label_set_text(status_label, text);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(color), 0);
}

void ui_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(screen, &ui_font, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(screen, "声控小灯", 0, 0, TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    box(screen, 20, 21, 28, 2, ACCENT, 1);
    box(screen, 192, 21, 28, 2, ACCENT, 1);

    lv_obj_t *card = box(screen, 12, 40, 216, 88, CARD, 16);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(EDGE), 0);
    // A simple bulb icon drawn with LVGL primitives stays crisp without image assets.
    lamp = box(card, 17, 18, 34, 34, 0x536178, 17);
    lv_obj_set_style_border_width(lamp, 1, 0);
    lv_obj_set_style_border_color(lamp, lv_color_hex(EDGE), 0);
    lamp_base = box(card, 26, 54, 16, 7, 0x536178, 3);
    box(card, 29, 64, 10, 3, 0x8a9ab1, 2);
    power_label = label(card, "灯光已关闭", 71, 14, TEXT);
    color_label = label(card, "白色", 71, 39, MUTED);
    brightness_label = label(card, "亮度 35%", 71, 62, MUTED);

    power_btn = button(screen, LV_SYMBOL_POWER "  开灯", 12, 140, 102, 44, 0x1d69c7, CMD_TOGGLE);
    lv_obj_set_style_text_color(power_btn, lv_color_white(), 0);
    button(screen, LV_SYMBOL_REFRESH "  变色", 126, 140, 102, 44, CARD, CMD_NEXT);
    for (int i = 0; i < 9; ++i) {
        lv_obj_t *btn = button(screen, "", 12 + i * 24, 191, 24, 35, BG, CMD_RED + i);
        lv_obj_set_style_border_width(btn, 0, 0);
        swatches[i] = box(btn, 2, 7, 20, 20, light_colors[i].rgb, 10);
        lv_obj_set_style_border_color(swatches[i], lv_color_hex(EDGE), 0);
        lv_obj_clear_flag(swatches[i], LV_OBJ_FLAG_CLICKABLE);
    }
    button(screen, LV_SYMBOL_MINUS "  调暗", 12, 232, 102, 32, CARD, CMD_DIMMER);
    button(screen, LV_SYMBOL_PLUS "  调亮", 126, 232, 102, 32, CARD, CMD_BRIGHTER);
    status_dot = box(screen, 15, 277, 6, 6, 0xffd166, 3);
    status_label = label(screen, "语音准备中", 29, 272, MUTED);
    label(screen, "唤醒词：嗨乐鑫", 15, 297, MUTED);
    ui_refresh_light();
}

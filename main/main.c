// E-ink Claude assistant: tap BOOT, talk, tap again, and what you said is
// transcribed on the board, sent to Claude, and the reply is spoken by an
// on-device voice with subtitles over the face. Settings come from config.json on
// the microSD card, and the voice files and language model from its models folder
// (see sdcard/); the speech recognition model is in flash.
#include <stdbool.h>
#include <stdio.h>

#include "assistant.h"
#include "audio.h"
#include "board.h"
#include "claude.h"
#include "config_store.h"
#include "console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "memory.h"
#include "nvs_flash.h"
#include "sdcard.h"
#include "speaker.h"
#include "stt.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "main";

static app_config_t s_cfg;
static volatile bool s_running;  // false while waiting for the settings to be fixed

// Files from the SD card's models folder, loaded once into PSRAM and kept.
#define MODELS_DIR SDCARD_ROOT "/models"
static void *s_voice_ta, *s_voice_sg, *s_lm;
static size_t s_lm_len;

static void load_models(void)
{
    if (sdcard_mount() != SDCARD_OK) {
        memory_init();  // starts empty; saved to the card when it's back
        return;
    }
    size_t len;
    if (s_cfg.speech_enabled) {
        s_voice_ta = sdcard_load(MODELS_DIR "/en-GB_ta.bin", 2 * 1024 * 1024, &len);
        s_voice_sg = sdcard_load(MODELS_DIR "/en-GB_kh0_sg.bin", 2 * 1024 * 1024, &len);
        if (!s_voice_ta || !s_voice_sg) {  // both or neither
            heap_caps_free(s_voice_ta);
            heap_caps_free(s_voice_sg);
            s_voice_ta = s_voice_sg = NULL;
        }
    }
    if (s_cfg.listen_language_model) s_lm = sdcard_load(MODELS_DIR "/oido_v2.tlm", 4 * 1024 * 1024, &s_lm_len);
    memory_init();
    sdcard_unmount();
}

static void power_off(void)
{
    ESP_LOGI(TAG, "switching off");
    ui_set_pinned("Hold PWR to wake");
    ui_set_mode(MODE_ASLEEP);
    ui_full_refresh();
    ui_wait_idle(8000);
    board_power_off();  // returns only when USB is powering the board
    ui_set_pinned("On USB power");
    ui_set_mode(MODE_IDLE);
}

static void on_button(board_btn_t button, board_btn_event_t event)
{
    if (button == BTN_PWR && event == BTN_LONG) {
        power_off();
        return;
    }
    if (!s_running) {  // settings problem on screen: BOOT re-reads the card
        if (button == BTN_BOOT && event == BTN_SHORT) esp_restart();
        return;
    }
    if (button == BTN_BOOT) {
        assistant_button(event == BTN_LONG);
    } else if (button == BTN_PWR && event == BTN_SHORT) {
        ui_full_refresh();
    }
}

void app_main(void)
{
    board_power_hold();  // must come first, or the board dies when PWR is released
    board_init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    char problem[128];
    config_source_t src = config_load(&s_cfg, problem, sizeof problem);

    ui_init(s_cfg.display_rotation, s_cfg.display_busy_active_high);
    console_start();
    board_buttons_start(on_button);

    if (src == CFG_INVALID || src == CFG_MISSING) {
        ui_show_notice(EXPR_SORRY, problem, "Fix it, then press BOOT");
        return;  // buttons and console keep running
    }

    ui_set_pinned(src == CFG_FROM_SAVED ? "No SD card: saved settings" : "Connecting\xE2\x80\xA6");
    wifi_start(s_cfg.wifi_ssid, s_cfg.wifi_password);
    if (src == CFG_FROM_SD) load_models();  // a second or two, while Wi-Fi connects
    else memory_init();
    claude_init(&s_cfg);  // also points cJSON at PSRAM, so it comes before anything else parses JSON
    bool audio_ok = audio_init(board_i2c_bus(), s_cfg.listen_mic_gain_db) == ESP_OK;
    if (!audio_ok) ESP_LOGE(TAG, "audio codec unavailable; typed questions and subtitles still work");
    speaker_init(&s_cfg, audio_ok, s_voice_ta, s_voice_sg);
    assistant_init(&s_cfg, audio_ok);
    // Last, so the recogniser only borrows internal RAM the rest has left spare.
    stt_init(&s_cfg, s_lm, s_lm_len);
    s_running = true;
    ESP_LOGI(TAG, "ready: %u KB of PSRAM and %u KB of internal RAM free",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    if (!wifi_wait(20000)) {
        ESP_LOGW(TAG, "no Wi-Fi yet; check wifi.ssid and wifi.password in config.json");
        ui_set_pinned("No Wi-Fi: check config.json");
    } else if (!stt_ready()) {
        ui_set_pinned(stt_problem());
    } else if (s_cfg.speech_enabled && !speaker_has_voice()) {
        ui_set_pinned("No voice files: subtitles only");
    } else {
        ui_set_pinned("Tap BOOT to talk");
    }
}

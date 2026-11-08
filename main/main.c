// E-ink Claude assistant, milestone 1: face, subtitles and streaming Claude replies.
// All settings come from config.json on the microSD card (see sdcard/config.json).
#include <stdbool.h>
#include <stdio.h>

#include "assistant.h"
#include "board.h"
#include "claude.h"
#include "config_store.h"
#include "console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "speaker.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "main";

static app_config_t s_cfg;
static volatile bool s_running;  // false while waiting for the settings to be fixed

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
    if (button == BTN_BOOT && event == BTN_SHORT) {
        // Becomes push-to-talk in milestone 3.
        assistant_ask("Introduce yourself in one sentence, then tell me one surprising fact.");
    } else if (button == BTN_BOOT && event == BTN_LONG) {
        claude_forget();
        ui_set_pinned("New conversation");
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
    claude_init(&s_cfg);
    speaker_init(s_cfg.speech_pace_cps);
    assistant_init();
    s_running = true;

    if (wifi_wait(20000)) {
        ui_set_pinned("Ready: press BOOT");
    } else {
        ESP_LOGW(TAG, "no Wi-Fi yet; check wifi.ssid and wifi.password in config.json");
        ui_set_pinned("No Wi-Fi: check config.json");
    }
}

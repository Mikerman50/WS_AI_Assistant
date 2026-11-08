#include "console.h"

#include <stdio.h>
#include <string.h>

#include "assistant.h"
#include "board.h"
#include "claude.h"
#include "config_store.h"
#include "driver/usb_serial_jtag.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "face.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "driver/usb_serial_jtag_vfs.h"
#else
#include "esp_vfs_usb_serial_jtag.h"
#endif

static const char *TAG = "console";

static void handle(char *line)
{
    if (line[0] != '/') {
        assistant_ask(line);
        return;
    }
    char *arg = strchr(line, ' ');
    if (arg) *arg++ = '\0';

    if (strcmp(line, "/face") == 0 && arg) {
        face_expr_t e = face_expr_from_name(arg);
        if (e < EXPR_COUNT) ui_set_expr(e);
        else ESP_LOGW(TAG, "unknown face \"%s\"", arg);
    } else if (strcmp(line, "/mode") == 0 && arg) {
        static const char *const modes[] = {"idle", "listening", "thinking", "speaking", "asleep", "notice"};
        for (int i = 0; i < 6; i++)
            if (strcmp(arg, modes[i]) == 0) ui_set_mode((face_mode_t)i);
    } else if (strcmp(line, "/pin") == 0) {
        ui_set_pinned(arg ? arg : "");
    } else if (strcmp(line, "/full") == 0) {
        ui_full_refresh();
    } else if (strcmp(line, "/forget") == 0) {
        claude_forget();
        ESP_LOGI(TAG, "next question starts a new conversation");
    } else if (strcmp(line, "/battery") == 0) {
        ESP_LOGI(TAG, "battery %d mV", board_battery_mv());
    } else if (strcmp(line, "/reload") == 0) {
        esp_restart();  // re-reads config.json from the SD card
    } else if (strcmp(line, "/forget-settings") == 0) {
        config_forget_saved();
        ESP_LOGI(TAG, "saved copy of the settings deleted; config.json on the card is untouched");
    } else {
        ESP_LOGI(TAG, "type a question, or /face <name>, /mode <name>, /pin <text>, /full, /forget, "
                      "/battery, /reload, /forget-settings");
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char line[256];
    size_t len = 0;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) <= 0) continue;
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                line[len] = '\0';
                handle(line);
                len = 0;
            }
            continue;
        }
        if (c == 0x08 || c == 0x7F) {
            if (len > 0) len--;
            continue;
        }
        if (c >= 0x20 && len < sizeof line - 1) line[len++] = (char)c;
    }
}

void console_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "USB console unavailable");
        return;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    usb_serial_jtag_vfs_use_driver();
#else
    esp_vfs_usb_serial_jtag_use_driver();
#endif
    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "type a question and press Enter (/help for commands)");
}

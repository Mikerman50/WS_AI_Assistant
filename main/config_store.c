#include "config_store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdcard.h"

static const char *TAG = "config";

#define MAX_CONFIG_BYTES (16 * 1024)
#define NVS_NAMESPACE "assistant"
#define NVS_KEY "config_json"

// Windows hides extensions, so a file saved as "config.json" in Notepad is often
// really "config.json.txt". Accept that too.
static const char *const k_paths[] = {
    SDCARD_ROOT "/config.json",
    SDCARD_ROOT "/config.json.txt",
};

typedef enum { SD_OK, SD_NO_CARD, SD_NOT_FAT, SD_NO_FILE, SD_TOO_BIG } sd_result_t;

static char *ps_alloc(size_t n)
{
    char *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static char *read_file(const char *path, bool *too_big)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = ps_alloc(MAX_CONFIG_BYTES + 2);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, MAX_CONFIG_BYTES + 1, f);
    fclose(f);
    if (n > MAX_CONFIG_BYTES) {
        *too_big = true;
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static sd_result_t read_from_sd(char **out)
{
    sdcard_status_t st = sdcard_mount();
    if (st != SDCARD_OK) return st == SDCARD_NOT_FAT ? SD_NOT_FAT : SD_NO_CARD;

    sd_result_t result = SD_NO_FILE;
    for (size_t i = 0; i < sizeof k_paths / sizeof k_paths[0]; i++) {
        bool too_big = false;
        char *text = read_file(k_paths[i], &too_big);
        if (too_big) {
            result = SD_TOO_BIG;
            break;
        }
        if (text) {
            ESP_LOGI(TAG, "read %s", k_paths[i]);
            *out = text;
            result = SD_OK;
            break;
        }
    }
    sdcard_unmount();
    return result;
}

static char *load_saved(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return NULL;
    char *buf = NULL;
    size_t len = 0;
    if (nvs_get_blob(h, NVS_KEY, NULL, &len) == ESP_OK && len > 0 && len <= MAX_CONFIG_BYTES) {
        buf = ps_alloc(len + 1);
        if (buf && nvs_get_blob(h, NVS_KEY, buf, &len) == ESP_OK) {
            buf[len] = '\0';
        } else {
            free(buf);
            buf = NULL;
        }
    }
    nvs_close(h);
    return buf;
}

static void save_copy(const char *json)
{
    char *old = load_saved();
    bool same = old && strcmp(old, json) == 0;
    free(old);
    if (same) return;  // spare the flash
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, NVS_KEY, json, strlen(json)) == ESP_OK && nvs_commit(h) == ESP_OK)
        ESP_LOGI(TAG, "saved a copy of the settings to flash");
    nvs_close(h);
}

void config_forget_saved(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, NVS_KEY);
    nvs_commit(h);
    nvs_close(h);
}

config_source_t config_load(app_config_t *cfg, char *err, size_t errsz)
{
    config_set_defaults(cfg);
    err[0] = '\0';

    char *text = NULL;
    sd_result_t sd = read_from_sd(&text);
    if (sd == SD_OK) {
        bool ok = config_parse(text, cfg, err, errsz);
        if (ok) save_copy(text);
        free(text);
        if (ok) return CFG_FROM_SD;
        ESP_LOGE(TAG, "config.json: %s", err);
        config_set_defaults(cfg);
        return CFG_INVALID;
    }
    if (sd == SD_TOO_BIG) {
        snprintf(err, errsz, "config.json is bigger than 16 KB.");
        ESP_LOGE(TAG, "%s", err);
        return CFG_INVALID;
    }

    text = load_saved();
    if (text) {
        char saved_err[96];
        bool ok = config_parse(text, cfg, saved_err, sizeof saved_err);
        free(text);
        if (ok) {
            ESP_LOGW(TAG, "no usable config.json on the SD card, using the saved copy");
            return CFG_FROM_SAVED;
        }
        config_set_defaults(cfg);
    }

    switch (sd) {
    case SD_NO_CARD: snprintf(err, errsz, "I can't find an SD card."); break;
    case SD_NOT_FAT: snprintf(err, errsz, "The SD card needs to be FAT32."); break;
    default: snprintf(err, errsz, "No config.json on the SD card."); break;
    }
    ESP_LOGE(TAG, "%s", err);
    return CFG_MISSING;
}

#include "memory.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "memory_text.h"
#include "sdcard.h"

static const char *TAG = "memory";

#define MEMORY_FILE SDCARD_ROOT "/memory.txt"

static memory_t *s_mem;
static bool s_dirty;

void memory_init(void)
{
    s_mem = heap_caps_calloc(1, sizeof *s_mem, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_mem) return;
    size_t len = 0;
    char *text = sdcard_load(MEMORY_FILE, 64 * 1024, &len);  // fails quietly if the card isn't mounted
    if (text) {
        char *z = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (z) {
            memcpy(z, text, len);
            z[len] = '\0';
            memory_load(s_mem, z);
            heap_caps_free(z);
        }
        heap_caps_free(text);
    }
    ESP_LOGI(TAG, "%u bytes remembered", (unsigned)s_mem->len);
}

const char *memory_text(void)
{
    return s_mem ? s_mem->text : "";
}

void memory_note(const char *note)
{
    if (!s_mem) return;
    const char *arg;
    switch (memory_parse_note(note, &arg)) {
    case NOTE_REMEMBER:
        if (memory_remember(s_mem, arg)) {
            ESP_LOGI(TAG, "remembered: %s", arg);
            s_dirty = true;
        }
        break;
    case NOTE_FORGET: {
        int n = memory_forget(s_mem, arg);
        ESP_LOGI(TAG, "forgot %d line(s) about \"%s\"", n, arg);
        if (n) s_dirty = true;
        break;
    }
    case NOTE_OTHER:
        ESP_LOGI(TAG, "ignored note: %s", note);
        break;
    }
}

void memory_save(void)
{
    if (!s_mem || !s_dirty) return;
    if (sdcard_mount() != SDCARD_OK) {
        ESP_LOGW(TAG, "no SD card: memory kept until the next restart only");
        return;
    }
    FILE *f = fopen(MEMORY_FILE, "w");
    if (f) {
        size_t w = fwrite(s_mem->text, 1, s_mem->len, f);
        fclose(f);
        if (w == s_mem->len) s_dirty = false;
    }
    sdcard_unmount();
    if (s_dirty) ESP_LOGE(TAG, "couldn't write memory.txt");
}

void memory_clear(void)
{
    if (!s_mem) return;
    memory_load(s_mem, "");
    s_dirty = true;
    memory_save();
}

#include "wake.h"

#include <stdio.h>
#include <string.h>

#include "assistant.h"
#include "audio.h"
#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "power_src.h"
#include "speaker.h"
#include "wake_words.h"
#include "wakeword.h"

static const char *TAG = "wake";

#define CHUNK 480                     // 30 ms: one step of the model
#define POWER_EVERY_MS 5000
#define INTERNAL_RESERVE (160 * 1024)  // as stt.c: room for Wi-Fi, TLS and the recogniser

static ww_t *s_ww;
static const wake_word_t *s_word;
static bool s_always;
static SemaphoreHandle_t s_mic_lock;  // held by the wake task while it has the microphone open
static volatile int s_paused;
static volatile bool s_usb;
static char s_status[64] = "off";

static void *ww_alloc(size_t n, bool fast)
{
    void *p = NULL;
    if (fast && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > n + INTERNAL_RESERVE)
        p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool quiet(void)
{
    return !assistant_busy() && !speaker_busy();
}

static void set_status(const char *s)
{
    if (strcmp(s_status, s) == 0) return;
    snprintf(s_status, sizeof s_status, "%s", s);
    ESP_LOGI(TAG, "%s", s_status);
}

// Checks the power source every few seconds. True if listening is allowed.
static bool power_ok(power_src_t *power, int64_t *last)
{
    int64_t t = now_ms();
    if (t - *last >= POWER_EVERY_MS) {
        *last = t;
        bool was = s_usb;
        s_usb = power_src_update(power, usb_serial_jtag_is_connected(), board_battery_mv(), quiet(), t);
        if (s_usb != was) ESP_LOGI(TAG, "now on %s power", s_usb ? "USB" : "battery");
    }
    return s_always || s_usb;
}

static void wake_task(void *arg)
{
    (void)arg;
    static int16_t chunk[CHUNK];  // internal RAM: the I2S driver copies from DMA here
    power_src_t power;
    power_src_init(&power, now_ms());
    int64_t last_power = 0;
    char listening[48];
    snprintf(listening, sizeof listening, "listening for \"%s\"", s_word->name);

    for (;;) {
        if (!power_ok(&power, &last_power)) {
            set_status("waiting: on battery (wake_word.listen is usb)");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (s_paused || !quiet()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Listen until the wake word, a conversation, or a change of power.
        xSemaphoreTake(s_mic_lock, portMAX_DELAY);
        if (s_paused || audio_mic_open() != ESP_OK) {
            xSemaphoreGive(s_mic_lock);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        set_status(listening);
        ww_reset(s_ww);
        bool heard = false;
        while (!s_paused && quiet() && power_ok(&power, &last_power)) {
            if (audio_mic_read(chunk, CHUNK) != ESP_OK) break;
            if (ww_feed(s_ww, chunk, CHUNK)) {
                heard = true;
                break;
            }
        }
        audio_mic_close();
        xSemaphoreGive(s_mic_lock);
        if (heard) {
            ESP_LOGI(TAG, "heard \"%s\"", s_word->name);
            assistant_wake();
            vTaskDelay(pdMS_TO_TICKS(300));  // let the assistant take over before looking again
        }
    }
}

void wake_init(const app_config_t *cfg, bool audio_ok)
{
    s_mic_lock = xSemaphoreCreateMutex();
    if (wake_word_is_off(cfg->wake_word)) {
        set_status("off (wake_word.word is off)");
        return;
    }
    if (!audio_ok) {
        set_status("off: listening isn't available");
        return;
    }
    s_word = wake_word_find(cfg->wake_word);
    if (!s_word) return;  // the settings check already refused it
    s_always = strcmp(cfg->wake_listen, "always") == 0;

    const uint8_t *model = NULL;
    size_t len = 0;
    ww_builtin_model(s_word->file, &model, &len);
    int sens = wake_sens_from_name(cfg->wake_sensitivity);
    ww_params_t p = {
        .cutoff = wake_word_cutoff(s_word, sens < 0 ? WAKE_SENS_NORMAL : (wake_sens_t)sens),
        .window = s_word->window,
        .arena_bytes = s_word->arena_bytes,
    };
    char err[80];
    s_ww = model ? ww_create(model, len, &p, ww_alloc, heap_caps_free, err, sizeof err) : NULL;
    if (!s_ww) {
        ESP_LOGE(TAG, "wake word unavailable: %s", model ? err : "model missing from the firmware");
        set_status("off: the wake word model didn't load");
        return;
    }
    ESP_LOGI(TAG, "\"%s\" ready (cutoff %.3f, arena %u bytes, %u KB internal RAM free)", s_word->name,
             (double)p.cutoff, (unsigned)ww_arena_bytes(s_ww),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    set_status("starting");
    // Core 1 with the recogniser and the voice, which never run at the same time.
    if (xTaskCreatePinnedToCore(wake_task, "wake", 6144, NULL, 4, NULL, 1) != pdPASS) {
        set_status("off: no memory for its task");
        ww_destroy(s_ww);
        s_ww = NULL;
    }
}

void wake_pause(void)
{
    s_paused++;
    if (!s_mic_lock) return;
    xSemaphoreTake(s_mic_lock, portMAX_DELAY);  // the wake task has let go of the microphone
    xSemaphoreGive(s_mic_lock);
}

void wake_resume(void)
{
    if (s_paused > 0) s_paused--;
}

const char *wake_status(void)
{
    return s_status;
}

#include "stt.h"

#include <stdio.h>
#include <string.h>

#include "asr_text.h"
#include "audio_level.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tasr_nemo.h"
#include "tinyasr.h"
#include "tinyasr_lm.h"
#include "utf8.h"

static const char *TAG = "stt";

#define ASR_PARTITION "asr"
#define ASR_SUBTYPE 0x40
#define VOCAB_PLUS_BLANK 1025         // the model's 1024 word pieces and the CTC blank
#define BEAM 4                         // as upstream's measurements
#define TOP_K 6
#define ASR_STACK 36864                // the engine needs about 28 KB (measured on a PC)
#define INTERNAL_RESERVE (160 * 1024)  // internal RAM always left for Wi-Fi, TLS, DMA and the rest
#define RAW_TEXT_MAX 1024

static tasr_nemo_t *s_model;
static tasr_decoder_t *s_dec;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static TickType_t s_last_pause;

// One transcription at a time, handed to the recogniser task.
static struct {
    const int16_t *pcm;
    int n;
    char raw[RAW_TEXT_MAX];
    int64_t took_us;
} s_job;

// The engine's memory: small, frequently used buffers in internal RAM while
// there's plenty to spare, everything else in PSRAM. It expects zeroed memory.
static void *asr_alloc(size_t n, int kind)
{
    void *p = NULL;
    if (kind == 1 && n <= 48 * 1024 && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > n + INTERNAL_RESERVE)
        p = heap_caps_aligned_alloc(16, n ? n : 16, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_aligned_alloc(16, n ? n : 16, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) memset(p, 0, n);
    else ESP_LOGE(TAG, "out of memory (%u bytes)", (unsigned)n);
    return p;
}

static void asr_free(void *p)
{
    heap_caps_free(p);
}

// The engine hands its work out through this hook (meant for spreading it over
// both cores); here it all runs on one core, pausing for a tick every 100 ms so
// lower-priority work, and the idle task the watchdog checks on, still runs.
static void run_here(tasr_job_fn fn, void *ctx, int n)
{
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_pause >= pdMS_TO_TICKS(100)) {
        vTaskDelay(1);
        s_last_pause = xTaskGetTickCount();
    }
    fn(ctx, 0, n, 0);
}

static void asr_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time();
        s_last_pause = xTaskGetTickCount();
        s_job.raw[0] = '\0';
        tasr_nemo_transcribe(s_model, s_job.pcm, s_job.n, s_dec, s_job.raw, sizeof s_job.raw, NULL, 0, NULL);
        s_job.took_us = esp_timer_get_time() - t0;
        xSemaphoreGive(s_done);
    }
}

void stt_init(const app_config_t *cfg, const void *lm, size_t lm_len)
{
    tasr_alloc = asr_alloc;
    tasr_free = asr_free;
    tasr_parallel = run_here;
    s_done = xSemaphoreCreateBinary();
    // Core 1, below the UI, buttons and network work, like the voice. Created
    // first, so its stack is in place before the model takes any memory.
    if (!s_done || xTaskCreatePinnedToCore(asr_task, "asr", ASR_STACK, NULL, 3, &s_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the recogniser task");
        return;
    }

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)ASR_SUBTYPE, ASR_PARTITION);
    if (!part) {
        ESP_LOGE(TAG, "no \"%s\" partition: this firmware needs its own partitions.csv", ASR_PARTITION);
        return;
    }
    const void *blob = NULL;
    esp_partition_mmap_handle_t map;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &blob, &map) != ESP_OK) {
        ESP_LOGE(TAG, "couldn't map the speech model");
        return;
    }
    if (memcmp(blob, "TNM1", 4) != 0) {
        ESP_LOGE(TAG, "no speech model in flash: flash the merged image, which includes it");
        esp_partition_munmap(map);
        return;
    }
    s_model = tasr_nemo_load(blob, part->size);
    if (!s_model) {
        ESP_LOGE(TAG, "the speech model in flash is damaged: flash the merged image again");
        esp_partition_munmap(map);
        return;
    }
    ESP_LOGI(TAG, "speech model ready: %u KB of weights read in place from flash",
             (unsigned)(tasr_nemo_weight_bytes(s_model) / 1024));

    if (cfg->listen_language_model && lm) {
        tasr_lm_t *l = tasr_lm_load(lm, lm_len);  // weights stay where they are, already in PSRAM
        if (l) s_dec = tasr_decoder_create(l, VOCAB_PLUS_BLANK, BEAM, TOP_K, tasr_lm_weight(l, 0.3f), tasr_lm_bonus(l, 1.0f));
        if (s_dec) ESP_LOGI(TAG, "language model on");
        else ESP_LOGW(TAG, "models/oido_v2.tlm couldn't be used; listening without it");
    } else if (cfg->listen_language_model) {
        ESP_LOGW(TAG, "no models/oido_v2.tlm on the SD card; listening without the language model");
    }
}

bool stt_ready(void)
{
    return s_model != NULL;
}

bool stt_has_language_model(void)
{
    return s_dec != NULL;
}

esp_err_t stt_transcribe(int16_t *pcm, size_t samples, char *text, size_t textsz, char *err, size_t errsz,
                         volatile bool *cancel)
{
    if (textsz) text[0] = '\0';
    if (errsz) err[0] = '\0';
    if (!s_model) {
        snprintf(err, errsz, "My speech model is missing. Flash the full firmware image.");
        return ESP_ERR_INVALID_STATE;
    }
    // Working memory grows with the recording: about 0.6 MB plus 3.5 bytes per
    // sample (measured), the largest single piece about 2 bytes per sample.
    size_t need = 600 * 1024 + samples * 4;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < need ||
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < samples * 2 + 64 * 1024) {
        ESP_LOGE(TAG, "not enough PSRAM for %u samples (%u free)", (unsigned)samples,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        snprintf(err, errsz, "That was too long for me to follow. Try a shorter question.");
        return ESP_ERR_NO_MEM;
    }

    float gain = audio_speech_gain(pcm, samples);
    audio_apply_gain(pcm, samples, gain);

    s_job.pcm = pcm;
    s_job.n = (int)samples;
    xTaskNotifyGive(s_task);
    xSemaphoreTake(s_done, portMAX_DELAY);

    char tidy[RAW_TEXT_MAX];
    asr_tidy(s_job.raw, tidy, sizeof tidy);
    ESP_LOGI(TAG, "%.1f s of speech transcribed in %.1f s (level x%.1f): \"%s\"",
             (double)samples / 16000.0, (double)s_job.took_us / 1e6, (double)gain, tidy);
    if (cancel && *cancel) return ESP_ERR_NOT_FINISHED;
    if (textsz) {
        size_t len = strlen(tidy);
        size_t n = utf8_truncate(tidy, len < textsz - 1 ? len : textsz - 1);
        memcpy(text, tidy, n);
        text[n] = '\0';
    }
    return ESP_OK;
}

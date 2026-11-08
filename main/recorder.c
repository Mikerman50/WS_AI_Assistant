#include "recorder.h"

#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "audio_level.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "recorder";

#define CHUNK 512            // samples per read: 32 ms
#define SKIP_START_CHUNKS 2  // the codec settles for a few ms after opening
#define TRIM_END_MS 120      // drop the click of the button press that ended it
#define LEVEL_EVERY_US (150 * 1000)

esp_err_t recorder_capture(recording_t *out, int max_seconds, volatile bool *stop, recorder_level_cb_t on_level)
{
    memset(out, 0, sizeof *out);
    out->peak_dbfs = -96.0f;
    size_t cap = (size_t)max_seconds * AUDIO_SAMPLE_RATE;
    out->pcm = heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out->pcm) return ESP_ERR_NO_MEM;

    esp_err_t err = audio_mic_open();
    if (err != ESP_OK) {
        recorder_free(out);
        return err;
    }

    static int16_t chunk[CHUNK];  // internal RAM: the I2S driver copies from DMA here
    int64_t last_level = 0;
    int skipped = 0;
    while (!*stop) {
        err = audio_mic_read(chunk, CHUNK);
        if (err != ESP_OK) break;
        if (skipped < SKIP_START_CHUNKS) {
            skipped++;
            continue;
        }
        size_t n = CHUNK;
        if (out->samples + n > cap) n = cap - out->samples;
        memcpy(out->pcm + out->samples, chunk, n * sizeof(int16_t));
        out->samples += n;

        float db = audio_rms_dbfs(chunk, CHUNK);
        if (db > out->peak_dbfs) out->peak_dbfs = db;
        int64_t now = esp_timer_get_time();
        if (on_level && now - last_level > LEVEL_EVERY_US) {
            on_level(audio_meter(db));
            last_level = now;
        }
        if (out->samples >= cap) {
            out->hit_limit = true;
            break;
        }
    }
    audio_mic_close();
    if (on_level) on_level(0.0f);

    size_t trim = (size_t)AUDIO_SAMPLE_RATE * TRIM_END_MS / 1000;
    if (!out->hit_limit) out->samples = out->samples > trim ? out->samples - trim : 0;
    ESP_LOGI(TAG, "%.1f s recorded, loudest %.0f dBFS%s", (double)out->samples / AUDIO_SAMPLE_RATE,
             (double)out->peak_dbfs, out->peak_dbfs < -50.0f ? " (very quiet: raise listen.mic_gain_db?)" : "");
    if (err != ESP_OK) recorder_free(out);
    return err;
}

void recorder_free(recording_t *r)
{
    free(r->pcm);
    r->pcm = NULL;
    r->samples = 0;
}

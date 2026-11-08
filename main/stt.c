#include "stt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "multipart.h"
#include "stt_parse.h"

static const char *TAG = "stt";

#define STT_URL "https://api.elevenlabs.io/v1/speech-to-text"
#define UPLOAD_CHUNK 8192
#define MAX_RESPONSE (64 * 1024)

static char s_key[128];
static char s_model[32];
static char s_language[12];

void stt_init(const app_config_t *cfg)
{
    snprintf(s_key, sizeof s_key, "%s", cfg->elevenlabs_api_key);
    snprintf(s_model, sizeof s_model, "%s", cfg->elevenlabs_stt_model);
    snprintf(s_language, sizeof s_language, "%s", cfg->elevenlabs_language);
}

bool stt_ready(void)
{
    return s_key[0] != '\0';
}

static bool write_all(esp_http_client_handle_t http, const char *data, int len)
{
    for (int off = 0; off < len;) {
        int w = esp_http_client_write(http, data + off, len - off);
        if (w <= 0) return false;
        off += w;
    }
    return true;
}

static bool cancelled(volatile bool *cancel)
{
    return cancel && *cancel;
}

esp_err_t stt_transcribe(const int16_t *pcm, size_t samples, char *text, size_t textsz, char *err,
                         size_t errsz, volatile bool *cancel)
{
    esp_err_t ret = ESP_FAIL;
    char boundary[40], head[768], tail[64], ctype[96];
    char *resp = NULL;
    esp_http_client_handle_t http = NULL;
    const char *bytes = (const char *)pcm;
    int head_len, tail_len, pcm_len, total, status, len = 0;
    int64_t t0 = esp_timer_get_time();
    esp_http_client_config_t cfg = {
        .url = STT_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    text[0] = '\0';
    err[0] = '\0';

    if (!stt_ready()) {
        snprintf(err, errsz, "Listening needs an ElevenLabs API key in config.json.");
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(boundary, sizeof boundary, "----einkassistant%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random());
    // "pcm_s16le_16" means raw samples with no WAV header: 16-bit, 16 kHz, mono, little-endian.
    mp_field_t fields[4] = {
        {"model_id", s_model},
        {"file_format", "pcm_s16le_16"},
        {"tag_audio_events", "false"},
        {"language_code", s_language},
    };
    head_len = multipart_head(head, sizeof head, boundary, fields, s_language[0] ? 4 : 3, "file", "speech.pcm",
                              "application/octet-stream");
    tail_len = multipart_tail(tail, sizeof tail, boundary);
    if (head_len < 0 || tail_len < 0) {
        snprintf(err, errsz, "The ElevenLabs settings are too long.");
        return ESP_ERR_INVALID_SIZE;
    }
    pcm_len = (int)(samples * sizeof(int16_t));
    total = head_len + pcm_len + tail_len;

    http = esp_http_client_init(&cfg);
    if (!http) {
        snprintf(err, errsz, "I couldn't start a connection.");
        return ESP_FAIL;
    }
    snprintf(ctype, sizeof ctype, "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(http, "Content-Type", ctype);
    esp_http_client_set_header(http, "xi-api-key", s_key);
    esp_http_client_set_header(http, "Accept", "application/json");

    if (esp_http_client_open(http, total) != ESP_OK) {
        snprintf(err, errsz, "I couldn't reach ElevenLabs.");
        goto out;
    }
    if (!write_all(http, head, head_len)) goto dropped;
    for (int off = 0; off < pcm_len; off += UPLOAD_CHUNK) {
        if (cancelled(cancel)) {
            ret = ESP_ERR_NOT_FINISHED;
            goto out;
        }
        if (!write_all(http, bytes + off, pcm_len - off < UPLOAD_CHUNK ? pcm_len - off : UPLOAD_CHUNK)) goto dropped;
    }
    if (!write_all(http, tail, tail_len)) goto dropped;

    esp_http_client_fetch_headers(http);
    status = esp_http_client_get_status_code(http);
    resp = heap_caps_malloc(MAX_RESPONSE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resp) {
        snprintf(err, errsz, "I ran out of memory.");
        ret = ESP_ERR_NO_MEM;
        goto out;
    }
    while (len < MAX_RESPONSE) {
        int n = esp_http_client_read(http, resp + len, MAX_RESPONSE - len);
        if (n <= 0) break;
        len += n;
    }
    resp[len] = '\0';
    if (cancelled(cancel)) {
        ret = ESP_ERR_NOT_FINISHED;
        goto out;
    }

    if (stt_parse_response(status, resp, text, textsz, err, errsz) == STT_ERROR) {
        ESP_LOGE(TAG, "HTTP %d: %.300s", status, resp);
        goto out;
    }
    ESP_LOGI(TAG, "%.1f s of audio transcribed in %lld ms: \"%s\"", (double)samples / 16000.0,
             (long long)((esp_timer_get_time() - t0) / 1000), text);
    ret = ESP_OK;
    goto out;

dropped:
    snprintf(err, errsz, "The connection to ElevenLabs dropped.");
out:
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    free(resp);
    return ret;
}

#include "speaker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "audio_level.h"
#include "captions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gfx.h"
#include "picotts_esp.h"
#include "tts_text.h"
#include "ui.h"
#include "utf8.h"

static const char *TAG = "speaker";

#define PLAY_CHUNK 512                              // samples per write: 32 ms
#define MAX_CLIP_SAMPLES (AUDIO_SAMPLE_RATE * 15)  // a sentence longer than 15 s is cut short
#define TTS_TEXT_MAX 1024

typedef enum { ITEM_BEGIN, ITEM_SENTENCE, ITEM_ERROR, ITEM_END } item_kind_t;

typedef struct {
    item_kind_t kind;
    uint32_t reply;  // which reply this belongs to (see s_reply and s_stopped)
    char *text;      // what the subtitles show
    char cue[24];
    int16_t *pcm;  // the synthesised speech, or NULL for subtitles only
    size_t samples, cap;
} item_t;

static QueueHandle_t s_in;     // from the assistant to the synthesiser
static QueueHandle_t s_ready;  // from the synthesiser to the player
static int s_pending;          // items not yet fully played (atomic)
// Replies are numbered; speaker_stop() marks every reply up to the current one
// as stopped, so a stop can never leak into the next reply.
static volatile uint32_t s_reply;
static volatile uint32_t s_stopped;
static bool s_voice;           // speak replies (voice files loaded, codec working, enabled)
static const void *s_ta, *s_sg;  // the voice files, in PSRAM
static bool s_engine_on;       // the engine is started for this reply (synthesiser task only)
static int s_volume = 70, s_speed = 100, s_pitch = 100, s_pace_cps = 15;

// Player state (player task only).
static bool s_spoke;                // this reply put subtitles on screen
static bool s_speaker_on;           // the amplifier is on for this reply
static bool s_speaker_failed;       // the speaker couldn't be opened: subtitles only
static char s_last[FACE_TEXT_MAX];  // last sentence, pinned on the idle face
static cap_layout_t s_layout;

static bool stopped(const item_t *it)
{
    return it->reply <= s_stopped;
}

static void enqueue(item_kind_t kind, const char *text, const char *cue)
{
    item_t it = {.kind = kind, .reply = s_reply};
    if (text) {
        size_t n = strlen(text) + 1;
        it.text = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!it.text) return;
        memcpy(it.text, text, n);
    }
    if (cue) snprintf(it.cue, sizeof it.cue, "%s", cue);
    __atomic_add_fetch(&s_pending, 1, __ATOMIC_SEQ_CST);
    xQueueSend(s_in, &it, portMAX_DELAY);
}

void speaker_begin_reply(void)
{
    s_reply++;
    enqueue(ITEM_BEGIN, NULL, NULL);
}

void speaker_say(const char *sentence, const char *cue)
{
    enqueue(ITEM_SENTENCE, sentence, cue);
}

void speaker_error(const char *message)
{
    enqueue(ITEM_ERROR, message, "sorry");
}

void speaker_end_reply(void)
{
    enqueue(ITEM_END, NULL, NULL);
}

void speaker_stop(void)
{
    s_stopped = s_reply;
}

bool speaker_busy(void)
{
    return __atomic_load_n(&s_pending, __ATOMIC_SEQ_CST) > 0;
}

bool speaker_has_voice(void)
{
    return s_voice;
}

// ------------------------------------------------------------------ synthesiser

// Collects the engine's output into the item's buffer, which grows as needed.
static bool collect(const int16_t *samples, size_t count, void *ctx)
{
    item_t *it = ctx;
    if (stopped(it)) return false;
    if (it->samples + count > it->cap) {
        size_t cap = it->cap ? it->cap * 2 : AUDIO_SAMPLE_RATE * 4;  // start with 4 s
        while (cap < it->samples + count) cap *= 2;
        if (cap > MAX_CLIP_SAMPLES) cap = MAX_CLIP_SAMPLES;
        if (it->samples + count > cap) return false;  // too long: keep what there is
        int16_t *p = heap_caps_realloc(it->pcm, cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) return false;
        it->pcm = p;
        it->cap = cap;
    }
    memcpy(it->pcm + it->samples, samples, count * sizeof(int16_t));
    it->samples += count;
    return true;
}

static void synthesise(item_t *it)
{
    static char text[TTS_TEXT_MAX];  // only this task uses it
    tts_prepare(it->text, s_speed, s_pitch, text, sizeof text);
    int64_t t0 = esp_timer_get_time();
    int r = picotts_direct_say(text, collect, it);
    if (r < 0 || it->samples == 0) {
        if (r < 0) ESP_LOGW(TAG, "voice failed (%d); subtitles only for this sentence", r);
        free(it->pcm);
        it->pcm = NULL;
        it->samples = it->cap = 0;
        return;
    }
    ESP_LOGI(TAG, "%.1f s of speech made in %.1f s", (double)it->samples / AUDIO_SAMPLE_RATE,
             (double)(esp_timer_get_time() - t0) / 1e6);
}

// The engine's 1.1 MB of working memory is only taken for the length of a reply,
// so it's free while the board listens and transcribes.
static void engine_start(void)
{
    if (!s_voice || s_engine_on) return;
    int64_t t0 = esp_timer_get_time();
    s_engine_on = picotts_esp_start(s_ta, s_sg);
    if (s_engine_on) ESP_LOGI(TAG, "voice started in %lld ms", (long long)((esp_timer_get_time() - t0) / 1000));
    else ESP_LOGE(TAG, "voice didn't start; subtitles only for this reply");
}

static void engine_stop(void)
{
    if (!s_engine_on) return;
    picotts_esp_stop();
    s_engine_on = false;
}

static void synth_task(void *arg)
{
    (void)arg;
    item_t it;
    for (;;) {
        xQueueReceive(s_in, &it, portMAX_DELAY);
        if (it.kind == ITEM_BEGIN) engine_start();
        if ((it.kind == ITEM_SENTENCE || it.kind == ITEM_ERROR) && s_engine_on && !stopped(&it)) synthesise(&it);
        if (it.kind == ITEM_END) engine_stop();  // what's been made plays on from its own buffers
        // A short queue: the next sentence is made while one plays.
        xQueueSend(s_ready, &it, portMAX_DELAY);
    }
}

// ------------------------------------------------------------------ player

static int measure_caption(const char *s, int nbytes, void *ctx)
{
    (void)ctx;
    return gfx_text_width_n(&font_caption, s, nbytes);
}

// Lays the sentence out as subtitle pages timed across `duration_ms` (0: at the
// subtitle pace). Returns the duration used.
static uint32_t layout_captions(const char *text, uint32_t duration_ms)
{
    int n = utf8_count(text);
    uint32_t *starts = malloc(sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
    if (!starts) {
        s_layout.npages = 0;
        return duration_ms;
    }
    uint32_t est = captions_estimate(text, (float)s_pace_cps, starts, n);
    if (duration_ms) captions_scale(starts, n, est, duration_ms);  // stretch to the real audio
    else duration_ms = est;
    captions_layout(&s_layout, text, FACE_CAPTION_MAX_W, FACE_CAPTION_LINES, measure_caption, NULL);
    captions_apply_timing(&s_layout, starts, NULL, n, duration_ms);
    free(starts);
    return duration_ms;
}

static void show_caption_at(uint32_t t_ms, int *shown, float mouth)
{
    if (s_layout.npages == 0) return;
    int p = captions_page_at(&s_layout, t_ms);
    if (p != *shown) {
        ui_set_caption(&s_layout.page[p]);
        *shown = p;
    }
    ui_set_speaking(mouth, captions_progress(&s_layout.page[p], t_ms));
}

static bool speaker_ready(void)
{
    if (s_speaker_on) return true;
    if (s_speaker_failed) return false;
    if (audio_speaker_open(s_volume) != ESP_OK) {
        ESP_LOGE(TAG, "speaker unavailable; subtitles only");
        s_speaker_failed = true;
        return false;
    }
    s_speaker_on = true;
    return true;
}

// Plays the clip with the subtitles following it and the mouth moving with its
// loudness. Writes block while the I2S buffer is full, which keeps time.
static void play_audio(const item_t *it)
{
    layout_captions(it->text, (uint32_t)((uint64_t)it->samples * 1000 / AUDIO_SAMPLE_RATE));
    ui_set_mode(MODE_SPEAKING);
    s_spoke = true;
    int shown = -1;
    for (size_t off = 0; off < it->samples && !stopped(it); off += PLAY_CHUNK) {
        size_t n = it->samples - off < PLAY_CHUNK ? it->samples - off : PLAY_CHUNK;
        uint32_t t = (uint32_t)((uint64_t)off * 1000 / AUDIO_SAMPLE_RATE);
        show_caption_at(t, &shown, audio_mouth(audio_rms_dbfs(it->pcm + off, n)));
        if (audio_speaker_write(it->pcm + off, n) != ESP_OK) {
            ESP_LOGW(TAG, "speaker write failed");
            break;
        }
    }
    if (!stopped(it)) ui_set_speaking(0.0f, 1.0f);  // mouth closed until the next sentence
}

// Without a voice: subtitles at the configured pace and a mouth that flaps.
static void play_paced(const item_t *it)
{
    uint32_t total = layout_captions(it->text, 0);
    if (s_layout.npages == 0) return;
    ui_set_mode(MODE_SPEAKING);
    s_spoke = true;
    int shown = -1;
    int64_t t0 = esp_timer_get_time();
    while (!stopped(it)) {
        uint32_t t = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        float mouth = t < total ? (((t / 170) % 2) ? 1.0f : 0.34f) : 0.0f;
        show_caption_at(t, &shown, mouth);
        if (t >= total + 400) break;
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

static void play(const item_t *it)
{
    face_expr_t e = face_expr_from_name(it->cue);
    if (e < EXPR_COUNT) ui_set_expr(e);
    if (it->pcm && speaker_ready()) play_audio(it);
    else play_paced(it);
    size_t len = strlen(it->text);
    size_t n = utf8_truncate(it->text, len < sizeof s_last - 1 ? len : sizeof s_last - 1);
    memcpy(s_last, it->text, n);
    s_last[n] = '\0';
}

static void finish_reply(void)
{
    if (s_speaker_on) {
        audio_speaker_close();
        s_speaker_on = false;
    }
    s_speaker_failed = false;  // try again next reply
    ui_clear_caption();
    if (s_last[0]) ui_set_pinned(s_last);
    ui_set_mode(MODE_IDLE);
    if (s_spoke) ui_full_refresh();  // clean up ghosting from the burst of partial refreshes
    s_last[0] = '\0';
    s_spoke = false;
}

static void play_task(void *arg)
{
    (void)arg;
    item_t it;
    for (;;) {
        xQueueReceive(s_ready, &it, portMAX_DELAY);
        switch (it.kind) {
        case ITEM_BEGIN:
            break;
        case ITEM_SENTENCE:
        case ITEM_ERROR:
            if (it.kind == ITEM_ERROR) ESP_LOGW(TAG, "error: %s", it.text);
            if (!stopped(&it)) play(&it);  // stopped: skip the rest of the reply
            break;
        case ITEM_END:
            finish_reply();
            break;
        }
        free(it.text);
        free(it.pcm);
        __atomic_sub_fetch(&s_pending, 1, __ATOMIC_SEQ_CST);
    }
}

void speaker_init(const app_config_t *cfg, bool audio_ok, const void *voice_ta, const void *voice_sg)
{
    s_volume = cfg->speech_volume;
    s_speed = cfg->speech_speed;
    s_pitch = cfg->speech_pitch;
    s_pace_cps = cfg->speech_pace_cps;
    s_ta = voice_ta;
    s_sg = voice_sg;
    if (!cfg->speech_enabled) {
        ESP_LOGI(TAG, "voice off in config.json; subtitles only");
    } else if (!audio_ok) {
        ESP_LOGW(TAG, "no audio codec; subtitles only");
    } else if (!voice_ta || !voice_sg) {
        ESP_LOGW(TAG, "no voice files (models/en-GB_*.bin on the SD card); subtitles only");
    } else {
        s_voice = true;
    }

    s_in = xQueueCreate(24, sizeof(item_t));
    s_ready = xQueueCreate(1, sizeof(item_t));
    // The engine is heavy and Wi-Fi lives on core 0, so it gets core 1, below
    // the UI, buttons and network work. The engine itself needs only about 1 KB
    // of stack (measured by test/host/speaker_sim.c); the rest is room for
    // logging with floats and the Xtensa cores' register-window spills.
    xTaskCreatePinnedToCore(synth_task, "synth", 12288, NULL, 3, NULL, 1);
    xTaskCreate(play_task, "player", 6144, NULL, 6, NULL);
}

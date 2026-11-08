// Milestone 1 speaker: no audio, subtitles paced at speaking speed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "captions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gfx.h"
#include "speaker.h"
#include "ui.h"
#include "utf8.h"

static const char *TAG = "speaker";

typedef enum { ITEM_SENTENCE, ITEM_ERROR, ITEM_END } item_kind_t;

typedef struct {
    item_kind_t kind;
    char *text;
    char cue[24];
} item_t;

static QueueHandle_t s_queue;
static volatile bool s_playing;
static char s_last[FACE_TEXT_MAX];  // last thing said, pinned on the idle face
static cap_layout_t s_layout;       // used only by the speaker task
static int s_pace_cps = 15;

static int measure_caption(const char *s, int nbytes, void *ctx)
{
    (void)ctx;
    return gfx_text_width_n(&font_caption, s, nbytes);
}

static void enqueue(item_kind_t kind, const char *text, const char *cue)
{
    item_t it = {.kind = kind};
    if (text) {
        size_t n = strlen(text) + 1;
        it.text = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!it.text) return;
        memcpy(it.text, text, n);
    }
    if (cue) snprintf(it.cue, sizeof it.cue, "%s", cue);
    xQueueSend(s_queue, &it, portMAX_DELAY);
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

bool speaker_busy(void)
{
    return s_playing || uxQueueMessagesWaiting(s_queue) > 0;
}

static void play(const char *text, const char *cue)
{
    face_expr_t e = face_expr_from_name(cue);
    if (e < EXPR_COUNT) ui_set_expr(e);

    int n = utf8_count(text);
    uint32_t *starts = malloc(sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
    if (!starts) return;
    uint32_t total = captions_estimate(text, (float)s_pace_cps, starts, n);
    captions_layout(&s_layout, text, FACE_CAPTION_MAX_W, FACE_CAPTION_LINES, measure_caption, NULL);
    captions_apply_timing(&s_layout, starts, NULL, n, total);
    free(starts);
    if (s_layout.npages == 0) return;

    ui_set_mode(MODE_SPEAKING);
    int shown = -1;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        uint32_t t = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        int p = captions_page_at(&s_layout, t);
        if (p != shown) {
            ui_set_caption(&s_layout.page[p]);
            shown = p;
        }
        // Stand-in for the audio envelope until there is real audio.
        float mouth = t < total ? (((t / 170) % 2) ? 1.0f : 0.34f) : 0.0f;
        ui_set_speaking(mouth, captions_progress(&s_layout.page[p], t));
        if (t >= total + 400) break;
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    snprintf(s_last, sizeof s_last, "%s", text);
}

static void finish_reply(void)
{
    ui_clear_caption();
    if (s_last[0]) ui_set_pinned(s_last);
    ui_set_mode(MODE_IDLE);
    ui_full_refresh();  // clean up ghosting from the burst of partial refreshes
    s_last[0] = '\0';
}

static void speaker_task(void *arg)
{
    (void)arg;
    item_t it;
    for (;;) {
        xQueueReceive(s_queue, &it, portMAX_DELAY);
        s_playing = true;
        switch (it.kind) {
        case ITEM_SENTENCE:
            play(it.text, it.cue);
            break;
        case ITEM_ERROR:
            ESP_LOGW(TAG, "error: %s", it.text);
            play(it.text, it.cue);
            break;
        case ITEM_END:
            finish_reply();
            break;
        }
        free(it.text);
        s_playing = false;
    }
}

void speaker_init(int pace_cps)
{
    s_pace_cps = pace_cps;
    s_queue = xQueueCreate(24, sizeof(item_t));
    xTaskCreate(speaker_task, "speaker", 4096, NULL, 5, NULL);
}

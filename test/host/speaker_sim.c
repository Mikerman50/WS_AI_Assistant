// Runs the firmware's speaker (main/speaker.c, unchanged) on a PC: the real
// voice engine, subtitle layout and text preparation, with FreeRTOS, the audio
// codec and the screen replaced by small stand-ins. Simulated time runs
// SPEEDUP times faster than real time, so a reply plays in a fraction of a second.
//   make -C test/host speaker
// It checks what someone in the room would notice: every sentence is heard in
// order, subtitles follow the audio, the mouth moves, a tap stops the voice at
// once and skips the rest of the reply, the next reply isn't affected, and the
// speaker is switched off afterwards. It also reports how much stack each task
// used, against what speaker.c gives it.
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "captions.h"
#include "config_parse.h"
#include "face.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "picotts_esp.h"
#include "speaker.h"
#include "ui.h"

#define SPEEDUP 8

static int g_fail, g_pass;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (cond) g_pass++;                                                        \
        else {                                                                     \
            g_fail++;                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                          \
    } while (0)

// ---------------------------------------------------------------- simulated time

static struct timespec g_t0;

int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    int64_t us = (int64_t)(t.tv_sec - g_t0.tv_sec) * 1000000 + (t.tv_nsec - g_t0.tv_nsec) / 1000;
    return us * SPEEDUP;
}

static void sim_sleep_us(int64_t sim_us)
{
    if (sim_us <= 0) return;
    int64_t real = sim_us / SPEEDUP;
    struct timespec t = {(time_t)(real / 1000000), (long)(real % 1000000) * 1000};
    while (nanosleep(&t, &t) == -1 && errno == EINTR) {
    }
}

static uint32_t sim_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------- FreeRTOS stand-ins

void vTaskDelay(TickType_t ticks)
{
    sim_sleep_us((int64_t)ticks * 1000);  // CONFIG_FREERTOS_HZ=1000
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)sim_ms();
}

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t c;
    size_t item, len, head, count;
    unsigned char *buf;
} queue_t;

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size)
{
    queue_t *q = calloc(1, sizeof *q);
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->c, NULL);
    q->item = item_size;
    q->len = len;
    q->buf = malloc((size_t)len * item_size);
    return q;
}

BaseType_t xQueueSend(QueueHandle_t h, const void *item, TickType_t wait)
{
    queue_t *q = h;
    pthread_mutex_lock(&q->m);
    while (q->count == q->len) {
        if (wait == 0) {
            pthread_mutex_unlock(&q->m);
            return pdFALSE;
        }
        pthread_cond_wait(&q->c, &q->m);
    }
    memcpy(q->buf + ((q->head + q->count) % q->len) * q->item, item, q->item);
    q->count++;
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t h, void *item, TickType_t wait)
{
    queue_t *q = h;
    pthread_mutex_lock(&q->m);
    while (q->count == 0) {
        if (wait == 0) {
            pthread_mutex_unlock(&q->m);
            return pdFALSE;
        }
        pthread_cond_wait(&q->c, &q->m);
    }
    memcpy(item, q->buf + q->head * q->item, q->item);
    q->head = (q->head + 1) % q->len;
    q->count--;
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
    return pdTRUE;
}

// Tasks run on pthreads with stacks we allocate, filled with a pattern so the
// high-water mark can be measured afterwards.
#define STACK_FILL 0xA5
typedef struct {
    const char *name;
    uint32_t declared;
    unsigned char *mem;
    size_t size;
} task_stack_t;
static task_stack_t g_stacks[8];
static int g_nstacks;

typedef struct {
    TaskFunction_t fn;
    void *arg;
} start_t;

static void *trampoline(void *p)
{
    start_t s = *(start_t *)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *const name, const uint32_t stack, void *const arg,
                                   UBaseType_t prio, TaskHandle_t *const handle, const BaseType_t core)
{
    (void)prio;
    (void)core;
    if (handle) *handle = NULL;
    size_t size = stack < (uint32_t)PTHREAD_STACK_MIN ? (size_t)PTHREAD_STACK_MIN : stack;
    size = (size + 4095) & ~(size_t)4095;
    unsigned char *mem = aligned_alloc(4096, size);
    memset(mem, STACK_FILL, size);
    g_stacks[g_nstacks++] = (task_stack_t){name, stack, mem, size};
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstack(&a, mem, size);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    start_t *s = malloc(sizeof *s);
    *s = (start_t){fn, arg};
    pthread_t t;
    int r = pthread_create(&t, &a, trampoline, s);
    pthread_attr_destroy(&a);
    return r == 0 ? pdPASS : pdFALSE;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                       TaskHandle_t *handle)
{
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, -1);
}

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    (void)caps;
    return realloc(ptr, size);
}

void heap_caps_free(void *ptr)
{
    free(ptr);
}

// ---------------------------------------------------------------- the voice, from files

static const char *g_ta_path, *g_sg_path;

static void *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = aligned_alloc(16, ((size_t)n + 15) & ~(size_t)15);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f);
    return buf;
}

// As on the board: the engine's memory is taken when a reply starts and given
// back when it ends. Counted, so the test can check that happens once each.
static int g_engine_starts, g_engine_stops;
static bool g_engine_on;
static char *g_engine_mem;

bool picotts_esp_start(const void *ta, const void *sg)
{
    if (g_engine_on) return true;
    g_engine_mem = malloc(PICOTTS_MEM_SIZE);
    memset(g_engine_mem, 0xA5, PICOTTS_MEM_SIZE);  // heap memory isn't zeroed on the board either
    if (!picotts_direct_init(ta, sg, g_engine_mem, PICOTTS_MEM_SIZE)) return false;
    g_engine_on = true;
    g_engine_starts++;
    return true;
}

void picotts_esp_stop(void)
{
    if (!g_engine_on) return;
    picotts_direct_shutdown();
    free(g_engine_mem);
    g_engine_mem = NULL;
    g_engine_on = false;
    g_engine_stops++;
}

// ---------------------------------------------------------------- the speaker stand-in

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_speaker_open, g_speaker_fails;
static int g_opens, g_closes, g_volume;
static size_t g_played;                 // samples written to the speaker
static uint32_t g_first_sound_ms;       // simulated time of the first write in a reply
static uint32_t g_longest_gap_ms;       // longest pause between writes while open
static uint32_t g_last_write_end_ms;

esp_err_t audio_speaker_open(int volume)
{
    pthread_mutex_lock(&g_lock);
    esp_err_t r = ESP_OK;
    if (g_speaker_fails) {
        r = ESP_FAIL;
    } else {
        g_speaker_open = true;
        g_opens++;
        g_volume = volume;
        g_last_write_end_ms = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return r;
}

esp_err_t audio_speaker_write(const int16_t *samples, size_t n)
{
    (void)samples;
    pthread_mutex_lock(&g_lock);
    bool open = g_speaker_open;
    uint32_t now = sim_ms();
    if (open) {
        if (g_played == 0) g_first_sound_ms = now;
        if (g_last_write_end_ms && now > g_last_write_end_ms && now - g_last_write_end_ms > g_longest_gap_ms)
            g_longest_gap_ms = now - g_last_write_end_ms;
        g_played += n;
    }
    pthread_mutex_unlock(&g_lock);
    if (!open) return ESP_ERR_INVALID_STATE;
    sim_sleep_us((int64_t)n * 1000000 / AUDIO_SAMPLE_RATE);  // the I2S buffer drains in real time
    pthread_mutex_lock(&g_lock);
    g_last_write_end_ms = sim_ms();
    pthread_mutex_unlock(&g_lock);
    return ESP_OK;
}

void audio_speaker_close(void)
{
    pthread_mutex_lock(&g_lock);
    g_speaker_open = false;
    g_closes++;
    pthread_mutex_unlock(&g_lock);
}

// ---------------------------------------------------------------- the screen stand-in

static face_mode_t g_mode = MODE_IDLE;
static face_expr_t g_expr = EXPR_NEUTRAL;
static char g_captions[4096];  // every subtitle page shown, joined with " | "
static char g_pinned[FACE_TEXT_MAX];
static int g_full_refreshes, g_mouth_changes;
static float g_mouth, g_mouth_max;
static bool g_spoke_while_idle;  // speaking state while the face was idle

void ui_set_mode(face_mode_t mode)
{
    pthread_mutex_lock(&g_lock);
    g_mode = mode;
    if (mode != MODE_SPEAKING) g_mouth = 0.0f;
    pthread_mutex_unlock(&g_lock);
}

void ui_set_expr(face_expr_t expr)
{
    pthread_mutex_lock(&g_lock);
    g_expr = expr;
    pthread_mutex_unlock(&g_lock);
}

void ui_set_caption(const cap_page_t *page)
{
    pthread_mutex_lock(&g_lock);
    size_t len = strlen(g_captions);
    for (int i = 0; i < page->nlines; i++)
        len += (size_t)snprintf(g_captions + len, sizeof g_captions - len, "%s%s", len ? (i ? " " : " | ") : "",
                                page->line[i]);
    pthread_mutex_unlock(&g_lock);
}

void ui_clear_caption(void)
{
}

void ui_set_speaking(float mouth, float look)
{
    (void)look;
    pthread_mutex_lock(&g_lock);
    if (g_mode != MODE_SPEAKING && mouth > 0.0f) g_spoke_while_idle = true;
    if (mouth != g_mouth) g_mouth_changes++;
    g_mouth = mouth;
    if (mouth > g_mouth_max) g_mouth_max = mouth;
    pthread_mutex_unlock(&g_lock);
}

void ui_set_pinned(const char *text)
{
    pthread_mutex_lock(&g_lock);
    snprintf(g_pinned, sizeof g_pinned, "%s", text);
    pthread_mutex_unlock(&g_lock);
}

void ui_full_refresh(void)
{
    pthread_mutex_lock(&g_lock);
    g_full_refreshes++;
    pthread_mutex_unlock(&g_lock);
}

// ---------------------------------------------------------------- scenarios

static void reset_observations(void)
{
    pthread_mutex_lock(&g_lock);
    g_played = 0;
    g_first_sound_ms = 0;
    g_longest_gap_ms = 0;
    g_last_write_end_ms = 0;
    g_captions[0] = '\0';
    g_pinned[0] = '\0';
    g_full_refreshes = 0;
    g_mouth_changes = 0;
    g_mouth_max = 0.0f;
    g_spoke_while_idle = false;
    pthread_mutex_unlock(&g_lock);
}

static double played_s(void)
{
    pthread_mutex_lock(&g_lock);
    double s = (double)g_played / AUDIO_SAMPLE_RATE;
    pthread_mutex_unlock(&g_lock);
    return s;
}

// Waits (in real time) until the speaker has nothing left to do.
static bool wait_idle(int real_ms)
{
    for (int i = 0; i < real_ms; i++) {
        if (!speaker_busy()) return true;
        usleep(1000);
    }
    return false;
}

static bool contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

static void scenario_reply(void)
{
    reset_observations();
    int opens = g_opens, closes = g_closes, starts = g_engine_starts, stops = g_engine_stops;
    uint32_t t0 = sim_ms();
    speaker_begin_reply();
    speaker_say("Hello! I'm Claude, living in a very small screen.", "happy");
    speaker_say("It should stay dry until about four, so no coat needed.", "");
    speaker_say("Anything else?", "curious");
    speaker_end_reply();
    CHECK(wait_idle(20000));
    printf("reply: %.2f s heard, first sound after %u ms, longest gap %u ms, %d mouth changes\n", played_s(),
           g_first_sound_ms - t0, g_longest_gap_ms, g_mouth_changes);
    printf("  subtitles: %s\n", g_captions);
    CHECK(played_s() > 5.0 && played_s() < 15.0);
    CHECK(g_opens == opens + 1 && g_closes == closes + 1 && !g_speaker_open);  // amp on once, off at the end
    CHECK(g_volume == 65);
    const char *a = strstr(g_captions, "Hello!"), *b = strstr(g_captions, "dry"), *c = strstr(g_captions, "else?");
    CHECK(a && b && c && a < b && b < c);  // every sentence, in order
    CHECK(g_longest_gap_ms < 1000);        // no stall between sentences (loose: CI machines vary)
    CHECK(g_mouth_max >= 0.99f && g_mouth_changes > 10);
    CHECK(!g_spoke_while_idle);
    CHECK(g_mode == MODE_IDLE && g_expr == EXPR_CURIOUS);
    CHECK(strcmp(g_pinned, "Anything else?") == 0);
    CHECK(g_full_refreshes == 1);
    // The voice engine ran for this reply only, and its memory is free again.
    CHECK(g_engine_starts == starts + 1 && g_engine_stops == stops + 1 && !g_engine_on);
}

static void scenario_stop(void)
{
    reset_observations();
    int closes = g_closes;
    speaker_begin_reply();
    speaker_say("This is a long first sentence, which goes on for quite a while so that there is plenty of time "
                "to interrupt it part of the way through.",
                "");
    speaker_say("SECOND sentence that should never be heard.", "");
    speaker_say("THIRD one, also skipped.", "");
    speaker_end_reply();
    while (played_s() < 1.0) usleep(200);
    speaker_stop();  // a tap on BOOT
    double at_stop = played_s();
    CHECK(wait_idle(20000));
    printf("stop: stopped at %.2f s, %.2f s heard in all\n", at_stop, played_s());
    CHECK(played_s() - at_stop < 0.1);  // cut off within a chunk or two
    CHECK(!contains(g_captions, "SECOND") && !contains(g_captions, "THIRD"));
    CHECK(g_closes == closes + 1 && !g_speaker_open);
    CHECK(g_mode == MODE_IDLE);
    CHECK(strncmp(g_pinned, "This is a long first sentence", 29) == 0);
}

static void scenario_after_stop(void)
{
    // A stop never leaks into the next reply, even one made while idle.
    speaker_stop();
    reset_observations();
    speaker_begin_reply();
    speaker_say("Still here, and still talking.", "");
    speaker_end_reply();
    CHECK(wait_idle(20000));
    printf("after stop: %.2f s heard\n", played_s());
    CHECK(played_s() > 1.0);
    CHECK(contains(g_captions, "Still here"));
}

static void scenario_error(void)
{
    reset_observations();
    speaker_begin_reply();
    speaker_error("I can't reach the Wi-Fi.");
    speaker_end_reply();
    CHECK(wait_idle(20000));
    printf("error: %.2f s heard\n", played_s());
    CHECK(played_s() > 1.0);
    CHECK(g_expr == EXPR_SORRY);
    CHECK(contains(g_captions, "Wi-Fi"));
}

static void scenario_speaker_fails(void)
{
    // The amplifier side can't be opened: subtitles carry on at the subtitle pace.
    reset_observations();
    g_speaker_fails = true;
    uint32_t t0 = sim_ms();
    speaker_begin_reply();
    speaker_say("Nobody can hear this.", "");
    speaker_end_reply();
    CHECK(wait_idle(20000));
    uint32_t took = sim_ms() - t0;
    g_speaker_fails = false;
    printf("speaker fails: subtitles only, %u ms\n", took);
    CHECK(g_played == 0);
    CHECK(contains(g_captions, "Nobody"));
    CHECK(took > 1000 && g_mouth_max > 0.9f);
    // The next reply tries the speaker again.
    reset_observations();
    speaker_begin_reply();
    speaker_say("Better now.", "");
    speaker_end_reply();
    CHECK(wait_idle(20000));
    CHECK(played_s() > 0.4);
}

static void scenario_voice_off(void)
{
    reset_observations();
    uint32_t t0 = sim_ms();
    speaker_begin_reply();
    speaker_say("Subtitles only, at the configured pace.", "");
    speaker_end_reply();
    CHECK(wait_idle(20000));
    uint32_t took = sim_ms() - t0;
    uint32_t starts[64];
    uint32_t expect = captions_estimate("Subtitles only, at the configured pace.", 15.0f, starts, 64);
    printf("voice off: %u ms (pace estimate %u ms)\n", took, expect);
    CHECK(g_opens == 0 && g_played == 0 && g_engine_starts == 0);
    CHECK(contains(g_captions, "Subtitles only"));
    CHECK(took >= expect && took < expect + 3000);
    CHECK(g_mode == MODE_IDLE && g_full_refreshes == 1);
}

static size_t stack_used(const task_stack_t *s)
{
    size_t untouched = 0;
    while (untouched < s->size && s->mem[untouched] == STACK_FILL) untouched++;
    return s->size - untouched;
}

static void do_nothing(void *arg)
{
    (void)arg;
    for (;;) pause();  // like the real tasks, it never returns
}

// The thread library keeps its own records at the top of each stack; a task that
// does nothing shows how much, and that is taken off the others.
static void report_stacks(size_t baseline)
{
    for (int i = 1; i < g_nstacks; i++) {
        const task_stack_t *s = &g_stacks[i];
        size_t used = stack_used(s) - baseline;
        printf("task %-7s stack: about %5zu bytes used on this PC, %5u given in speaker.c\n", s->name, used,
               s->declared);
        CHECK(used < s->declared);
    }
}

int main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[1], "voice") && strcmp(argv[1], "novoice"))) {
        fprintf(stderr, "usage: %s voice|novoice en-GB_ta.bin en-GB_kh0_sg.bin\n", argv[0]);
        return 2;
    }
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    g_ta_path = argv[2];
    g_sg_path = argv[3];
    bool voice = strcmp(argv[1], "voice") == 0;

    xTaskCreate(do_nothing, "nothing", 0, NULL, 1, NULL);
    usleep(20000);
    size_t baseline = stack_used(&g_stacks[0]);

    app_config_t cfg;
    config_set_defaults(&cfg);
    cfg.speech_enabled = voice;
    cfg.speech_volume = 65;
    speaker_init(&cfg, true, load(g_ta_path), load(g_sg_path));
    CHECK(speaker_has_voice() == voice);

    if (voice) {
        scenario_reply();
        scenario_stop();
        scenario_after_stop();
        scenario_error();
        scenario_speaker_fails();
    } else {
        scenario_voice_off();
    }
    report_stacks(baseline);
    printf("speaker %s: %d passed, %d failed\n", argv[1], g_pass, g_fail);
    return g_fail ? 1 : 0;
}

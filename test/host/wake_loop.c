// The wake word detector on a PC: the on-device voice says each wake word (and
// some ordinary sentences, and the other wake words) at a few levels, with a
// little background noise. On low and normal sensitivity the detector must
// never fire on anything but its own word; on high, which trades false alarms
// for catching more, they're only reported. On normal and high it must catch
// its word at most levels (a real voice differs from this synthetic one, so a
// single miss isn't held against it).
//   make -C test/host wake TFLM=/path/to/esp-tflite-micro
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "picotts_direct.h"
#include "tts_text.h"
#include "wake_words.h"
#include "wakeword.h"

typedef struct {
    int16_t *pcm;
    size_t n, cap;
} clip_t;

static bool sink(const int16_t *samples, size_t count, void *ctx)
{
    clip_t *c = ctx;
    if (c->n + count > c->cap) {
        c->cap = (c->n + count) * 2 + 16000;
        c->pcm = realloc(c->pcm, c->cap * sizeof(int16_t));
    }
    memcpy(c->pcm + c->n, samples, count * sizeof(int16_t));
    c->n += count;
    return true;
}

static void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) exit(1);
    fclose(f);
    if (len) *len = (size_t)n;
    return buf;
}

static void *alloc16(size_t n, bool fast)
{
    (void)fast;
    void *p = NULL;
    return posix_memalign(&p, 16, n ? n : 16) == 0 ? p : NULL;
}

static uint32_t g_rng = 12345;
static int noise(int amp)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return amp ? (int)(g_rng % (uint32_t)(2 * amp + 1)) - amp : 0;
}

// 1.5 s of quiet, the phrase at `gain`, 1 s of quiet; all with light noise.
static clip_t speak(const char *text, float gain)
{
    char prepared[512];
    tts_prepare(text, 100, 100, prepared, sizeof prepared);
    clip_t voice = {0};
    picotts_direct_say(prepared, sink, &voice);
    clip_t c = {0};
    c.cap = voice.n + 40000;
    c.pcm = calloc(c.cap, sizeof(int16_t));
    c.n = 24000 + voice.n + 16000;
    for (size_t i = 0; i < c.n; i++) {
        float s = (i >= 24000 && i < 24000 + voice.n) ? voice.pcm[i - 24000] * gain : 0.0f;
        s += (float)noise(60);
        c.pcm[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    free(voice.pcm);
    return c;
}

// How to spell each word so the synthetic voice says it right ("Nabu" comes out
// as "nab-you").
static const char *spoken(const wake_word_t *w)
{
    return strcmp(w->file, "okay_nabu") == 0 ? "Okay Naboo" : w->name;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: wake_loop voice_dir models_dir\n");
        return 2;
    }
    char ta_path[512], sg_path[512];
    snprintf(ta_path, sizeof ta_path, "%s/en-GB_ta.bin", argv[1]);
    snprintf(sg_path, sizeof sg_path, "%s/en-GB_kh0_sg.bin", argv[1]);
    void *ta = read_file(ta_path, NULL), *sg = read_file(sg_path, NULL);
    void *mem = malloc(PICOTTS_MEM_SIZE);
    if (!picotts_direct_init(ta, sg, mem, PICOTTS_MEM_SIZE)) {
        printf("FAIL: voice didn't start\n");
        return 1;
    }

    static const char *const others[] = {
        "What's the weather like tomorrow?",
        "Can you set a timer for ten minutes?",
        "Tell me a joke about penguins.",
        "Hey, how are you doing today?",
        "Okay, thanks very much.",
        "Jars of jam are in the cupboard.",
    };
    static const float gains[] = {0.25f, 1.0f, 2.0f};
    int fails = 0, checks = 0, false_alarms = 0;

    static const char *const sens_names[] = {"low", "normal", "high"};
    for (int sens = WAKE_SENS_LOW; sens <= WAKE_SENS_HIGH; sens++)
    for (int m = 0; m < WAKE_WORD_COUNT; m++) {
        const wake_word_t *ww = &WAKE_WORDS[m];
        char path[512];
        snprintf(path, sizeof path, "%s/%s.tflite", argv[2], ww->file);
        size_t len;
        uint8_t *model = read_file(path, &len);
        ww_params_t p = {.cutoff = wake_word_cutoff(ww, (wake_sens_t)sens), .window = ww->window,
                         .arena_bytes = ww->arena_bytes};
        char err[96];
        ww_t *w = ww_create(model, len, &p, alloc16, free, err, sizeof err);
        free(model);
        if (!w) {
            printf("FAIL %s: %s\n", ww->name, err);
            fails++;
            continue;
        }
        printf("%s, %s sensitivity: arena %zu bytes in use (manifest %zu), cutoff %.3f\n", ww->name,
               sens_names[sens], ww_arena_bytes(w), ww->arena_bytes, (double)p.cutoff);

        double audio_s = 0, cpu_s = 0;
        int caught = 0;
        const int n_others = (int)(sizeof others / sizeof *others);
        for (int g = 0; g < 3; g++) {
            for (int t = -1; t < n_others + WAKE_WORD_COUNT; t++) {
                char text[160];
                if (t < 0) snprintf(text, sizeof text, "%s.", spoken(ww));
                else if (t >= n_others) {
                    if (t - n_others == m) continue;
                    snprintf(text, sizeof text, "%s.", spoken(&WAKE_WORDS[t - n_others]));
                }
                const char *say = t < 0 || t >= n_others ? text : others[t];
                clip_t c = speak(say, gains[g]);
                ww_reset(w);
                int hits = 0;
                clock_t t0 = clock();
                for (size_t i = 0; i < c.n; i += 480) {  // 30 ms reads, as on the board
                    size_t n = c.n - i < 480 ? c.n - i : 480;
                    if (ww_feed(w, c.pcm + i, n)) hits++;
                }
                cpu_s += (double)(clock() - t0) / CLOCKS_PER_SEC;
                audio_s += (double)c.n / 16000.0;
                float peak = ww_take_peak(w);
                bool want = t < 0;
                if (want) {
                    if (hits == 1) caught++;
                    bool twice = hits > 1;
                    if (twice) fails++;
                    printf("  %s x%.2f \"%s\": %d detection(s), peak %.2f\n", twice ? "FAIL" : hits ? "ok  " : "miss",
                           (double)gains[g], say, hits, (double)peak);
                } else {
                    bool strict = sens != WAKE_SENS_HIGH;
                    if (strict) checks++;
                    if (hits) {
                        if (strict) fails++;
                        else false_alarms++;
                        printf("  %s x%.2f \"%s\": false alarm, peak %.2f\n", strict ? "FAIL" : "note",
                               (double)gains[g], say, (double)peak);
                    }
                }
            }
        }
        if (sens != WAKE_SENS_LOW) checks++;
        if (sens != WAKE_SENS_LOW && caught < 2) {
            fails++;
            printf("  FAIL: caught at only %d of 3 levels\n", caught);
        }
        printf("  %.1f s of audio in %.2f s of CPU\n", audio_s, cpu_s);
        ww_destroy(w);
    }
    printf("wake words: %d passed, %d failed (%d false alarm(s) on high sensitivity)\n", checks - fails, fails,
           false_alarms);
    return fails ? 1 : 0;
}

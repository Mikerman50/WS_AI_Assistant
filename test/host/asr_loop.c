// Listening, end to end on a PC: the board's own voice (Pico) says questions
// someone might ask, the audio is made quiet or loud and given some room hiss,
// then it goes through exactly what the firmware does with a recording: the
// level adjustment (core/audio_level.c), Oído with the language model
// (components/oido, the model the firmware flashes) and the tidy-up of the text
// (core/asr_text.c). Checks the words that come back.
//   make -C test/host asr
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "asr_text.h"
#include "audio_level.h"
#include "picotts_direct.h"
#include "tasr_nemo.h"
#include "tinyasr.h"
#include "tinyasr_lm.h"
#include "tts_text.h"

static int g_fail, g_pass;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) g_pass++;                                                  \
        else {                                                               \
            g_fail++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

static void *load(const char *dir, const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
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
    *len = (size_t)n;
    return buf;
}

// ---------------------------------------------------------------- speech from the board's voice

typedef struct {
    int16_t *pcm;
    size_t n, cap;
} clip_t;

static bool collect(const int16_t *s, size_t n, void *ctx)
{
    clip_t *c = ctx;
    if (c->n + n > c->cap) {
        c->cap = (c->n + n) * 2;
        c->pcm = realloc(c->pcm, c->cap * sizeof(int16_t));
    }
    memcpy(c->pcm + c->n, s, n * sizeof(int16_t));
    c->n += n;
    return true;
}

static clip_t speak(const char *text)
{
    char prepared[1024];
    tts_prepare(text, 100, 100, prepared, sizeof prepared);
    clip_t c = {0};
    picotts_direct_say(prepared, collect, &c);
    return c;
}

// A "recording": half a second of room hiss, the speech at `dbfs` RMS, half a
// second of hiss, as if the microphone heard it at that level.
static int16_t *record(const clip_t *speech, float dbfs, size_t *n)
{
    size_t pad = 8000;
    *n = speech->n + 2 * pad;
    int16_t *rec = malloc(*n * sizeof(int16_t));
    double sum = 0;
    for (size_t i = 0; i < speech->n; i++) sum += (double)speech->pcm[i] * speech->pcm[i];
    double rms = sqrt(sum / (double)speech->n);
    double scale = 32768.0 * pow(10.0, dbfs / 20.0) / rms;
    unsigned seed = 7;
    for (size_t i = 0; i < *n; i++) {
        seed = seed * 1103515245u + 12345u;
        double hiss = ((double)((seed >> 8) & 0xFFFF) / 65535.0 - 0.5) * 2.0 * 12.0;  // about -72 dBFS
        double s = (i >= pad && i < pad + speech->n) ? speech->pcm[i - pad] * scale : 0.0;
        double y = s + hiss;
        rec[i] = (int16_t)(y > 32767 ? 32767 : (y < -32768 ? -32768 : lround(y)));
    }
    return rec;
}

// ---------------------------------------------------------------- word error rate

static int split_words(const char *s, char words[][32], int max)
{
    int n = 0, len = 0;
    for (const char *p = s;; p++) {
        char c = (char)tolower((unsigned char)*p);
        if (isalnum((unsigned char)c)) {
            if (len < 31) words[n][len++] = c;
        } else if (c == '\'') {
            // "what's" and "whats" count as the same word
        } else {
            if (len) {
                words[n][len] = '\0';
                if (++n == max) return n;
                len = 0;
            }
            if (!*p) break;
        }
    }
    return n;
}

static int word_errors(const char *ref, const char *hyp, int *nref)
{
    static char r[64][32], h[64][32];
    int nr = split_words(ref, r, 64), nh = split_words(hyp, h, 64);
    static int d[65][65];
    for (int i = 0; i <= nr; i++) d[i][0] = i;
    for (int j = 0; j <= nh; j++) d[0][j] = j;
    for (int i = 1; i <= nr; i++)
        for (int j = 1; j <= nh; j++) {
            int sub = d[i - 1][j - 1] + (strcmp(r[i - 1], h[j - 1]) != 0);
            int del = d[i - 1][j] + 1, ins = d[i][j - 1] + 1;
            d[i][j] = sub < del ? (sub < ins ? sub : ins) : (del < ins ? del : ins);
        }
    *nref = nr;
    return d[nr][nh];
}

// ---------------------------------------------------------------- the test

static const char *const k_questions[] = {
    "What's the weather going to be like tomorrow?",
    "How long should I boil an egg for?",
    "Tell me a joke about cats.",
    "What is the capital of Australia?",
    "Can you explain how a rainbow forms?",
    "Remind me what time the bins go out on Thursday.",
    "How many tablespoons are in a cup?",
    "What should I make for dinner tonight?",
};
#define NQ (sizeof k_questions / sizeof k_questions[0])

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s sdcard/models components/oido/model/oido_v2b.tnm\n", argv[0]);
        return 2;
    }
    size_t ta_len, sg_len, lm_len, model_len;
    void *ta = load(argv[1], "en-GB_ta.bin", &ta_len);
    void *sg = load(argv[1], "en-GB_kh0_sg.bin", &sg_len);
    void *lm_blob = load(argv[1], "oido_v2.tlm", &lm_len);
    char *slash = strrchr(argv[2], '/');
    char dir[512];
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - argv[2]) : 1, slash ? argv[2] : ".");
    void *model_blob = load(dir, slash ? slash + 1 : argv[2], &model_len);

    static char pico_mem[PICOTTS_MEM_SIZE];
    if (!picotts_direct_init(ta, sg, pico_mem, sizeof pico_mem)) return 2;
    tasr_nemo_t *model = tasr_nemo_load(model_blob, model_len);
    tasr_lm_t *lm = tasr_lm_load(lm_blob, lm_len);
    CHECK(model && lm);
    if (!model || !lm) return 1;
    tasr_decoder_t *dec = tasr_decoder_create(lm, 1025, 4, 6, tasr_lm_weight(lm, 0.3f), tasr_lm_bonus(lm, 1.0f));

    // A soft voice across the room (only 30 dB above the hiss), normal speech at
    // arm's length, and someone close and loud.
    static const float levels[] = {-42.0f, -30.0f, -12.0f};
    int errors[3] = {0}, raw_errors[3] = {0}, words = 0;
    double audio_s = 0, cpu_s = 0;
    for (size_t q = 0; q < NQ; q++) {
        clip_t speech = speak(k_questions[q]);
        CHECK(speech.n > 16000);
        for (int l = 0; l < 3; l++) {
            size_t n;
            int16_t *rec = record(&speech, levels[l], &n);
            char raw[1024], heard[1024];
            int nref;

            // Without the level adjustment, for comparison.
            tasr_nemo_transcribe(model, rec, (int)n, dec, raw, sizeof raw, NULL, 0, NULL);
            raw_errors[l] += word_errors(k_questions[q], raw, &nref);

            // What the firmware does.
            float gain = audio_speech_gain(rec, n);
            audio_apply_gain(rec, n, gain);
            clock_t t0 = clock();
            tasr_nemo_transcribe(model, rec, (int)n, dec, raw, sizeof raw, NULL, 0, NULL);
            cpu_s += (double)(clock() - t0) / CLOCKS_PER_SEC;
            audio_s += (double)n / 16000.0;
            asr_tidy(raw, heard, sizeof heard);
            int e = word_errors(k_questions[q], heard, &nref);
            errors[l] += e;
            if (l == 0) words += nref;
            printf("%4.0f dBFS x%-5.1f %d wrong: \"%s\"\n", (double)levels[l], (double)gain, e, heard);
            CHECK(heard[0] >= 'A' && heard[0] <= 'Z');  // tidied
            free(rec);
        }
        free(speech.pcm);
    }
    printf("\nword error rate with the level adjustment: %.1f%% (quiet), %.1f%% (normal), %.1f%% (loud)\n",
           100.0 * errors[0] / words, 100.0 * errors[1] / words, 100.0 * errors[2] / words);
    printf("without it:                                %.1f%% (quiet), %.1f%% (normal), %.1f%% (loud)\n",
           100.0 * raw_errors[0] / words, 100.0 * raw_errors[1] / words, 100.0 * raw_errors[2] / words);
    printf("PC time: %.2f s for %.1f s of recordings\n", cpu_s, audio_s);
    // Normal and loud speech: at most 15% of words wrong (upstream measures 6-14% on
    // LibriSpeech for this model). The soft voice is limited by the hiss rather
    // than its level, so it only has to stay usable and not get worse.
    CHECK(errors[1] * 100 <= words * 15);
    CHECK(errors[2] * 100 <= words * 15);
    CHECK(errors[0] * 100 <= words * 30);
    for (int l = 0; l < 3; l++) CHECK(errors[l] <= raw_errors[l]);  // the adjustment never makes it worse
    printf("asr loop: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

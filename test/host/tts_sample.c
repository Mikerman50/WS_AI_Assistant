// Runs the on-device voice (SVOX Pico, en-GB) on a PC, using the same wrapper
// and resource loader as the firmware, and writes the speech to WAV files.
//   make -C test/host tts      -> build/voice_*.wav plus a timing report
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "picotts_direct.h"
#include "tts_text.h"

typedef struct {
    int16_t *pcm;
    size_t n, cap;
    size_t stop_after;  // 0 = never
} clip_t;

static bool sink(const int16_t *samples, size_t count, void *ctx)
{
    clip_t *c = ctx;
    if (c->n + count > c->cap) {
        c->cap = (c->n + count) * 2;
        c->pcm = realloc(c->pcm, c->cap * sizeof(int16_t));
    }
    memcpy(c->pcm + c->n, samples, count * sizeof(int16_t));
    c->n += count;
    return !(c->stop_after && c->n >= c->stop_after);
}

static void *read_file(const char *path)
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
    return buf;
}

static void put_u32(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f);
    fputc((int)(v >> 24), f);
}

static void put_u16(FILE *f, uint16_t v)
{
    fputc(v & 0xff, f);
    fputc(v >> 8, f);
}

static void write_wav(const char *path, const int16_t *pcm, size_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = (uint32_t)(n * 2);
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);
    put_u16(f, 1);  // PCM
    put_u16(f, 1);  // mono
    put_u32(f, PICOTTS_SAMPLE_RATE);
    put_u32(f, PICOTTS_SAMPLE_RATE * 2);
    put_u16(f, 2);
    put_u16(f, 16);
    fwrite("data", 1, 4, f);
    put_u32(f, bytes);
    fwrite(pcm, 2, n, f);  // little-endian host
    fclose(f);
}

static double rms_dbfs(const int16_t *pcm, size_t n)
{
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += (double)pcm[i] * pcm[i];
    return n ? 20.0 * log10(sqrt(sum / (double)n) / 32768.0 + 1e-9) : -96.0;
}

static int g_fail;

static double say(const char *name, const char *text, int speed, int pitch, const char *dir, bool save)
{
    char prepared[1024];
    tts_prepare(text, speed, pitch, prepared, sizeof prepared);
    clip_t c = {0};
    clock_t t0 = clock();
    int r = picotts_direct_say(prepared, sink, &c);
    double cpu = (double)(clock() - t0) / CLOCKS_PER_SEC;
    double secs = (double)c.n / PICOTTS_SAMPLE_RATE;
    double db = rms_dbfs(c.pcm, c.n);
    printf("%-10s %5.2f s audio, %5.0f dBFS, %.2f s CPU, result %d: %s\n", name, secs, db, cpu, r, text);
    if (r != 0 || secs < 0.3 || db < -40.0) {
        printf("  FAIL: expected audible speech\n");
        g_fail++;
    }
    if (save) {
        char path[256];
        snprintf(path, sizeof path, "%s/voice_%s.wav", dir, name);
        write_wav(path, c.pcm, c.n);
    }
    free(c.pcm);
    return secs;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *lang = argc > 2 ? argv[2] : "../../sdcard/models";
    char ta_path[512], sg_path[512];
    snprintf(ta_path, sizeof ta_path, "%s/en-GB_ta.bin", lang);
    snprintf(sg_path, sizeof sg_path, "%s/en-GB_kh0_sg.bin", lang);
    void *ta = read_file(ta_path), *sg = read_file(sg_path);
    void *mem = malloc(PICOTTS_MEM_SIZE);
    if (!picotts_direct_init(ta, sg, mem, PICOTTS_MEM_SIZE)) {
        printf("FAIL: engine didn't start\n");
        return 1;
    }

    say("hello", "Hello! I'm Claude, and I'm living in a very small e-ink screen.", 100, 100, dir, true);
    double normal = say("weather", "Probably, yes. Rain from about two in the afternoon, so take a coat.", 100, 100, dir, true);
    double fast = say("fast", "Probably, yes. Rain from about two in the afternoon, so take a coat.", 140, 100, dir, false);
    double slow = say("slow", "Probably, yes. Rain from about two in the afternoon, so take a coat.", 70, 100, dir, false);
    say("pitch", "Probably, yes. Rain from about two in the afternoon, so take a coat.", 100, 130, dir, false);
    say("symbols", "It costs \xC2\xA3" "5 \xE2\x80\x94 that's \xE2\x80\x9C" "fine\xE2\x80\x9D <b>& cheap</b>.", 100, 100, dir, true);
    if (!(fast < normal * 0.9 && slow > normal * 1.15)) {
        printf("FAIL: speed markup had no effect (normal %.2f, fast %.2f, slow %.2f)\n", normal, fast, slow);
        g_fail++;
    }

    // Stopping part-way returns 1 and leaves the engine ready for the next sentence.
    clip_t c = {.stop_after = 8000};
    int r = picotts_direct_say("This sentence is long enough that it gets cut off part of the way through.", sink, &c);
    printf("stopped    result %d after %.2f s\n", r, (double)c.n / PICOTTS_SAMPLE_RATE);
    if (r != 1) g_fail++;
    free(c.pcm);
    say("after", "Still working after being interrupted.", 100, 100, dir, false);

    // The voice comes out bit for bit the same wherever the engine's memory is,
    // as on the board, where it's started again in different memory for each
    // reply. Two sentences each, so the second one uses memory the first one
    // left behind. (The engine keeps some state between sentences, so the same
    // sentence twice in a row isn't identical; across engines it must be.)
    const char *a = "What should I make for dinner tonight? Something warm, I think.";
    const char *b = "It's ten past seven; the bins go out on Thursday.";
    picotts_direct_shutdown();
    clip_t a1 = {0}, b1 = {0}, a2 = {0}, b2 = {0};
    if (!picotts_direct_init(ta, sg, mem, PICOTTS_MEM_SIZE)) g_fail++;
    picotts_direct_say(a, sink, &a1);
    picotts_direct_say(b, sink, &b1);
    picotts_direct_shutdown();
    char *mem2 = malloc(PICOTTS_MEM_SIZE + 4096 + 48);
    memset(mem2, 0xA5, PICOTTS_MEM_SIZE + 4096 + 48);  // not zero, as heap memory wouldn't be
    if (!picotts_direct_init(ta, sg, mem2 + 4096 + 48, PICOTTS_MEM_SIZE)) g_fail++;
    picotts_direct_say(a, sink, &a2);
    picotts_direct_say(b, sink, &b2);
    bool same = a1.n == a2.n && b1.n == b2.n && a1.n > 0 && b1.n > 0 &&
                memcmp(a1.pcm, a2.pcm, a1.n * sizeof(int16_t)) == 0 &&
                memcmp(b1.pcm, b2.pcm, b1.n * sizeof(int16_t)) == 0;
    printf("repeat     %s\n", same ? "identical in different memory" : "DIFFERENT in different memory");
    if (!same) g_fail++;
    free(a1.pcm);
    free(b1.pcm);
    free(a2.pcm);
    free(b2.pcm);

    picotts_direct_shutdown();
    free(mem);
    free(mem2);
    printf("%s\n", g_fail ? "TTS CHECK FAILED" : "tts ok");
    return g_fail ? 1 : 0;
}

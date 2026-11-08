// Runs the voice and the recogniser built through their component files: the
// voice says a sentence and the recogniser has to hear it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "picotts_direct.h"
#include "tasr_nemo.h"

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

static void *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = aligned_alloc(16, ((size_t)n + 15) & ~(size_t)15);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) exit(1);
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s sdcard/models\n", argv[0]);
        return 2;
    }
    char path[512];
    size_t n;
    snprintf(path, sizeof path, "%s/en-GB_ta.bin", argv[1]);
    void *ta = load(path, &n);
    snprintf(path, sizeof path, "%s/en-GB_kh0_sg.bin", argv[1]);
    void *sg = load(path, &n);
    static char mem[PICOTTS_MEM_SIZE];
    if (!picotts_direct_init(ta, sg, mem, sizeof mem)) return 1;
    clip_t c = {0};
    picotts_direct_say("Tell me a joke about cats.", collect, &c);

    size_t model_len;
    void *model_blob = load(MODEL_FILE, &model_len);
    tasr_nemo_t *model = tasr_nemo_load(model_blob, model_len);
    if (!model) return 1;
    char text[256];
    tasr_nemo_transcribe(model, c.pcm, (int)c.n, NULL, text, sizeof text, NULL, 0, NULL);
    printf("voice said \"Tell me a joke about cats.\", recogniser heard \"%s\"\n", text);
    if (strcmp(text, "tell me a joke about cats") != 0) return 1;
    puts("components ok");
    return 0;
}

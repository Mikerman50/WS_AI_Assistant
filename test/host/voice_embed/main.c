// Checks the voice data embedded by components/picotts/voice_data.S.
#include <stdint.h>
#include <stdio.h>

#include "picotts_direct.h"

extern const uint8_t picotts_ta[], picotts_ta_end[], picotts_sg[], picotts_sg_end[];

static size_t s_samples;

static bool count(const int16_t *samples, size_t n, void *ctx)
{
    (void)samples;
    (void)ctx;
    s_samples += n;
    return true;
}

int main(void)
{
    size_t ta = (size_t)(picotts_ta_end - picotts_ta), sg = (size_t)(picotts_sg_end - picotts_sg);
    printf("text analysis: %zu bytes, signal generation: %zu bytes\n", ta, sg);
    if (ta != 412248 || sg != 584436) {
        fprintf(stderr, "embedded voice files have the wrong size\n");
        return 1;
    }
    if ((uintptr_t)picotts_ta % 16 || (uintptr_t)picotts_sg % 16) {
        fprintf(stderr, "embedded voice files are not aligned\n");
        return 1;
    }
    static char mem[PICOTTS_MEM_SIZE];
    if (!picotts_direct_init(picotts_ta, picotts_sg, mem, sizeof mem)) return 1;
    int r = picotts_direct_say("Hello from the embedded voice.", count, NULL);
    printf("spoke %.2f s (result %d)\n", (double)s_samples / PICOTTS_SAMPLE_RATE, r);
    if (r != 0 || s_samples < PICOTTS_SAMPLE_RATE) return 1;
    puts("voice embedding ok");
    return 0;
}

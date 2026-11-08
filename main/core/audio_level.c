#include "audio_level.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define METER_FLOOR_DB (-55.0f)
#define METER_TOP_DB (-15.0f)

float audio_rms_dbfs(const int16_t *samples, size_t n)
{
    if (n == 0) return -96.0f;
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += (double)samples[i] * (double)samples[i];
    double rms = sqrt(sum / (double)n);
    if (rms < 1.0) return -96.0f;
    return (float)(20.0 * log10(rms / 32768.0));
}

int audio_peak(const int16_t *samples, size_t n)
{
    int peak = 0;
    for (size_t i = 0; i < n; i++) {
        int a = abs((int)samples[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

float audio_meter(float dbfs)
{
    float m = (dbfs - METER_FLOOR_DB) / (METER_TOP_DB - METER_FLOOR_DB);
    return m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
}

float audio_mouth(float dbfs)
{
    float m = (dbfs + 45.0f) / 30.0f;
    return m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
}

#define BLOCK 320  // 20 ms at 16 kHz

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

float audio_speech_gain(const int16_t *samples, size_t n)
{
    size_t nb = n / BLOCK;
    if (nb == 0) return 1.0f;
    float *e = malloc(nb * 2 * sizeof(float));  // block energies, then a sorted copy
    if (!e) return 1.0f;
    float *sorted = e + nb;
    for (size_t b = 0; b < nb; b++) {
        double sum = 0.0;
        for (size_t i = 0; i < BLOCK; i++) {
            double x = samples[b * BLOCK + i] / 32768.0;
            sum += x * x;
        }
        e[b] = (float)(sum / BLOCK);
    }
    memcpy(sorted, e, nb * sizeof(float));
    qsort(sorted, nb, sizeof(float), cmp_float);
    float floor = sorted[nb / 10];
    float threshold = 8.0f * floor;  // 9 dB above the background
    if (threshold < 1e-8f) threshold = 1e-8f;
    double sum = 0.0;
    size_t voiced = 0;
    for (size_t b = 0; b < nb; b++) {
        if (e[b] > threshold) {
            sum += e[b];
            voiced++;
        }
    }
    free(e);
    if (voiced == 0) return 1.0f;
    float rms = (float)sqrt(sum / (double)voiced);
    float gain = 0.1f / rms;  // -20 dBFS
    return gain < 0.25f ? 0.25f : (gain > 64.0f ? 64.0f : gain);
}

void audio_apply_gain(int16_t *samples, size_t n, float gain)
{
    for (size_t i = 0; i < n; i++) {
        float y = samples[i] * gain;
        samples[i] = (int16_t)(y > 32767.0f ? 32767.0f : (y < -32768.0f ? -32768.0f : lrintf(y)));
    }
}

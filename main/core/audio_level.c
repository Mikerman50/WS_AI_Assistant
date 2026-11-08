#include "audio_level.h"

#include <math.h>
#include <stdlib.h>

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

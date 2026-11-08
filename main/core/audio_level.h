// Loudness of 16-bit microphone samples, for the listening meter and the log.
#pragma once

#include <stddef.h>
#include <stdint.h>

// RMS level in dB relative to full scale; -96 for silence.
float audio_rms_dbfs(const int16_t *samples, size_t n);

// Largest absolute sample value.
int audio_peak(const int16_t *samples, size_t n);

// Maps a level to the meter's 0..1 range (-55 dBFS and below = 0, -15 and above = 1).
float audio_meter(float dbfs);

// How open the mouth should be for speech at this level, 0..1
// (-45 dBFS and below = closed, -15 and above = wide open).
float audio_mouth(float dbfs);

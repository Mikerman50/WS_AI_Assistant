// The ES8311 codec on I2S: the built-in microphone and the speaker.
//
// Set up the same way as XiaoZhi's support for this board (duplex I2S on port 0,
// MCLK = 256 x sample rate, ES8311 at its default I2C address), via Espressif's
// esp_codec_dev component. Microphone and speaker share one 16 kHz stream; the
// codec is opened while either is in use.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define AUDIO_SAMPLE_RATE 16000  // speech-to-text and the Pico voice both use 16 kHz mono 16-bit

// Creates the I2S channels and the codec. Call once after board_init().
esp_err_t audio_init(i2c_master_bus_handle_t i2c_bus, int mic_gain_db);

// Starts the microphone. The speaker amplifier stays off.
esp_err_t audio_mic_open(void);

// Fills `samples` (blocking, about n / 16 ms). Returns ESP_OK or an error.
esp_err_t audio_mic_read(int16_t *samples, size_t n);

void audio_mic_close(void);

// Starts the speaker at `volume` (0-100) and switches the amplifier on.
esp_err_t audio_speaker_open(int volume);

// Plays samples (blocking at the playback rate; the DMA buffer holds ~90 ms).
esp_err_t audio_speaker_write(const int16_t *samples, size_t n);

// Switches the amplifier off and lets the codec idle.
void audio_speaker_close(void);

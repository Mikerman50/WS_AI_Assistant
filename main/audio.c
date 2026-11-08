#include "audio.h"

#include <stdbool.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
// esp_codec_dev (from the component registry, see main/idf_component.yml)
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "audio";

static i2s_chan_handle_t s_tx, s_rx;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if;
static esp_codec_dev_handle_t s_dev;
static float s_mic_gain_db = 30.0f;
static SemaphoreHandle_t s_lock;
static int s_users;           // microphone and/or speaker currently using the codec
static bool s_mic, s_speaker;

static esp_err_t create_i2s(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear_after_cb = true;  // the speaker side sends silence when idle
    esp_err_t err = i2s_new_channel(&chan, &s_tx, &s_rx);
    if (err != ESP_OK) return err;

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din = PIN_I2S_DIN,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if ((err = i2s_channel_init_std_mode(s_tx, &std)) != ESP_OK) return err;
    if ((err = i2s_channel_init_std_mode(s_rx, &std)) != ESP_OK) return err;
    // esp_codec_dev expects the channels enabled; it pauses them while reconfiguring.
    if ((err = i2s_channel_enable(s_tx)) != ESP_OK) return err;
    return i2s_channel_enable(s_rx);
}

esp_err_t audio_init(i2c_master_bus_handle_t i2c_bus, int mic_gain_db)
{
    s_mic_gain_db = (float)mic_gain_db;
    s_lock = xSemaphoreCreateMutex();
    if (!i2c_bus) return ESP_ERR_INVALID_STATE;
    vTaskDelay(pdMS_TO_TICKS(20));  // codec power came up in board_init()

    esp_err_t err = create_i2s();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed: %s", esp_err_to_name(err));
        return err;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .rx_handle = s_rx, .tx_handle = s_tx};
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {.port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus};
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    s_gpio_if = audio_codec_new_gpio();
    if (!s_data_if || !s_ctrl_if || !s_gpio_if) {
        ESP_LOGE(TAG, "codec interfaces failed");
        return ESP_FAIL;
    }

    // Hold the codec's digital blocks in reset briefly, as XiaoZhi does; opening
    // the codec releases it.
    uint8_t reset = 0x1F;
    if (s_ctrl_if->write_reg(s_ctrl_if, 0x00, 1, &reset, 1) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "no answer from the ES8311 codec on I2C");
        return ESP_ERR_NOT_FOUND;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    es8311_codec_cfg_t es = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = -1,  // we switch the amplifier ourselves, so recording doesn't pop the speaker
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
    };
    s_codec_if = es8311_codec_new(&es);
    if (!s_codec_if) {
        ESP_LOGE(TAG, "ES8311 setup failed");
        return ESP_FAIL;
    }
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) return ESP_FAIL;
    ESP_LOGI(TAG, "codec ready, mic gain %d dB", mic_gain_db);
    return ESP_OK;
}

// The codec is opened when the first user (microphone or speaker) needs it and
// closed when the last one is done.
static esp_err_t acquire(void)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_users == 0) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .sample_rate = AUDIO_SAMPLE_RATE,
        };
        if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "couldn't open the codec");
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK) s_users++;
    xSemaphoreGive(s_lock);
    return err;
}

static void release(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_users > 0 && --s_users == 0) esp_codec_dev_close(s_dev);
    xSemaphoreGive(s_lock);
}

esp_err_t audio_mic_open(void)
{
    if (s_mic) return ESP_OK;
    esp_err_t err = acquire();
    if (err != ESP_OK) return err;
    esp_codec_dev_set_in_gain(s_dev, s_mic_gain_db);
    s_mic = true;
    return ESP_OK;
}

esp_err_t audio_mic_read(int16_t *samples, size_t n)
{
    if (!s_mic) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_read(s_dev, samples, (int)(n * sizeof(int16_t))) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

void audio_mic_close(void)
{
    if (!s_mic) return;
    s_mic = false;
    release();
}

esp_err_t audio_speaker_open(int volume)
{
    if (s_speaker) return ESP_OK;
    esp_err_t err = acquire();
    if (err != ESP_OK) return err;
    esp_codec_dev_set_out_vol(s_dev, volume);
    vTaskDelay(pdMS_TO_TICKS(20));  // let the DAC settle before the amplifier comes on: no pop
    gpio_set_level(PIN_AMP_EN, 1);
    s_speaker = true;
    return ESP_OK;
}

esp_err_t audio_speaker_write(const int16_t *samples, size_t n)
{
    if (!s_speaker) return ESP_ERR_INVALID_STATE;
    // esp_codec_dev takes a non-const pointer but only reads from it.
    int r = esp_codec_dev_write(s_dev, (void *)(uintptr_t)samples, (int)(n * sizeof(int16_t)));
    return r == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

void audio_speaker_close(void)
{
    if (!s_speaker) return;
    vTaskDelay(pdMS_TO_TICKS(100));  // let the last ~90 ms in the DMA buffer play out
    gpio_set_level(PIN_AMP_EN, 0);
    s_speaker = false;
    release();
}

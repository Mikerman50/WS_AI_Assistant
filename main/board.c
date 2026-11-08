#include "board.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

#define BAT_CHANNEL ADC_CHANNEL_3  // GPIO4
#define LONG_PRESS_US (1500 * 1000)

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static board_btn_cb_t s_btn_cb;
static i2c_master_bus_handle_t s_i2c;

void board_power_hold(void)
{
    // Level first, then release any deep-sleep hold, so the latch never dips low.
    gpio_set_level(PIN_PWR_HOLD, 1);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_PWR_HOLD,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_PWR_HOLD, 1);
    gpio_hold_dis(PIN_PWR_HOLD);
}

void board_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_LED) | (1ULL << PIN_EPD_PWR) | (1ULL << PIN_AUDIO_PWR) | (1ULL << PIN_AMP_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_LED, 1);        // LED off
    gpio_set_level(PIN_EPD_PWR, 0);    // panel on
    gpio_set_level(PIN_AUDIO_PWR, 0);  // codec on (it idles at low power until opened)
    gpio_set_level(PIN_AMP_EN, 0);     // amplifier off until there is something to play

    // Same settings as XiaoZhi's support for this board: port 0, internal pull-ups.
    i2c_master_bus_config_t bus = {
        .i2c_port = 0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    if (i2c_new_master_bus(&bus, &s_i2c) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus failed to start");
        s_i2c = NULL;
    }

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_PWR_KEY) | (1ULL << PIN_BOOT_KEY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit, &s_adc) == ESP_OK) {
        adc_oneshot_chan_cfg_t chan = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
        adc_oneshot_config_channel(s_adc, BAT_CHANNEL, &chan);
        adc_cali_curve_fitting_config_t cal = {
            .unit_id = ADC_UNIT_1,
            .chan = BAT_CHANNEL,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
    } else {
        s_adc = NULL;
    }
    ESP_LOGI(TAG, "battery %d mV", board_battery_mv());
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c;
}

void board_led(bool on)
{
    gpio_set_level(PIN_LED, on ? 0 : 1);
}

int board_battery_mv(void)
{
    if (!s_adc) return -1;
    int sum = 0, raw = 0, mv = 0;
    for (int i = 0; i < 8; i++) {
        if (adc_oneshot_read(s_adc, BAT_CHANNEL, &raw) != ESP_OK) return -1;
        sum += raw;
    }
    raw = sum / 8;
    if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) return mv * 2;
    return raw * 3300 / 4095 * 2;  // uncalibrated estimate
}

void board_power_off(void)
{
    // While PWR is held the button itself keeps the board powered, so wait for release.
    for (int i = 0; i < 500 && gpio_get_level(PIN_PWR_KEY) == 0; i++) vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "releasing the power latch");
    gpio_hold_dis(PIN_PWR_HOLD);
    gpio_set_level(PIN_PWR_HOLD, 0);
    vTaskDelay(pdMS_TO_TICKS(1500));
    gpio_set_level(PIN_PWR_HOLD, 1);  // still running, so USB is powering us
    ESP_LOGI(TAG, "still powered over USB, staying on");
}

typedef struct {
    gpio_num_t pin;
    board_btn_t id;
    bool armed;  // false until the button has been seen released once
    bool down;
    int stable;
    int64_t t_down;
    bool long_sent;
} btn_state_t;

static void buttons_task(void *arg)
{
    (void)arg;
    btn_state_t b[2] = {
        {.pin = PIN_BOOT_KEY, .id = BTN_BOOT},
        {.pin = PIN_PWR_KEY, .id = BTN_PWR},
    };
    for (;;) {
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < 2; i++) {
            btn_state_t *k = &b[i];
            bool pressed = gpio_get_level(k->pin) == 0;
            if (!k->armed) {  // ignore the press that switched the board on
                if (!pressed) k->armed = true;
                continue;
            }
            if (pressed != k->down) {
                if (++k->stable < 3) continue;  // 60 ms debounce
                k->stable = 0;
                k->down = pressed;
                if (pressed) {
                    k->t_down = now;
                    k->long_sent = false;
                } else if (!k->long_sent) {
                    s_btn_cb(k->id, BTN_SHORT);
                }
            } else {
                k->stable = 0;
                if (k->down && !k->long_sent && now - k->t_down > LONG_PRESS_US) {
                    k->long_sent = true;
                    s_btn_cb(k->id, BTN_LONG);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void board_buttons_start(board_btn_cb_t cb)
{
    s_btn_cb = cb;
    xTaskCreate(buttons_task, "buttons", 4096, NULL, 5, NULL);
}

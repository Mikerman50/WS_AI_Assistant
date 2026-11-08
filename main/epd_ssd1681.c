// SSD1681 (Waveshare 1.54" V2 panel). Command sequence follows the SSD1681
// datasheet and GxEPD2's driver for this panel (GxEPD2_154_D67): full updates
// use the OTP "mode 1" waveform; partial updates use "mode 2", which compares
// RAM 0x24 (new image) with RAM 0x26 (previous image) and drives only the
// pixels that changed.
#include <assert.h>
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";

#define EPD_BYTES (GFX_STRIDE * GFX_H)  // 5000

static int s_rotation;       // quarter turns, from config.json
static int s_busy_level = 1; // BUSY pin level while the panel is busy
static spi_device_handle_t s_spi;
static uint8_t *s_new;    // frame being sent, panel format (1 = white)
static uint8_t *s_shown;  // what the panel shows now
static bool s_ready;      // controller initialised since reset/sleep
static bool s_analog_on;  // charge pump left running by a partial update
static bool s_have_image; // s_shown is valid

static void send_small(const uint8_t *d, size_t n, int dc)
{
    gpio_set_level(PIN_EPD_DC, dc);
    spi_transaction_t t = {.flags = SPI_TRANS_USE_TXDATA, .length = n * 8};
    memcpy(t.tx_data, d, n);
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void cmd(uint8_t c)
{
    send_small(&c, 1, 0);
}

static void data(const uint8_t *d, size_t n)
{
    while (n > 0) {
        size_t k = n > 4 ? 4 : n;
        send_small(d, k, 1);
        d += k;
        n -= k;
    }
}

static void data1(uint8_t v)
{
    data(&v, 1);
}

static void data_bulk(const uint8_t *d, size_t n)
{
    gpio_set_level(PIN_EPD_DC, 1);
    spi_transaction_t t = {.length = n * 8, .tx_buffer = d};
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void wait_idle(int timeout_ms, const char *what)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_EPD_BUSY) == s_busy_level) {
        if ((esp_timer_get_time() - t0) / 1000 > timeout_ms) {
            ESP_LOGW(TAG, "busy timeout during %s; try display.busy_active_high in config.json", what);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void hw_reset(void)
{
    gpio_set_level(PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void controller_init(void)
{
    hw_reset();
    wait_idle(200, "reset");
    cmd(0x12);  // software reset
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_idle(200, "software reset");

    static const uint8_t driver_output[] = {0xC7, 0x00, 0x00};  // 200 gate lines
    static const uint8_t x_range[] = {0x00, 0x18};              // RAM columns 0..24 (bytes)
    static const uint8_t y_range[] = {0x00, 0x00, 0xC7, 0x00};  // RAM rows 0..199
    cmd(0x01);
    data(driver_output, sizeof driver_output);
    cmd(0x11);
    data1(0x03);  // data entry: X increment, Y increment
    cmd(0x44);
    data(x_range, sizeof x_range);
    cmd(0x45);
    data(y_range, sizeof y_range);
    cmd(0x3C);
    data1(0x05);  // border waveform
    cmd(0x18);
    data1(0x80);  // internal temperature sensor
    s_ready = true;
    s_analog_on = false;
}

static void write_ram(uint8_t reg, const uint8_t *img)
{
    static const uint8_t y0[] = {0x00, 0x00};
    cmd(0x4E);
    data1(0x00);
    cmd(0x4F);
    data(y0, sizeof y0);
    cmd(reg);
    data_bulk(img, EPD_BYTES);
}

static void update(uint8_t mode, int timeout_ms)
{
    cmd(0x22);
    data1(mode);
    cmd(0x20);
    wait_idle(timeout_ms, "update");
}

// Canvas (1 = ink) -> panel RAM (1 = white), applying the configured rotation.
static void convert(const gfx_t *g, uint8_t *out)
{
    if (s_rotation == 0) {
        for (int i = 0; i < EPD_BYTES; i++) out[i] = (uint8_t)~g->bits[i];
        return;
    }
    memset(out, 0xFF, EPD_BYTES);
    for (int y = 0; y < GFX_H; y++) {
        for (int x = 0; x < GFX_W; x++) {
            if (!gfx_get(g, x, y)) continue;
            int px, py;
            switch (s_rotation) {
            case 1: px = GFX_W - 1 - y; py = x; break;
            case 2: px = GFX_W - 1 - x; py = GFX_H - 1 - y; break;
            default: px = y; py = GFX_H - 1 - x; break;
            }
            out[py * GFX_STRIDE + (px >> 3)] &= (uint8_t)~(0x80 >> (px & 7));
        }
    }
}

void epd_init(int rotation, bool busy_active_high)
{
    s_rotation = rotation & 3;
    s_busy_level = busy_active_high ? 1 : 0;
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_EPD_DC) | (1ULL << PIN_EPD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_config_t in = {.pin_bit_mask = 1ULL << PIN_EPD_BUSY, .mode = GPIO_MODE_INPUT};
    ESP_ERROR_CHECK(gpio_config(&in));

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_EPD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_EPD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_BYTES + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_EPD_CS,
        .queue_size = 2,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    s_new = heap_caps_malloc(EPD_BYTES, MALLOC_CAP_DMA);
    s_shown = heap_caps_malloc(EPD_BYTES, MALLOC_CAP_DMA);
    assert(s_new && s_shown);

    vTaskDelay(pdMS_TO_TICKS(10));  // panel power was switched on in board_init()
    controller_init();
}

void epd_show_full(const gfx_t *g)
{
    if (!s_ready) controller_init();
    convert(g, s_new);
    write_ram(0x26, s_new);  // the next partial update compares against this
    write_ram(0x24, s_new);
    update(0xF7, 5000);      // full waveform, then charge pump off
    s_analog_on = false;
    memcpy(s_shown, s_new, EPD_BYTES);
    s_have_image = true;
}

void epd_show_partial(const gfx_t *g)
{
    if (!s_have_image) {
        epd_show_full(g);  // the first frame after power-up has to be a full one
        return;
    }
    convert(g, s_new);
    if (memcmp(s_new, s_shown, EPD_BYTES) == 0) return;
    if (!s_ready) {  // woken from sleep: tell the controller what is on screen
        controller_init();
        write_ram(0x26, s_shown);
    }
    write_ram(0x24, s_new);
    update(0xFC, 1500);  // mode 2: only changed pixels are driven
    s_analog_on = true;
    write_ram(0x26, s_new);
    memcpy(s_shown, s_new, EPD_BYTES);
}

void epd_power_off(void)
{
    if (s_ready && s_analog_on) {
        update(0x83, 500);
        s_analog_on = false;
    }
}

void epd_sleep(void)
{
    if (!s_ready) return;
    epd_power_off();
    cmd(0x10);
    data1(0x01);  // deep sleep mode 1
    s_ready = false;
}

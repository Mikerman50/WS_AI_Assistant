// Waveshare ESP32-S3-ePaper-1.54 (V2): pins, power latch, buttons, battery.
//
// GPIO map from Waveshare's schematic as tabulated on espboards.dev (cross-checked
// there against Waveshare's ESP-IDF demos). If something doesn't respond, compare
// with the pin definitions in Waveshare's V2 example code first.
#pragma once

#include <stdbool.h>

// E-paper (SSD1681, 200x200, 4-wire SPI)
#define PIN_EPD_SCK 12
#define PIN_EPD_MOSI 13
#define PIN_EPD_CS 11
#define PIN_EPD_DC 10
#define PIN_EPD_RST 9
#define PIN_EPD_BUSY 8
#define PIN_EPD_PWR 6  // LOW = panel powered

// Shared I2C bus: PCF85063 RTC (0x51), SHTC3 (0x70), ES8311 codec (0x18), touch
#define PIN_I2C_SDA 47
#define PIN_I2C_SCL 48
#define PIN_RTC_INT 5

// Audio: ES8311 codec + NS4150B amplifier (used from milestone 2)
#define PIN_I2S_MCLK 14
#define PIN_I2S_BCLK 15
#define PIN_I2S_WS 38
#define PIN_I2S_DIN 16   // codec -> ESP32 (microphone)
#define PIN_I2S_DOUT 45  // ESP32 -> codec (speaker)
#define PIN_AMP_EN 46    // HIGH = amplifier on
#define PIN_AUDIO_PWR 42 // LOW = codec powered

// microSD, 1-bit SDMMC (used from milestone 4)
#define PIN_SD_CLK 39
#define PIN_SD_CMD 41
#define PIN_SD_D0 40

// Power and buttons
#define PIN_BAT_ADC 4    // ADC1 channel 3, sees VBAT / 2
#define PIN_PWR_HOLD 17  // HIGH keeps battery power latched on
#define PIN_PWR_KEY 18   // PWR button, LOW while pressed
#define PIN_BOOT_KEY 0   // BOOT button, LOW while pressed
#define PIN_LED 3        // green LED, LOW = on

typedef enum { BTN_BOOT, BTN_PWR } board_btn_t;
typedef enum { BTN_SHORT, BTN_LONG } board_btn_event_t;
typedef void (*board_btn_cb_t)(board_btn_t button, board_btn_event_t event);

// Call first thing in app_main: on battery, the board switches off as soon as the
// PWR button is released unless firmware holds GPIO17 high.
void board_power_hold(void);

void board_init(void);

// Releases the power latch. Returns only if the board stays on (USB power).
void board_power_off(void);

int board_battery_mv(void);  // -1 if the reading failed

// Starts the button poller. The PWR press that switched the board on is ignored.
void board_buttons_start(board_btn_cb_t cb);

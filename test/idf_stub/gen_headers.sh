#!/usr/bin/env bash
# Writes one-line ESP-IDF header stand-ins (each includes idf_stub.h) into the
# directory given, plus a small sdkconfig.h. Used by check.sh and by the PC
# simulation of the speaker (test/host, make speaker).
set -euo pipefail
gen="$1"
headers="esp_err.h esp_log.h esp_timer.h esp_heap_caps.h esp_idf_version.h esp_system.h esp_random.h
freertos/FreeRTOS.h freertos/task.h freertos/queue.h freertos/semphr.h freertos/event_groups.h
driver/gpio.h driver/spi_master.h driver/usb_serial_jtag.h driver/usb_serial_jtag_vfs.h
esp_vfs_usb_serial_jtag.h esp_adc/adc_oneshot.h esp_adc/adc_cali.h esp_adc/adc_cali_scheme.h
esp_event.h esp_netif.h esp_wifi.h nvs_flash.h nvs.h esp_http_client.h esp_crt_bundle.h
driver/sdmmc_host.h sdmmc_cmd.h esp_vfs_fat.h driver/i2c_master.h driver/i2s_std.h esp_partition.h"

rm -rf "$gen"
mkdir -p "$gen"
for h in $headers; do
    mkdir -p "$gen/$(dirname "$h")"
    echo '#include "idf_stub.h"' > "$gen/$h"
done
printf '#define CONFIG_IDF_TARGET_ESP32S3 1\n#define CONFIG_CODEC_ES8311_SUPPORT 1\n' > "$gen/sdkconfig.h"

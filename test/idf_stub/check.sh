#!/usr/bin/env bash
# Syntax-checks every firmware source against minimal ESP-IDF stub headers with
# the host compiler (cJSON.h is the real one ESP-IDF ships). Catches C mistakes
# without an ESP-IDF install; the real build (idf.py build or the GitHub
# Actions workflow) is still the final word on API details.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
gen="$here/build/inc"
CC="${CC:-cc}"

headers="esp_err.h esp_log.h esp_timer.h esp_heap_caps.h esp_idf_version.h esp_system.h
freertos/FreeRTOS.h freertos/task.h freertos/queue.h freertos/semphr.h freertos/event_groups.h
driver/gpio.h driver/spi_master.h driver/usb_serial_jtag.h driver/usb_serial_jtag_vfs.h
esp_vfs_usb_serial_jtag.h esp_adc/adc_oneshot.h esp_adc/adc_cali.h esp_adc/adc_cali_scheme.h
esp_event.h esp_netif.h esp_wifi.h nvs_flash.h nvs.h esp_http_client.h esp_crt_bundle.h
driver/sdmmc_host.h sdmmc_cmd.h esp_vfs_fat.h"

rm -rf "$gen"
mkdir -p "$gen"
for h in $headers; do
    mkdir -p "$gen/$(dirname "$h")"
    echo '#include "idf_stub.h"' > "$gen/$h"
done

count=0
for src in "$root"/main/*.c "$root"/main/core/*.c; do
    "$CC" -std=gnu11 -fsyntax-only -Wall -Wextra -Werror -DESP_PLATFORM \
        -I"$gen" -I"$here" -I"$root/main" -I"$root/main/core" \
        -I"$root/test/host/third_party/cJSON" "$src"
    count=$((count + 1))
done
echo "syntax check passed ($count files)"

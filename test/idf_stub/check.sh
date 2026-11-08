#!/usr/bin/env bash
# Syntax-checks every firmware source with the host compiler against minimal
# ESP-IDF stub headers (declarations copied from ESP-IDF 5.5.6) plus the real
# cJSON and esp_codec_dev headers. Catches C mistakes without an ESP-IDF
# install; the real build (idf.py build or the GitHub Actions workflow) is
# still the final word.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
gen="$here/build/inc"
CC="${CC:-cc}"

bash "$here/gen_headers.sh" "$gen"

# Pass 1: as ESP-IDF compiles (GNU C). Pass 2: strict ISO C with the ESP32-S3's
# 32-bit integer types (see xtensa_types/stdint.h), to catch printf format
# mismatches that only fail on the real toolchain.
# The voice component's own glue is checked too (the SVOX engine under
# components/picotts/pico is third-party code, built with warnings off).
pico="$root/components/picotts"
count=0
for src in "$root"/main/*.c "$root"/main/core/*.c "$pico"/picotts_esp.c "$pico"/picotts_direct.c; do
    for pass in gnu xtensa; do
        if [ "$pass" = gnu ]; then std=-std=gnu11; types=""; else std=-std=c11; types="-I$here/xtensa_types"; fi
        "$CC" $std -fsyntax-only -Wall -Wextra -Wformat=2 -Werror -DESP_PLATFORM $types \
            -I"$gen" -I"$here" -I"$root/main" -I"$root/main/core" -I"$pico/include" \
            -I"$root/test/host/third_party/cJSON" -I"$here/esp_codec_dev" \
            $(case "$src" in "$pico"/*) echo "-I$pico/pico/lib -I$pico";; esac) "$src"
    done
    count=$((count + 1))
done
echo "syntax check passed ($count files, 2 passes each)"

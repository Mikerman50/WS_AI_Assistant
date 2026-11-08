#include "sdcard.h"

#include <stdio.h>

#include "board.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";

static sdmmc_card_t *s_card;

sdcard_status_t sdcard_mount(void)
{
    if (s_card) return SDCARD_OK;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0 = PIN_SD_D0;
    slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;  // the defaults name pins this board uses elsewhere
    slot.d4 = slot.d5 = slot.d6 = slot.d7 = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,  // never wipe someone's card
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SDCARD_ROOT, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "SD card not mounted: %s", esp_err_to_name(err));
        return err == ESP_FAIL ? SDCARD_NOT_FAT : SDCARD_NO_CARD;
    }
    return SDCARD_OK;
}

void sdcard_unmount(void)
{
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount(SDCARD_ROOT, s_card);
    s_card = NULL;
}

void *sdcard_load(const char *path, size_t max_bytes, size_t *len)
{
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size <= 0 || (size_t)size > max_bytes || fseek(f, 0, SEEK_SET) != 0) {
        if (size > 0) ESP_LOGE(TAG, "%s is too big (%ld bytes)", path, size);
        fclose(f);
        return NULL;
    }
    char *buf = heap_caps_aligned_alloc(16, (size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "no memory for %s (%ld bytes)", path, size);
        fclose(f);
        return NULL;
    }
    int64_t t0 = esp_timer_get_time();
    size_t got = 0;
    while (got < (size_t)size) {
        size_t want = (size_t)size - got < 32768 ? (size_t)size - got : 32768;
        size_t n = fread(buf + got, 1, want, f);
        if (n == 0) break;
        got += n;
    }
    fclose(f);
    if (got != (size_t)size) {
        ESP_LOGE(TAG, "couldn't read all of %s", path);
        heap_caps_free(buf);
        return NULL;
    }
    ESP_LOGI(TAG, "loaded %s: %ld bytes in %lld ms", path, size, (long long)((esp_timer_get_time() - t0) / 1000));
    *len = (size_t)size;
    return buf;
}

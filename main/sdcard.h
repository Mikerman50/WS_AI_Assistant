// The microSD card (FAT32, 1-bit SDMMC): mounted briefly at start-up to read
// config.json and the files in /models, then unmounted, so the card can be
// taken out while the board runs.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SDCARD_ROOT "/sdcard"

typedef enum { SDCARD_OK, SDCARD_NO_CARD, SDCARD_NOT_FAT } sdcard_status_t;

// Mounts the card at SDCARD_ROOT. Safe to call again while mounted.
sdcard_status_t sdcard_mount(void);
void sdcard_unmount(void);

// Reads a whole file into 16-byte-aligned PSRAM (free with heap_caps_free()).
// NULL if the file is missing, empty, bigger than max_bytes or unreadable.
// `*len` gets its size. The card must be mounted.
void *sdcard_load(const char *path, size_t max_bytes, size_t *len);

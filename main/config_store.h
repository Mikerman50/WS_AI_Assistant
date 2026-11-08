// Loads settings from config.json on the microSD card.
//
// A copy of the last good file is kept in flash, so the assistant still starts
// if the card is removed. If the card has a config.json with a mistake in it,
// that mistake is reported rather than quietly using the old copy.
#pragma once

#include <stddef.h>

#include "config_parse.h"

typedef enum {
    CFG_FROM_SD,     // read and checked from the card (and saved to flash)
    CFG_FROM_SAVED,  // no usable file on the card; using the copy saved last time
    CFG_INVALID,     // config.json on the card has a problem (see err)
    CFG_MISSING      // no card / no file, and nothing saved (see err)
} config_source_t;

// Fills `cfg` (defaults on failure). On CFG_INVALID or CFG_MISSING, `err` holds
// a short message for the screen.
config_source_t config_load(app_config_t *cfg, char *err, size_t errsz);

// Deletes the copy saved in flash.
void config_forget_saved(void);

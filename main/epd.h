// SSD1681 driver for the 1.54" 200x200 black/white panel.
#pragma once

#include <stdbool.h>

#include "gfx.h"

// rotation: quarter turns (0-3); busy_active_high: BUSY pin level while the panel works.
void epd_init(int rotation, bool busy_active_high);

// Full refresh (~2 s, flashes): use on start-up and now and then to clear ghosting.
void epd_show_full(const gfx_t *g);

// Fast differential refresh (~0.3 s): only pixels that changed are driven.
// Does nothing if the frame is identical to what's on screen.
void epd_show_partial(const gfx_t *g);

// Switches the panel's charge pump off between updates. The image stays.
void epd_power_off(void);

// Controller deep sleep. The next show call re-initialises it and carries on
// with partial updates, because the driver remembers what is on screen.
void epd_sleep(void);

// Is the board on USB power or running from its battery? The board has no pin
// that says so, so this goes by what it can see:
//   - a computer on the USB port (the USB console sees its frames): USB;
//   - the battery at 4.15 V or more: USB (a cell on its own sags below that
//     soon after it's unplugged);
//   - the battery voltage rising 50 mV above its recent low while nothing is
//     happening: charging, so USB;
// and it's back on battery when the computer goes away, or the voltage falls
// 50 mV below its recent high and under 4.12 V.
// Readings are only trusted after a minute of quiet, because the speaker and
// the processor working hard pull the voltage down for a while.
//
// Pure logic, unit-tested on the host.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool usb;
    bool had_host;
    bool settled;          // low/high are being tracked
    int64_t quiet_since_ms;
    float smooth_mv;
    float low_mv, high_mv;
} power_src_t;

#define POWER_SETTLE_MS 60000

void power_src_init(power_src_t *p, int64_t now_ms);

// `host`: a computer is connected; `battery_mv`: -1 if the reading failed;
// `quiet`: nothing is playing or working hard right now. Call every few
// seconds. Returns the current verdict (true = USB power).
bool power_src_update(power_src_t *p, bool host, int battery_mv, bool quiet, int64_t now_ms);

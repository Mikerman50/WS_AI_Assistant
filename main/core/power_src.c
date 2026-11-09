#include "power_src.h"

#define FULL_MV 4150.0f
#define FALL_FLOOR_MV 4120.0f
#define CHANGE_MV 50.0f
#define SMOOTHING 0.3f

void power_src_init(power_src_t *p, int64_t now_ms)
{
    p->usb = false;
    p->had_host = false;
    p->settled = false;
    p->quiet_since_ms = now_ms;
    p->smooth_mv = p->low_mv = p->high_mv = 0.0f;
}

bool power_src_update(power_src_t *p, bool host, int battery_mv, bool quiet, int64_t now_ms)
{
    if (host) {
        p->usb = true;
        p->had_host = true;
    } else if (p->had_host) {
        // The computer went away: most likely unplugged. If it was only a
        // computer going to sleep with the port still powered, the voltage
        // brings it back.
        p->had_host = false;
        p->usb = false;
        p->settled = false;
    }

    if (!quiet) {
        p->quiet_since_ms = now_ms;
        p->settled = false;
        return p->usb;
    }
    if (battery_mv < 0 || now_ms - p->quiet_since_ms < POWER_SETTLE_MS) return p->usb;

    float mv = (float)battery_mv;
    if (!p->settled) {
        p->settled = true;
        p->smooth_mv = p->low_mv = p->high_mv = mv;
    } else {
        p->smooth_mv += (mv - p->smooth_mv) * SMOOTHING;
    }
    float v = p->smooth_mv;
    if (v < p->low_mv) p->low_mv = v;
    if (v > p->high_mv) p->high_mv = v;
    if (host) return p->usb;

    if (!p->usb && (v >= FULL_MV || v >= p->low_mv + CHANGE_MV)) {
        p->usb = true;
        p->high_mv = v;
    } else if (p->usb && v < FALL_FLOOR_MV && v <= p->high_mv - CHANGE_MV) {
        p->usb = false;
        p->low_mv = v;
    }
    return p->usb;
}

#include "voice_ctl.h"

#include <string.h>

void voice_ctl_init(voice_ctl_t *v)
{
    memset(v, 0, sizeof *v);
    v->state = VS_IDLE;
}

static bool just_stopped(const voice_ctl_t *v, int64_t now_ms)
{
    return v->has_stopped && now_ms - v->last_stop_ms < VOICE_STOP_GUARD_MS;
}

static void mark_stop(voice_ctl_t *v, int64_t now_ms)
{
    v->last_stop_ms = now_ms;
    v->has_stopped = true;
}

voice_action_t voice_ctl_button(voice_ctl_t *v, bool long_press, int64_t now_ms)
{
    switch (v->state) {
    case VS_IDLE:
        if (long_press) return VA_NEW_CONVERSATION;
        if (just_stopped(v, now_ms)) return VA_NONE;  // second tap of a double tap
        v->state = VS_LISTENING;
        v->listen_started_ms = now_ms;
        return VA_START_LISTENING;

    case VS_LISTENING:
        if (long_press) {
            v->state = VS_IDLE;
            mark_stop(v, now_ms);
            return VA_CANCEL_LISTENING;
        }
        if (now_ms - v->listen_started_ms < VOICE_START_GUARD_MS) return VA_NONE;
        v->state = VS_BUSY;
        mark_stop(v, now_ms);
        return VA_FINISH_LISTENING;

    case VS_BUSY:
        if (!long_press && just_stopped(v, now_ms)) return VA_NONE;
        mark_stop(v, now_ms);
        return VA_INTERRUPT;
    }
    return VA_NONE;
}

void voice_ctl_busy(voice_ctl_t *v)
{
    if (v->state == VS_IDLE) v->state = VS_BUSY;
}

void voice_ctl_listen_ended(voice_ctl_t *v, int64_t now_ms)
{
    if (v->state != VS_LISTENING) return;
    v->state = VS_BUSY;
    mark_stop(v, now_ms);  // a tap that was on its way shouldn't interrupt
}

void voice_ctl_idle(voice_ctl_t *v)
{
    if (v->state == VS_BUSY) v->state = VS_IDLE;
}

void voice_ctl_reset(voice_ctl_t *v)
{
    v->state = VS_IDLE;
}

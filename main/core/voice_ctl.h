// What the BOOT button does, depending on what the assistant is doing.
//
//   idle       tap: start listening          hold: start a new conversation
//   listening  tap: finished, send it        hold: cancel
//   busy       tap or hold: stop the reply   (transcribing, thinking or answering)
//
// A double tap to finish is fine: taps within STOP_GUARD_MS of finishing (or of
// stopping a reply) are ignored, so the second tap doesn't interrupt or start
// listening again. Likewise a "finish" tap in the first START_GUARD_MS after
// listening starts is ignored, so a double tap to start doesn't cancel itself.
//
// Pure logic, unit-tested on the host. Callers serialise access.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VOICE_START_GUARD_MS 600
#define VOICE_STOP_GUARD_MS 700

typedef enum { VS_IDLE, VS_LISTENING, VS_BUSY } voice_state_t;

typedef enum {
    VA_NONE,
    VA_START_LISTENING,
    VA_FINISH_LISTENING,
    VA_CANCEL_LISTENING,
    VA_INTERRUPT,
    VA_NEW_CONVERSATION,
} voice_action_t;

typedef struct {
    voice_state_t state;
    int64_t listen_started_ms;
    int64_t last_stop_ms;  // when listening finished or a reply was stopped
    bool has_stopped;      // last_stop_ms is valid
} voice_ctl_t;

void voice_ctl_init(voice_ctl_t *v);

// A BOOT button event. Updates the state and says what to do.
voice_action_t voice_ctl_button(voice_ctl_t *v, bool long_press, int64_t now_ms);

// The assistant starts work that didn't come from the button (a typed
// question). Only takes effect when idle.
void voice_ctl_busy(voice_ctl_t *v);

// Listening ended without a tap (time limit reached, or the mic failed).
// Only takes effect while listening.
void voice_ctl_listen_ended(voice_ctl_t *v, int64_t now_ms);

// The turn is over (reply finished, stopped or failed): busy becomes idle.
// Leaves "listening" alone, in case a new turn has already started.
void voice_ctl_idle(voice_ctl_t *v);

// Back to idle whatever the state (e.g. a turn couldn't be started).
void voice_ctl_reset(voice_ctl_t *v);

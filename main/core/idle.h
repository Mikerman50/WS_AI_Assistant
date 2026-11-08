// What the face gets up to while nobody's talking to it: blinks, glances, the
// odd passing mood, dozing off after a while, and finally sleep, when it stops
// changing altogether (so the e-ink panel and battery get a rest). Pure C with
// its own random numbers, so it's tested on a PC; main.c runs it a few times a
// second and hands each pose to the UI.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "face.h"

typedef enum { IDLE_OFF, IDLE_CALM, IDLE_LIVELY } idle_style_t;

typedef struct {
    bool blink;
    float look;  // 0..1, 0.5 = straight ahead
    face_expr_t expr;
    char prop[8];
} idle_pose_t;

typedef struct {
    idle_style_t style;
    uint32_t doze_ms;  // nothing happening this long: drowsy (0 = never)
    uint32_t rng;
    uint32_t since;    // last time something happened
    uint32_t blink_at, blink_until;
    uint32_t glance_at, glance_until;
    float glance;
    uint32_t mood_at, mood_until;
    face_expr_t mood;
    const char *mood_prop;
} idle_t;

// "off", "calm" or "lively" (anything else: calm).
idle_style_t idle_style_from_name(const char *name);

void idle_init(idle_t *s, idle_style_t style, uint32_t doze_minutes, uint32_t seed, uint32_t now_ms);

// Something happened (a button, a reply): wide awake again, from now.
void idle_wake(idle_t *s, uint32_t now_ms);

// The pose for this moment.
void idle_pose(idle_t *s, uint32_t now_ms, idle_pose_t *out);

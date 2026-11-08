#include "idle.h"

#include <stdio.h>
#include <string.h>

#define BLINK_MS 600      // long enough for the panel to show it
#define ASLEEP_AFTER 3    // dozing this many times longer than doze_ms: asleep

static uint32_t next_rand(idle_t *s)
{
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 17;
    s->rng ^= s->rng << 5;
    return s->rng;
}

static uint32_t between(idle_t *s, uint32_t lo_ms, uint32_t hi_ms)
{
    return lo_ms + next_rand(s) % (hi_ms - lo_ms + 1);
}

idle_style_t idle_style_from_name(const char *name)
{
    if (strcmp(name, "off") == 0) return IDLE_OFF;
    if (strcmp(name, "lively") == 0) return IDLE_LIVELY;
    return IDLE_CALM;
}

static void schedule(idle_t *s, uint32_t now)
{
    bool lively = s->style == IDLE_LIVELY;
    s->blink_at = now + (lively ? between(s, 4000, 9000) : between(s, 7000, 15000));
    s->glance_at = now + (lively ? between(s, 12000, 30000) : between(s, 25000, 60000));
    s->mood_at = now + (lively ? between(s, 30000, 90000) : between(s, 90000, 240000));
    s->blink_until = s->glance_until = s->mood_until = 0;
}

void idle_init(idle_t *s, idle_style_t style, uint32_t doze_minutes, uint32_t seed, uint32_t now_ms)
{
    memset(s, 0, sizeof *s);
    s->style = style;
    s->doze_ms = doze_minutes * 60000u;
    s->rng = seed ? seed : 0x9E3779B9u;
    idle_wake(s, now_ms);
}

void idle_wake(idle_t *s, uint32_t now_ms)
{
    s->since = now_ms;
    schedule(s, now_ms);
}

static bool due(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

void idle_pose(idle_t *s, uint32_t now, idle_pose_t *out)
{
    memset(out, 0, sizeof *out);
    out->look = 0.5f;
    out->expr = EXPR_NEUTRAL;
    if (s->style == IDLE_OFF) return;

    uint32_t quiet = now - s->since;
    if (s->doze_ms && quiet >= s->doze_ms * ASLEEP_AFTER) {  // asleep: one still picture
        out->blink = true;
        out->expr = EXPR_SLEEPY;
        snprintf(out->prop, sizeof out->prop, "z z");
        return;
    }
    if (s->doze_ms && quiet >= s->doze_ms) {  // drowsy: heavy eyes, the odd "z", slow blinks
        out->expr = EXPR_SLEEPY;
        uint32_t phase = (quiet / 6000) % 4;
        snprintf(out->prop, sizeof out->prop, "%s", phase == 1 ? "z" : (phase == 2 ? "z z" : ""));
        out->blink = (quiet / 1000) % 20 >= 17;  // eyes drift shut for a few seconds now and then
        return;
    }

    // Awake. Each kind of moment has its own timer.
    if (due(now, s->mood_at)) {
        static const struct {
            face_expr_t expr;
            const char *prop;
        } k_moods[] = {
            {EXPR_HAPPY, ""}, {EXPR_CURIOUS, "?"}, {EXPR_AMUSED, ""}, {EXPR_THINKING, "..."},
            {EXPR_SURPRISED, "!"}, {EXPR_HAPPY, "~"},
        };
        size_t n = s->style == IDLE_LIVELY ? sizeof k_moods / sizeof k_moods[0] : 2;  // calm: happy or curious
        size_t i = next_rand(s) % n;
        s->mood = k_moods[i].expr;
        s->mood_prop = k_moods[i].prop;
        s->mood_until = now + between(s, 4000, 8000);
        s->mood_at = s->mood_until + (s->style == IDLE_LIVELY ? between(s, 30000, 90000) : between(s, 90000, 240000));
    }
    if (due(now, s->glance_at)) {
        float sides[] = {0.1f, 0.9f, 0.25f, 0.75f};
        s->glance = sides[next_rand(s) % 4];
        s->glance_until = now + between(s, 1500, 4000);
        s->glance_at = s->glance_until + (s->style == IDLE_LIVELY ? between(s, 12000, 30000) : between(s, 25000, 60000));
    }
    if (due(now, s->blink_at)) {
        s->blink_until = now + BLINK_MS;
        s->blink_at = s->blink_until + (s->style == IDLE_LIVELY ? between(s, 4000, 9000) : between(s, 7000, 15000));
    }

    if (!due(now, s->mood_until)) {
        out->expr = s->mood;
        snprintf(out->prop, sizeof out->prop, "%s", s->mood_prop);
    }
    if (!due(now, s->glance_until)) out->look = s->glance;
    if (!due(now, s->blink_until)) out->blink = true;
}

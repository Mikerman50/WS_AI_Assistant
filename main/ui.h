// Owns the screen. Other tasks change the face state; a UI task redraws it with
// fast partial refreshes (about three a second at most) and a full refresh now
// and then to clear ghosting.
#pragma once

#include <stdbool.h>

#include "captions.h"
#include "face.h"

// Display settings come from config.json (or the defaults if it couldn't be read).
void ui_init(int rotation, bool busy_active_high);

// Shows a message with an expression and an instruction in the bottom band,
// e.g. a settings problem. The message should fit three subtitle lines.
void ui_show_notice(face_expr_t expr, const char *message, const char *footer);

void ui_set_mode(face_mode_t mode);
void ui_set_expr(face_expr_t expr);
void ui_set_heard(const char *text);  // NULL or "" clears the "you: ..." strip
void ui_set_hands_free(bool on);      // listening face says "Listening..." rather than "Tap BOOT when done"
void ui_set_caption(const cap_page_t *page);
void ui_clear_caption(void);
void ui_set_speaking(float mouth, float look);  // mouth 0..1, look 0..1
void ui_set_level(float level);                 // mic level while listening
void ui_set_pinned(const char *text);           // bottom band while idle
void ui_full_refresh(void);

face_mode_t ui_get_mode(void);

// While idle only (ignored otherwise): eyes shut, where they look (0..1), the
// expression, and a small mark beside the head ("" for none).
void ui_set_idle_pose(bool blink, float look, face_expr_t expr, const char *prop);

// Waits until everything requested so far is on the panel.
bool ui_wait_idle(int timeout_ms);

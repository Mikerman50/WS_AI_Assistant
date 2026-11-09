// The conversation loop:
//   tap BOOT -> listening face -> tap again -> transcribe -> thinking face -> Claude
//   -> spoken reply with subtitles
// or, hands-free: wake word -> chime -> listening face -> a pause -> transcribe...
// Typed questions from the USB console skip straight to Claude.
#pragma once

#include <stdbool.h>

#include "config_parse.h"

// `voice_ok` is false if the microphone couldn't be set up.
void assistant_init(const app_config_t *cfg, bool voice_ok);

// A BOOT button event (see core/voice_ctl.h for what each does).
void assistant_button(bool long_press);

// The wake word was heard: listen hands-free (after a chime, if wanted) and
// stop at a pause. Ignored unless idle.
void assistant_wake(void);

// Queues a typed question (copied).
void assistant_ask(const char *question);

// Speaks `text` (copied) as if it were a reply, without asking Claude: a quick
// way to try the voice.
void assistant_say(const char *text);

bool assistant_busy(void);

// Speaks Claude's reply with the on-device voice (SVOX Pico, British English)
// while subtitles over the face follow the audio and the mouth moves with it.
//
// Two tasks form a pipeline: one synthesises the next sentence while the other
// plays the current one, so there are no gaps once speech has started. The
// voice engine is started for each reply and stopped after it, so its memory is
// free while the board is listening. If the voice is switched off
// (speech.enabled = false) or can't start, subtitles are paced at
// speech.pace_chars_per_sec instead, with no sound.
//
// A reply is: speaker_begin_reply(), any number of speaker_say() /
// speaker_error(), then speaker_end_reply(). Call those from one task.
#pragma once

#include <stdbool.h>

#include "config_parse.h"

// `audio_ok` is false if the audio codec couldn't be set up. `voice_ta` and
// `voice_sg` are the two voice files from the SD card, in memory (they must stay
// valid), or NULL. Without all three, replies are subtitles only.
void speaker_init(const app_config_t *cfg, bool audio_ok, const void *voice_ta, const void *voice_sg);

// True if replies will be spoken (false: subtitles only).
bool speaker_has_voice(void);

void speaker_begin_reply(void);

// Queues a sentence (copied). `cue` is a face cue name or "".
void speaker_say(const char *sentence, const char *cue);

// Says a short error message with the sorry face.
void speaker_error(const char *message);

// Marks the end of a reply; the face goes idle once everything has played.
void speaker_end_reply(void);

// Stops the current reply now: speech is cut off and the rest of the reply is
// skipped. Safe to call from any task.
void speaker_stop(void);

// True while anything is queued, being synthesised or playing.
bool speaker_busy(void);

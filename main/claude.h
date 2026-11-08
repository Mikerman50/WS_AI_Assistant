// Streaming client for Claude's Messages API.
#pragma once

#include <stddef.h>

#include "config_parse.h"
#include "esp_err.h"

// Called for each finished sentence while the reply streams in. `cue` is the
// face cue that came before the sentence, or "".
typedef void (*claude_sentence_cb_t)(const char *sentence, const char *cue, void *ctx);

// Takes the API key, model and household context from the loaded settings.
void claude_init(const app_config_t *cfg);

// Sends `question` with the recent conversation and streams the reply,
// calling `on_sentence` as sentences complete. Blocks until the reply is done.
// On failure returns an error and writes a short, speakable reason to `err`.
esp_err_t claude_ask(const char *question, claude_sentence_cb_t on_sentence, void *ctx,
                     char *err, size_t errsz);

// Starts a fresh conversation (takes effect before the next question).
void claude_forget(void);

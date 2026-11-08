#include "config_parse.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

void config_set_defaults(app_config_t *c)
{
    memset(c, 0, sizeof *c);
    snprintf(c->claude_model, sizeof c->claude_model, "%s", "claude-haiku-5-5");
    c->claude_max_tokens = 300;
    c->claude_web_search = false;
    c->listen_max_seconds = 20;
    c->listen_mic_gain_db = 30;
    c->listen_language_model = true;
    c->display_rotation = 0;
    c->display_busy_active_high = true;
    c->speech_enabled = true;
    c->speech_volume = 70;
    c->speech_speed = 100;
    c->speech_pitch = 100;
    c->speech_pace_cps = 15;
}

typedef struct {
    char *err;
    size_t errsz;
    bool failed;
} ctx_t;

__attribute__((format(printf, 2, 3))) static void fail(ctx_t *x, const char *fmt, ...)
{
    if (x->failed) return;  // keep the first problem; it is usually the real one
    x->failed = true;
    if (x->errsz == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(x->err, x->errsz, fmt, ap);
    va_end(ap);
}

static const cJSON *section(ctx_t *x, const cJSON *root, const char *name)
{
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, name);
    if (s && !cJSON_IsObject(s)) {
        fail(x, "\"%s\" must be a { } section in config.json.", name);
        return NULL;
    }
    return s;
}

static const cJSON *item(const cJSON *sec, const char *key)
{
    const cJSON *v = sec ? cJSON_GetObjectItemCaseSensitive(sec, key) : NULL;
    return (v && !cJSON_IsNull(v)) ? v : NULL;
}

static void get_str(ctx_t *x, const cJSON *sec, const char *secname, const char *key, char *out,
                    size_t outsz, bool required)
{
    const cJSON *v = item(sec, key);
    if (!v) {
        if (required) fail(x, "%s.%s is missing from config.json.", secname, key);
        return;
    }
    if (!cJSON_IsString(v)) {
        fail(x, "%s.%s must be text in quotes.", secname, key);
        return;
    }
    size_t n = strlen(v->valuestring);
    if (n >= outsz) {
        fail(x, "%s.%s is too long.", secname, key);
        return;
    }
    if (required && n == 0) {
        fail(x, "%s.%s is empty in config.json.", secname, key);
        return;
    }
    memcpy(out, v->valuestring, n + 1);
}

static void get_int(ctx_t *x, const cJSON *sec, const char *secname, const char *key, int *out, int lo, int hi)
{
    const cJSON *v = item(sec, key);
    if (!v) return;
    if (!cJSON_IsNumber(v)) {
        fail(x, "%s.%s must be a number.", secname, key);
        return;
    }
    double d = v->valuedouble;
    if (d < lo || d > hi || d != (double)(int)d) {
        fail(x, "%s.%s must be %d to %d.", secname, key, lo, hi);
        return;
    }
    *out = (int)d;
}

static void get_bool(ctx_t *x, const cJSON *sec, const char *secname, const char *key, bool *out)
{
    const cJSON *v = item(sec, key);
    if (!v) return;
    if (!cJSON_IsBool(v)) {
        fail(x, "%s.%s must be true or false.", secname, key);
        return;
    }
    *out = cJSON_IsTrue(v);
}

static int line_of(const char *text, const char *pos)
{
    int line = 1;
    for (const char *p = text; p < pos && *p; p++)
        if (*p == '\n') line++;
    return line;
}

bool config_parse(const char *json, app_config_t *c, char *err, size_t errsz)
{
    ctx_t x = {.err = err, .errsz = errsz, .failed = false};
    config_set_defaults(c);
    if (errsz) err[0] = '\0';

    if (!json || json[strspn(json, " \t\r\n")] == '\0') {
        fail(&x, "config.json is empty.");
        return false;
    }
    if ((unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB && (unsigned char)json[2] == 0xBF)
        json += 3;  // byte-order mark added by some Windows editors

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        const char *at = cJSON_GetErrorPtr();
        int line = (at && at >= json) ? line_of(json, at) : 1;
        // It may just contain comments: strip them and try again.
        size_t n = strlen(json);
        char *copy = malloc(n + 1);
        if (copy) {
            memcpy(copy, json, n + 1);
            cJSON_Minify(copy);
            root = cJSON_Parse(copy);
            free(copy);
        }
        if (!root) {
            fail(&x, "config.json has a mistake on line %d.", line);
            return false;
        }
    }
    if (!cJSON_IsObject(root)) {
        fail(&x, "config.json must start with { and end with }.");
        cJSON_Delete(root);
        return false;
    }

    const cJSON *wifi = section(&x, root, "wifi");
    get_str(&x, wifi, "wifi", "ssid", c->wifi_ssid, sizeof c->wifi_ssid, true);
    get_str(&x, wifi, "wifi", "password", c->wifi_password, sizeof c->wifi_password, false);

    const cJSON *claude = section(&x, root, "claude");
    get_str(&x, claude, "claude", "api_key", c->claude_api_key, sizeof c->claude_api_key, true);
    get_str(&x, claude, "claude", "model", c->claude_model, sizeof c->claude_model, false);
    get_int(&x, claude, "claude", "max_tokens", &c->claude_max_tokens, 16, 8192);
    get_bool(&x, claude, "claude", "web_search", &c->claude_web_search);
    get_str(&x, claude, "claude", "context", c->claude_context, sizeof c->claude_context, false);

    // An "elevenlabs" section from older versions is ignored, like any unknown one:
    // listening happens on the board now.
    const cJSON *listen = section(&x, root, "listen");
    get_int(&x, listen, "listen", "max_seconds", &c->listen_max_seconds, 5, 30);
    get_int(&x, listen, "listen", "mic_gain_db", &c->listen_mic_gain_db, 0, 42);
    get_bool(&x, listen, "listen", "language_model", &c->listen_language_model);

    const cJSON *display = section(&x, root, "display");
    get_int(&x, display, "display", "rotation", &c->display_rotation, 0, 3);
    get_bool(&x, display, "display", "busy_active_high", &c->display_busy_active_high);

    const cJSON *speech = section(&x, root, "speech");
    get_bool(&x, speech, "speech", "enabled", &c->speech_enabled);
    get_int(&x, speech, "speech", "volume", &c->speech_volume, 0, 100);
    get_int(&x, speech, "speech", "speed", &c->speech_speed, 50, 200);
    get_int(&x, speech, "speech", "pitch", &c->speech_pitch, 50, 200);
    get_int(&x, speech, "speech", "pace_chars_per_sec", &c->speech_pace_cps, 5, 40);

    cJSON_Delete(root);

    if (!x.failed && (strcmp(c->wifi_ssid, "YOUR_WIFI_NAME") == 0 || strncmp(c->claude_api_key, "YOUR_", 5) == 0))
        fail(&x, "Replace the example values in config.json.");
    if (!x.failed && c->wifi_password[0] && strlen(c->wifi_password) < 8)
        fail(&x, "wifi.password must be at least 8 characters.");
    if (!x.failed && c->claude_model[0] == '\0')
        snprintf(c->claude_model, sizeof c->claude_model, "%s", "claude-haiku-5-5");
    return !x.failed;
}

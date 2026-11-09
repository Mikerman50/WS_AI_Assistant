// Host unit tests for the portable core: cc builds them, no ESP-IDF needed.
//   make -C test/host test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "asr_text.h"
#include "memory_text.h"
#include "idle.h"
#include "audio_level.h"
#include "captions.h"
#include "config_parse.h"
#include "face.h"
#include "gfx.h"
#include "sse.h"
#include "text_stream.h"
#include "tts_text.h"
#include "utf8.h"
#include "voice_ctl.h"
#include "wake_words.h"
#include "endpoint.h"
#include "power_src.h"

static int g_fail, g_pass;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) g_pass++;                                                  \
        else {                                                               \
            g_fail++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_STR(a, b)                                                                     \
    do {                                                                                    \
        const char *_a = (a), *_b = (b);                                                    \
        if (strcmp(_a, _b) == 0) g_pass++;                                                  \
        else {                                                                              \
            g_fail++;                                                                       \
            fprintf(stderr, "FAIL %s:%d:\n  got      \"%s\"\n  expected \"%s\"\n", __FILE__, \
                    __LINE__, _a, _b);                                                      \
        }                                                                                   \
    } while (0)

// ---------------------------------------------------------------- SSE

typedef struct {
    int n;
    char event[8][48];
    char data[8][512];
} sse_log_t;

static void on_sse(const char *event, const char *data, void *ctx)
{
    sse_log_t *log = ctx;
    if (log->n >= 8) return;
    snprintf(log->event[log->n], sizeof log->event[0], "%s", event);
    snprintf(log->data[log->n], sizeof log->data[0], "%s", data);
    log->n++;
}

static void feed_chunked(sse_t *s, const char *text, size_t chunk)
{
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i += chunk) sse_feed(s, text + i, (len - i) < chunk ? (len - i) : chunk);
}

static void test_sse(void)
{
    const char *stream =
        "event: message_start\r\n"
        "data: {\"type\":\"message_start\"}\r\n"
        "\r\n"
        ": keep-alive comment\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Hi\"}}\n"
        "\n"
        "event: ping\n"
        "data: {\"type\": \"ping\"}\n"
        "\n"
        "data: line one\n"
        "data: line two\n"
        "\n";
    for (size_t chunk = 1; chunk <= 7; chunk += 3) {
        sse_log_t log = {0};
        sse_t s;
        CHECK(sse_init(&s, 4096, on_sse, &log));
        feed_chunked(&s, stream, chunk);
        sse_free(&s);
        CHECK(log.n == 4);
        CHECK_STR(log.event[0], "message_start");
        CHECK_STR(log.data[0], "{\"type\":\"message_start\"}");
        CHECK_STR(log.event[1], "content_block_delta");
        CHECK(strstr(log.data[1], "\"text\":\"Hi\"") != NULL);
        CHECK_STR(log.event[2], "ping");
        CHECK_STR(log.event[3], "message");
        CHECK_STR(log.data[3], "line one\nline two");
    }

    // An event bigger than the limit is dropped, later ones still arrive.
    sse_log_t log = {0};
    sse_t s;
    CHECK(sse_init(&s, 64, on_sse, &log));
    sse_feed(&s, "event: big\ndata: ", 17);
    for (int i = 0; i < 200; i++) sse_feed(&s, "x", 1);
    const char *rest = "\n\nevent: small\ndata: ok\n\n";
    sse_feed(&s, rest, strlen(rest));
    sse_free(&s);
    CHECK(log.n == 1);
    CHECK_STR(log.event[0], "small");
    CHECK_STR(log.data[0], "ok");
}

// ---------------------------------------------------------------- text stream

typedef struct {
    int n;
    char s[32][256];
    char cue[32][24];
    int notes;
    char note[4][TS_NOTE_MAX];
} ts_log_t;

static void on_note(const char *note, void *ctx)
{
    ts_log_t *log = ctx;
    if (log->notes < 4) snprintf(log->note[log->notes], sizeof log->note[0], "%s", note);
    log->notes++;
}

static void on_sentence(const char *sentence, const char *cue, void *ctx)
{
    ts_log_t *log = ctx;
    if (log->n >= 32) return;
    snprintf(log->s[log->n], sizeof log->s[0], "%s", sentence);
    snprintf(log->cue[log->n], sizeof log->cue[0], "%s", cue);
    log->n++;
}

static ts_log_t run_ts(const char *text, size_t chunk)
{
    ts_log_t log;
    memset(&log, 0, sizeof log);
    text_stream_t t;
    ts_init(&t, on_sentence, &log);
    t.on_note = on_note;
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i += chunk) ts_feed(&t, text + i, (len - i) < chunk ? (len - i) : chunk);
    ts_finish(&t);
    return log;
}

static void test_text_stream(void)
{
    for (size_t chunk = 1; chunk <= 9; chunk += 4) {
        // Memory notes in braces are taken out of what's spoken.
        ts_log_t m = run_ts("[happy] Nice to meet you, Mike! {remember: Mike takes his tea black, no sugar.} "
                            "Shall I put the kettle on?{ forget:old address }",
                            chunk);
        CHECK(m.n == 2);
        CHECK_STR(m.s[0], "Nice to meet you, Mike!");
        CHECK_STR(m.cue[0], "happy");
        CHECK_STR(m.s[1], "Shall I put the kettle on?");
        CHECK(m.notes == 2);
        CHECK_STR(m.note[0], "remember: Mike takes his tea black, no sugar.");
        CHECK_STR(m.note[1], "forget:old address");
        ts_log_t u = run_ts("All done. {remember: cut off half", chunk);  // a reply cut short mid-note
        CHECK(u.n == 1 && u.notes == 1);
        CHECK_STR(u.s[0], "All done.");
        CHECK_STR(u.note[0], "remember: cut off half");

        ts_log_t a = run_ts("[amused] Ha, that's a good one! Penguins can't fly, but they swim brilliantly.", chunk);
        CHECK(a.n == 2);
        CHECK_STR(a.s[0], "Ha, that's a good one!");
        CHECK_STR(a.cue[0], "amused");
        CHECK_STR(a.s[1], "Penguins can't fly, but they swim brilliantly.");
        CHECK_STR(a.cue[1], "");

        ts_log_t b = run_ts("Ask Dr. Smith about it. He knows the 3.5 version well.", chunk);
        CHECK(b.n == 2);
        CHECK_STR(b.s[0], "Ask Dr. Smith about it.");
        CHECK_STR(b.s[1], "He knows the 3.5 version well.");

        ts_log_t c = run_ts("**Sure** thing \xF0\x9F\x8E\x89! Here is what I found.", chunk);
        CHECK(c.n == 1);
        CHECK_STR(c.s[0], "Sure thing! Here is what I found.");  // first part too short to stand alone

        ts_log_t d = run_ts("Well, [surprised] I didn't expect that. [happy]Great news though!", chunk);
        CHECK(d.n == 2);
        CHECK_STR(d.s[0], "Well, I didn't expect that.");
        CHECK_STR(d.cue[0], "surprised");
        CHECK_STR(d.s[1], "Great news though!");
        CHECK_STR(d.cue[1], "happy");

        ts_log_t e = run_ts("Use the [1] option, it is better.", chunk);
        CHECK(e.n == 1);
        CHECK_STR(e.s[0], "Use the [1] option, it is better.");

        ts_log_t f = run_ts("Caf\xC3\xA9 au lait costs \xE2\x82\xAC" "3 \xE2\x80\x94 not bad. Shall I find one?", chunk);
        CHECK(f.n == 2);
        CHECK_STR(f.s[0], "Caf\xC3\xA9 au lait costs \xE2\x82\xAC" "3 \xE2\x80\x94 not bad.");
        CHECK_STR(f.s[1], "Shall I find one?");

        ts_log_t g = run_ts("First line without a full stop\nSecond line.", chunk);
        CHECK(g.n == 2);
        CHECK_STR(g.s[0], "First line without a full stop");
    }

    // Long runs are cut at a comma so speech can start before Claude finishes.
    char longtext[600] = "";
    for (int i = 0; i < 12; i++) strcat(longtext, "this clause keeps going and going, ");
    strcat(longtext, "and finally ends.");
    ts_log_t h = run_ts(longtext, 5);
    CHECK(h.n >= 3);
    for (int i = 0; i < h.n; i++) CHECK(strlen(h.s[i]) <= 181);
    CHECK(h.s[0][strlen(h.s[0]) - 1] == ',');
}

// ---------------------------------------------------------------- captions

static int measure_caption(const char *s, int nbytes, void *ctx)
{
    (void)ctx;
    return gfx_text_width_n(&font_caption, s, nbytes);
}

static void test_captions(void)
{
    const char *text = "Probably, yes. There's rain from about two in the afternoon, so take a coat and maybe an umbrella too.";
    cap_layout_t L;
    int pages = captions_layout(&L, text, FACE_CAPTION_MAX_W, 3, measure_caption, NULL);
    CHECK(pages >= 1);
    CHECK(L.ncp == utf8_count(text));
    int words_seen = 0;
    for (int p = 0; p < L.npages; p++) {
        CHECK(L.page[p].nlines >= 1 && L.page[p].nlines <= 3);
        for (int i = 0; i < L.page[p].nlines; i++) {
            CHECK(gfx_text_width(&font_caption, L.page[p].line[i]) <= FACE_CAPTION_MAX_W);
            if (strstr(L.page[p].line[i], "umbrella")) words_seen++;
        }
        if (p > 0) CHECK(L.page[p].first_cp == L.page[p - 1].last_cp + 2);  // one space between pages
    }
    CHECK(words_seen == 1);
    CHECK(L.page[L.npages - 1].last_cp == L.ncp - 1);

    uint32_t starts[256];
    uint32_t total = captions_estimate(text, 15.0f, starts, 256);
    CHECK(total > 6000 && total < 10000);
    captions_apply_timing(&L, starts, NULL, L.ncp, total);
    CHECK(L.page[0].start_ms == 0);
    CHECK(captions_page_at(&L, 0) == 0);
    CHECK(captions_page_at(&L, total) == L.npages - 1);
    for (int p = 1; p < L.npages; p++) {
        CHECK(L.page[p].start_ms > L.page[p - 1].start_ms);
        CHECK(captions_page_at(&L, L.page[p].start_ms) == p);
        CHECK(captions_page_at(&L, L.page[p].start_ms - 1) == p - 1);
    }
    CHECK(captions_progress(&L.page[0], 0) == 0.0f);
    CHECK(captions_progress(&L.page[0], L.page[0].end_ms) == 1.0f);

    // A word wider than a line is split instead of overflowing.
    cap_layout_t W;
    captions_layout(&W, "Supercalifragilisticexpialidocious-and-then-some", FACE_CAPTION_MAX_W, 3, measure_caption, NULL);
    CHECK(W.npages == 1 && W.page[0].nlines >= 2);
    for (int i = 0; i < W.page[0].nlines; i++)
        CHECK(gfx_text_width(&font_caption, W.page[0].line[i]) <= FACE_CAPTION_MAX_W);

    cap_layout_t E;
    CHECK(captions_layout(&E, "   ", FACE_CAPTION_MAX_W, 3, measure_caption, NULL) == 0);

    // Estimated timings stretched to the real length of the synthesised audio.
    uint32_t s4[] = {0, 100, 200, 300};
    captions_scale(s4, 4, 400, 800);
    CHECK(s4[0] == 0 && s4[1] == 200 && s4[2] == 400 && s4[3] == 600);
    captions_scale(s4, 4, 0, 1000);  // nothing to scale from: left alone
    CHECK(s4[3] == 600);
    uint32_t big[] = {3000000000u};
    captions_scale(big, 1, 3000000000u, 1000);  // no overflow in the multiply
    CHECK(big[0] == 1000);
    total = captions_estimate(text, 15.0f, starts, 256);
    uint32_t actual = total * 2 / 3;  // the voice was quicker than the estimate
    captions_scale(starts, L.ncp, total, actual);
    captions_apply_timing(&L, starts, NULL, L.ncp, actual);
    CHECK(L.page[L.npages - 1].end_ms == actual);
    CHECK(captions_page_at(&L, actual) == L.npages - 1);
    for (int p = 1; p < L.npages; p++) CHECK(L.page[p].start_ms < actual && L.page[p].start_ms > L.page[p - 1].start_ms);
}

// ---------------------------------------------------------------- graphics

static void test_gfx(void)
{
    gfx_t g;
    gfx_clear(&g);
    gfx_fill_rect(&g, -5, -5, 10, 10, true);
    CHECK(gfx_get(&g, 0, 0) && gfx_get(&g, 4, 4) && !gfx_get(&g, 5, 5));
    gfx_fill_rect(&g, 10, 10, 0, 5, true);
    CHECK(!gfx_get(&g, 10, 10));

    char out[64];
    gfx_fit_text(&font_small, "short", 180, out, sizeof out);
    CHECK_STR(out, "short");
    gfx_fit_text(&font_small, "you: this question is far too long to fit across the status strip", 180, out, sizeof out);
    CHECK(gfx_text_width(&font_small, out) <= 180);
    CHECK(strstr(out, "\xE2\x80\xA6") != NULL);

    CHECK(face_expr_from_name("Amused") == EXPR_AMUSED);
    CHECK(face_expr_from_name("sad") == EXPR_SORRY);
    CHECK(face_expr_from_name("banana") == EXPR_COUNT);

    // Every mode renders without touching memory outside the canvas (ASan checks this).
    face_state_t s;
    face_state_init(&s);
    for (int m = MODE_IDLE; m <= MODE_NOTICE; m++) {
        for (int e = 0; e < EXPR_COUNT; e++) {
            s.mode = (face_mode_t)m;
            s.expr = (face_expr_t)e;
            strcpy(s.heard, "what time is it?");
            strcpy(s.pinned, "Bins: green, Thursday");
            strcpy(s.caption[0], "Hello there");
            s.caption_lines = 1;
            s.mouth = 1.0f;
            s.look = 1.0f;
            s.level = 1.0f;
            face_render(&g, &s);
        }
    }
}

// ---------------------------------------------------------------- config.json

static char *read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = calloc(1, 65536);
    size_t n = fread(buf, 1, 65535, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static bool parse(const char *json, app_config_t *c, char *err)
{
    return config_parse(json, c, err, 160);
}

static void test_config(void)
{
    app_config_t c;
    char err[160];

    // The sample shipped in sdcard/ parses, but asks for real values.
    char *sample = read_text_file("../../sdcard/config.json");
    CHECK(sample != NULL);
    if (sample) {
        CHECK(!parse(sample, &c, err));
        CHECK_STR(err, "Replace the example values in config.json.");
        CHECK(c.claude_max_tokens == 300 && c.display_busy_active_high && c.speech_pace_cps == 15);
        CHECK(c.speech_enabled && c.speech_volume == 70 && c.speech_speed == 100 && c.speech_pitch == 100);
        free(sample);
    }

    // Minimal file: everything else falls back to defaults.
    CHECK(parse("{\"wifi\":{\"ssid\":\"Home\",\"password\":\"correcthorse\"},"
                "\"claude\":{\"api_key\":\"sk-ant-abc\"}}", &c, err));
    CHECK_STR(c.wifi_ssid, "Home");
    CHECK_STR(c.wifi_password, "correcthorse");
    CHECK_STR(c.claude_api_key, "sk-ant-abc");
    CHECK_STR(c.claude_model, "claude-haiku-5-5");
    CHECK(c.claude_max_tokens == 300 && !c.claude_web_search);
    CHECK(c.display_rotation == 0 && c.display_busy_active_high && c.speech_pace_cps == 15);
    CHECK_STR(c.claude_context, "");
    CHECK(c.listen_max_seconds == 20 && c.listen_mic_gain_db == 30 && c.listen_language_model);
    CHECK_STR(c.claude_personality, "friendly");
    CHECK_STR(c.wake_word, "hey jarvis");
    CHECK_STR(c.wake_listen, "usb");
    CHECK_STR(c.wake_sensitivity, "normal");
    CHECK(c.wake_chime);
    CHECK(parse("{\"wifi\":{\"ssid\":\"Home\"},\"claude\":{\"api_key\":\"k\"},\"wake_word\":{\"word\":\"Okay Nabu\","
                "\"listen\":\"always\",\"sensitivity\":\"high\",\"chime\":false}}", &c, err));
    CHECK_STR(c.wake_word, "Okay Nabu");
    CHECK_STR(c.wake_listen, "always");
    CHECK(!c.wake_chime && wake_sens_from_name(c.wake_sensitivity) == WAKE_SENS_HIGH);
    CHECK(parse("{\"wifi\":{\"ssid\":\"Home\"},\"claude\":{\"api_key\":\"k\"},\"wake_word\":{\"word\":\"off\"}}", &c,
                err));
    CHECK(c.speech_enabled && c.speech_volume == 70 && c.speech_speed == 100 && c.speech_pitch == 100);

    // Every field set. (The elevenlabs section is left over from an older version
    // and is ignored, like any unknown setting.)
    const char *full =
        "{\n"
        "  \"wifi\": {\"ssid\": \"Caf\xC3\xA9 net\", \"password\": \"\"},\n"
        "  \"claude\": {\"api_key\": \"sk-ant-xyz\", \"model\": \"claude-sonnet-5-5\", \"max_tokens\": 500,\n"
        "             \"web_search\": true, \"context\": \"Our cat is called Biscuit.\", \"personality\": \"butler\"},\n"
        "  \"elevenlabs\": {\"api_key\": \"el-key\", \"voice_id\": \"v123\", \"stt_model\": \"scribe_v1\",\n"
        "                 \"language\": \"en\"},\n"
        "  \"listen\": {\"max_seconds\": 25, \"mic_gain_db\": 36, \"language_model\": false},\n"
        "  \"display\": {\"rotation\": 2, \"busy_active_high\": false},\n"
        "  \"speech\": {\"enabled\": false, \"volume\": 40, \"speed\": 120, \"pitch\": 90,\n"
        "             \"pace_chars_per_sec\": 18},\n"
        "  \"something_new\": {\"ignored\": 1}\n"
        "}\n";
    CHECK(parse(full, &c, err));
    CHECK_STR(c.wifi_ssid, "Caf\xC3\xA9 net");
    CHECK_STR(c.wifi_password, "");
    CHECK_STR(c.claude_model, "claude-sonnet-5-5");
    CHECK(c.claude_max_tokens == 500 && c.claude_web_search);
    CHECK_STR(c.claude_context, "Our cat is called Biscuit.");
    CHECK_STR(c.claude_personality, "butler");
    CHECK(c.listen_max_seconds == 25 && c.listen_mic_gain_db == 36 && !c.listen_language_model);
    CHECK(c.display_rotation == 2 && !c.display_busy_active_high && c.speech_pace_cps == 18);
    CHECK(!c.speech_enabled && c.speech_volume == 40 && c.speech_speed == 120 && c.speech_pitch == 90);

    // Comments and a Windows byte-order mark are tolerated.
    CHECK(parse("\xEF\xBB\xBF{ // my settings\n \"wifi\": {\"ssid\": \"Home\"}, /* key */\n"
                " \"claude\": {\"api_key\": \"sk-ant-1\"} }", &c, err));
    CHECK_STR(c.wifi_ssid, "Home");

    // Problems produce short messages that fit on the screen.
    struct {
        const char *json, *msg;
    } bad[] = {
        {"", "config.json is empty."},
        {"[1, 2]", "config.json must start with { and end with }."},
        {"{\n\"wifi\": {\"ssid\": \"Home\"},\n\"claude\": {\"api_key\": \"k\",}\n}", "config.json has a mistake on line 3."},
        {"{\"wifi\": {\"ssid\": \"Home\"}}", "claude.api_key is missing from config.json."},
        {"{\"claude\": {\"api_key\": \"k\"}}", "wifi.ssid is missing from config.json."},
        {"{\"wifi\": \"Home\", \"claude\": {\"api_key\": \"k\"}}", "\"wifi\" must be a { } section in config.json."},
        {"{\"wifi\": {\"ssid\": 42}, \"claude\": {\"api_key\": \"k\"}}", "wifi.ssid must be text in quotes."},
        {"{\"wifi\": {\"ssid\": \"\"}, \"claude\": {\"api_key\": \"k\"}}", "wifi.ssid is empty in config.json."},
        {"{\"wifi\": {\"ssid\": \"0123456789012345678901234567890123\"}, \"claude\": {\"api_key\": \"k\"}}",
         "wifi.ssid is too long."},
        {"{\"wifi\": {\"ssid\": \"Home\", \"password\": \"short\"}, \"claude\": {\"api_key\": \"k\"}}",
         "wifi.password must be at least 8 characters."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"max_tokens\": \"300\"}}",
         "claude.max_tokens must be a number."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"max_tokens\": 2.5}}",
         "claude.max_tokens must be 16 to 8192."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"display\": {\"rotation\": 5}}",
         "display.rotation must be 0 to 3."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"web_search\": \"yes\"}}",
         "claude.web_search must be true or false."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"listen\": {\"max_seconds\": 40}}",
         "listen.max_seconds must be 5 to 30."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"listen\": {\"language_model\": 1}}",
         "listen.language_model must be true or false."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"listen\": {\"mic_gain_db\": -1}}",
         "listen.mic_gain_db must be 0 to 42."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"idle\": {\"activity\": \"wild\"}}",
         "idle.activity must be off, calm or lively."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"wake_word\": {\"word\": \"hey claude\"}}",
         "wake_word.word isn't one it knows (see README)."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"wake_word\": {\"listen\": \"battery\"}}",
         "wake_word.listen must be usb or always."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"wake_word\": {\"sensitivity\": \"max\"}}",
         "wake_word.sensitivity must be low, normal or high."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"wake_word\": {\"chime\": \"yes\"}}",
         "wake_word.chime must be true or false."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"speech\": {\"enabled\": \"yes\"}}",
         "speech.enabled must be true or false."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"speech\": {\"volume\": 101}}",
         "speech.volume must be 0 to 100."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"speech\": {\"speed\": 30}}",
         "speech.speed must be 50 to 200."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"speech\": {\"pitch\": 250}}",
         "speech.pitch must be 50 to 200."},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!parse(bad[i].json, &c, err));
        CHECK_STR(err, bad[i].msg);
        CHECK(gfx_text_width(&font_caption, err) <= 3 * FACE_CAPTION_MAX_W);  // fits the notice lines
    }

    // An empty model falls back to the default; an over-long context is refused.
    CHECK(parse("{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"model\": \"\"}}", &c, err));
    CHECK_STR(c.claude_model, "claude-haiku-5-5");
    // An old-style "elevenlabs" entry, even a malformed one, doesn't matter any more.
    CHECK(parse("{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"elevenlabs\": 5}", &c, err));
    char *big = malloc(APP_CONTEXT_MAX + 200);
    strcpy(big, "{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"context\": \"");
    size_t at = strlen(big);
    memset(big + at, 'x', APP_CONTEXT_MAX + 1);
    strcpy(big + at + APP_CONTEXT_MAX + 1, "\"}}");
    CHECK(!parse(big, &c, err));
    CHECK_STR(err, "claude.context is too long.");
    free(big);
}


// ---------------------------------------------------------------- BOOT button gestures

static void test_voice_ctl(void)
{
    voice_ctl_t v;
    voice_ctl_init(&v);
    int64_t t = 10000;

    // Tap to start, tap to finish.
    CHECK(voice_ctl_button(&v, false, t) == VA_START_LISTENING);
    CHECK(v.state == VS_LISTENING);
    CHECK(voice_ctl_button(&v, false, t + 3000) == VA_FINISH_LISTENING);
    CHECK(v.state == VS_BUSY);
    // The second tap of a double tap to finish is ignored...
    CHECK(voice_ctl_button(&v, false, t + 3250) == VA_NONE);
    CHECK(v.state == VS_BUSY);
    // ...but a later tap stops the reply.
    CHECK(voice_ctl_button(&v, false, t + 6000) == VA_INTERRUPT);
    voice_ctl_idle(&v);
    // A tap straight after stopping the reply doesn't start listening.
    CHECK(voice_ctl_button(&v, false, t + 6300) == VA_NONE);
    CHECK(v.state == VS_IDLE);
    CHECK(voice_ctl_button(&v, false, t + 7000) == VA_START_LISTENING);

    // A double tap to start doesn't immediately finish.
    voice_ctl_init(&v);
    CHECK(voice_ctl_button(&v, false, t) == VA_START_LISTENING);
    CHECK(voice_ctl_button(&v, false, t + 250) == VA_NONE);
    CHECK(v.state == VS_LISTENING);
    CHECK(voice_ctl_button(&v, false, t + VOICE_START_GUARD_MS) == VA_FINISH_LISTENING);

    // Hold while listening cancels; hold when idle starts a new conversation.
    voice_ctl_init(&v);
    CHECK(voice_ctl_button(&v, true, t) == VA_NEW_CONVERSATION);
    CHECK(v.state == VS_IDLE);
    CHECK(voice_ctl_button(&v, false, t + 100) == VA_START_LISTENING);
    CHECK(voice_ctl_button(&v, true, t + 2000) == VA_CANCEL_LISTENING);
    CHECK(v.state == VS_IDLE);

    // Hitting the time limit counts as finishing; a tap that was on its way is ignored.
    voice_ctl_init(&v);
    voice_ctl_button(&v, false, t);
    voice_ctl_listen_ended(&v, t + 30000);
    CHECK(v.state == VS_BUSY);
    CHECK(voice_ctl_button(&v, false, t + 30200) == VA_NONE);
    CHECK(voice_ctl_button(&v, true, t + 30300) == VA_INTERRUPT);  // a hold always stops

    // A typed question makes it busy too, and a tap stops it.
    voice_ctl_init(&v);
    voice_ctl_busy(&v);
    CHECK(voice_ctl_button(&v, false, t) == VA_INTERRUPT);
    voice_ctl_listen_ended(&v, t);  // ignored when not listening
    CHECK(v.state == VS_BUSY);

    // The end of a turn doesn't cancel a new turn that has already started listening.
    voice_ctl_init(&v);
    voice_ctl_button(&v, false, t);
    voice_ctl_idle(&v);
    CHECK(v.state == VS_LISTENING);
    voice_ctl_busy(&v);  // nor does a typed question
    CHECK(v.state == VS_LISTENING);
    voice_ctl_reset(&v);
    CHECK(v.state == VS_IDLE);
}

// ---------------------------------------------------------------- mic level

static void test_audio_level(void)
{
    static int16_t buf[1600];
    memset(buf, 0, sizeof buf);
    CHECK(audio_rms_dbfs(buf, 1600) == -96.0f);
    CHECK(audio_meter(audio_rms_dbfs(buf, 1600)) == 0.0f);
    CHECK(audio_peak(buf, 1600) == 0);
    CHECK(audio_rms_dbfs(buf, 0) == -96.0f);

    for (int i = 0; i < 1600; i++) buf[i] = (int16_t)lround(32767.0 * sin(2 * 3.14159265358979 * 440.0 * i / 16000.0));
    float full = audio_rms_dbfs(buf, 1600);
    CHECK(full > -3.2f && full < -2.8f);  // a full-scale sine is -3 dBFS
    CHECK(audio_meter(full) == 1.0f);
    CHECK(audio_peak(buf, 1600) == 32767);

    for (int i = 0; i < 1600; i++) buf[i] = (int16_t)(buf[i] / 100);  // 40 dB quieter
    float quiet = audio_rms_dbfs(buf, 1600);
    CHECK(quiet > -43.5f && quiet < -42.5f);
    float m = audio_meter(quiet);
    CHECK(m > 0.25f && m < 0.35f);
    buf[7] = -32768;
    CHECK(audio_peak(buf, 1600) == 32768);

    // The mouth opens with loudness: shut below -45 dBFS, wide open from -15.
    CHECK(audio_mouth(-96.0f) == 0.0f);
    CHECK(audio_mouth(-45.0f) == 0.0f);
    CHECK(fabsf(audio_mouth(-30.0f) - 0.5f) < 1e-5f);
    CHECK(audio_mouth(-15.0f) == 1.0f);
    CHECK(audio_mouth(0.0f) == 1.0f);
    CHECK(audio_mouth(quiet) > 0.0f && audio_mouth(quiet) < 0.1f);  // the -43 dBFS tone: barely open

    // Speech level for the recogniser: 1 s of quiet room, 2 s of "speech" at
    // -40 dBFS, then quiet again. The gain should bring the speech to -20 dBFS
    // whatever the room noise does.
    static int16_t rec[64000];
    unsigned seed = 12345;
    for (int i = 0; i < 64000; i++) {
        seed = seed * 1103515245u + 12345u;
        int noise = (int)((seed >> 16) % 21) - 10;  // about -73 dBFS of hiss
        double tone = (i >= 16000 && i < 48000) ? 463.0 * sin(2 * 3.14159265358979 * 300.0 * i / 16000.0) : 0.0;
        rec[i] = (int16_t)lround(tone + noise);
    }
    float g = audio_speech_gain(rec, 64000);
    CHECK(g > 9.0f && g < 11.0f);  // -40 -> -20 dBFS is x10
    audio_apply_gain(rec, 64000, g);
    float after = audio_rms_dbfs(rec + 16000, 32000);
    CHECK(after > -20.5f && after < -19.5f);
    // Silence, a loud recording, a tiny one.
    memset(rec, 0, sizeof rec);
    CHECK(audio_speech_gain(rec, 64000) == 1.0f);
    CHECK(audio_speech_gain(rec, 100) == 1.0f);
    for (int i = 0; i < 64000; i++)  // very loud speech between pauses: turned down, but at most 4x
        rec[i] = (int16_t)((i >= 16000 && i < 48000) ? lround(23000.0 * sin(2 * 3.14159265358979 * 300.0 * i / 16000.0)) : 0);
    CHECK(audio_speech_gain(rec, 64000) == 0.25f);
    for (int i = 0; i < 64000; i++) rec[i] = (int16_t)((i / 8) % 2 ? 30000 : -30000);
    CHECK(audio_speech_gain(rec, 64000) == 1.0f);  // a steady din: nothing stands out, left alone
    // Gain saturates instead of wrapping.
    int16_t clip[3] = {20000, -20000, 100};
    audio_apply_gain(clip, 3, 2.0f);
    CHECK(clip[0] == 32767 && clip[1] == -32768 && clip[2] == 200);
}

static void test_asr_tidy(void)
{
    char out[64];
    CHECK(asr_tidy("  whats the weather   like tomorrow ", out, sizeof out) == strlen("Whats the weather like tomorrow"));
    CHECK_STR(out, "Whats the weather like tomorrow");
    asr_tidy("i think i'm late and i'll call if i've time i'd say", out, sizeof out);
    CHECK_STR(out, "I think I'm late and I'll call if I've time I'd say");
    asr_tidy("is it in italy", out, sizeof out);
    CHECK_STR(out, "Is it in italy");  // only the pronoun changes
    asr_tidy("", out, sizeof out);
    CHECK_STR(out, "");
    asr_tidy("   ", out, sizeof out);
    CHECK_STR(out, "");
    char small[12];
    CHECK(asr_tidy("hello there my friend", small, sizeof small) == strlen("Hello there"));
    CHECK_STR(small, "Hello there");  // whole words only
    CHECK(asr_tidy("x", out, 0) == 0);
}

static void test_memory(void)
{
    static memory_t m;
    memory_load(&m, "- Mike takes his tea black\r\n\n  - Sam is 7  \nfree text the owner wrote\n");
    CHECK_STR(m.text, "- Mike takes his tea black\n- Sam is 7\nfree text the owner wrote\n");
    CHECK(memory_remember(&m, "The cat is called Biscuit."));
    CHECK(!memory_remember(&m, "the cat is called biscuit"));  // already known
    CHECK(!memory_remember(&m, "  "));
    CHECK_STR(m.text, "- Mike takes his tea black\n- Sam is 7\nfree text the owner wrote\n- The cat is called Biscuit.\n");
    CHECK(memory_forget(&m, "BISCUIT") == 1);
    CHECK(memory_forget(&m, "zz") == 0);  // too short to trust
    CHECK(memory_forget(&m, "nothing like that") == 0);
    CHECK_STR(m.text, "- Mike takes his tea black\n- Sam is 7\nfree text the owner wrote\n");

    const char *arg;
    CHECK(memory_parse_note("remember: Sam's birthday is 3 May", &arg) == NOTE_REMEMBER);
    CHECK_STR(arg, "Sam's birthday is 3 May");
    CHECK(memory_parse_note(" Forget :old phone number", &arg) == NOTE_FORGET);
    CHECK_STR(arg, "old phone number");
    CHECK(memory_parse_note("rememberance of things", &arg) == NOTE_OTHER);

    // Full: the oldest lines make way, whole lines only.
    memory_load(&m, "");
    char fact[64];
    for (int i = 0; i < 400; i++) {
        snprintf(fact, sizeof fact, "fact number %d is worth keeping", i);
        memory_remember(&m, fact);
    }
    CHECK(m.len <= MEMORY_MAX && m.len > MEMORY_MAX - 64);
    CHECK(strncmp(m.text, "- fact number ", 14) == 0);
    CHECK(strstr(m.text, "fact number 399 ") != NULL && strstr(m.text, "fact number 0 ") == NULL);
    // A long file loads its newest lines.
    char *big = malloc(20000);
    big[0] = '\0';
    for (int i = 0; i < 500; i++) snprintf(big + strlen(big), 20000 - strlen(big), "- line %d of a long memory file\n", i);
    memory_load(&m, big);
    free(big);
    CHECK(strstr(m.text, "- line 499 ") != NULL && strstr(m.text, "- line 0 ") == NULL);
    CHECK(m.text[m.len - 1] == '\n');
}

// Counts what the idle face does over a stretch of time, in 100 ms steps.
typedef struct {
    int blinks, glances, moods, changes;
    bool sleepy, still_at_end;
    uint32_t longest_blink;
} idle_log_t;

static idle_log_t run_idle(idle_t *s, uint32_t from, uint32_t to)
{
    idle_log_t log = {0};
    idle_pose_t prev, p;
    idle_pose(s, from, &prev);
    uint32_t blink_start = 0, last_change = from;
    for (uint32_t step = 100; step <= to - from; step += 100) {  // works across a clock wrap too
        uint32_t t = from + step;
        idle_pose(s, t, &p);
        if (p.blink && !prev.blink) {
            log.blinks++;
            blink_start = t;
        }
        if (!p.blink && prev.blink && t - blink_start > log.longest_blink) log.longest_blink = t - blink_start;
        if (p.look != 0.5f && prev.look == 0.5f) log.glances++;
        if (p.expr != EXPR_NEUTRAL && p.expr != EXPR_SLEEPY && prev.expr != p.expr) log.moods++;
        if (p.expr == EXPR_SLEEPY) log.sleepy = true;
        if (p.blink != prev.blink || p.look != prev.look || p.expr != prev.expr || strcmp(p.prop, prev.prop)) {
            log.changes++;
            last_change = t;
        }
        prev = p;
    }
    log.still_at_end = to - last_change >= 60000;
    return log;
}

static void test_idle(void)
{
    CHECK(idle_style_from_name("off") == IDLE_OFF && idle_style_from_name("lively") == IDLE_LIVELY);
    CHECK(idle_style_from_name("calm") == IDLE_CALM && idle_style_from_name("???") == IDLE_CALM);

    idle_t s;
    idle_init(&s, IDLE_OFF, 10, 1, 0);
    idle_log_t off = run_idle(&s, 0, 3600000);
    CHECK(off.changes == 0);  // never moves

    // Calm, first five minutes: blinks every 7-15 s, the odd glance and mood, no dozing.
    idle_init(&s, IDLE_CALM, 10, 42, 1000);
    idle_log_t calm = run_idle(&s, 1000, 301000);
    CHECK(calm.blinks >= 18 && calm.blinks <= 45);
    CHECK(calm.longest_blink >= 500 && calm.longest_blink <= 700);
    CHECK(calm.glances >= 3);
    CHECK(calm.moods >= 1);
    CHECK(!calm.sleepy);

    idle_init(&s, IDLE_LIVELY, 10, 42, 1000);
    idle_log_t lively = run_idle(&s, 1000, 301000);
    CHECK(lively.blinks > calm.blinks && lively.moods > calm.moods);

    // Drowsy after the doze time, asleep (and still) after three times that.
    idle_log_t drowsy = run_idle(&s, 601000, 900000);
    CHECK(drowsy.sleepy);
    idle_log_t asleep = run_idle(&s, 1900000, 3600000);
    CHECK(asleep.changes == 0);
    idle_pose_t p;
    idle_pose(&s, 3600000, &p);
    CHECK(p.blink && p.expr == EXPR_SLEEPY && strcmp(p.prop, "z z") == 0);

    // A button or a reply wakes it right up.
    idle_wake(&s, 3600000);
    idle_pose(&s, 3600100, &p);
    CHECK(p.expr != EXPR_SLEEPY && strcmp(p.prop, "z z") != 0);

    // Doze 0: never drowsy. Wraps of the millisecond clock don't confuse it.
    idle_init(&s, IDLE_CALM, 0, 7, 0xFFFF0000u);
    idle_log_t awake = run_idle(&s, 0xFFFF0000u, 0xFFFF0000u + 3000000u);
    CHECK(!awake.sleepy && awake.blinks > 100);
}

static void test_utf8_truncate(void)
{
    const char *s = "ab\xC3\xA9\xE2\x82\xAC";  // a b é €
    CHECK(utf8_truncate(s, 0) == 0);
    CHECK(utf8_truncate(s, 2) == 2);
    CHECK(utf8_truncate(s, 3) == 2);  // would split é
    CHECK(utf8_truncate(s, 4) == 4);
    CHECK(utf8_truncate(s, 5) == 4);  // would split €
    CHECK(utf8_truncate(s, 6) == 4);
    CHECK(utf8_truncate(s, 7) == 7);
}

// ---------------------------------------------------------------- text for the voice

static void test_tts_prepare(void)
{
    char out[256];
    CHECK(tts_prepare("Hello there.", 100, 100, out, sizeof out) == strlen("Hello there."));
    CHECK_STR(out, "Hello there.");

    // Characters Pico would read as markup are replaced.
    tts_prepare("a<b>c & d", 100, 100, out, sizeof out);
    CHECK_STR(out, "a b c  and  d");
    CHECK(strchr(out, '<') == NULL && strchr(out, '>') == NULL && strchr(out, '&') == NULL);

    // Typographic punctuation, line breaks and control characters.
    tts_prepare("It\xE2\x80\x99s \xE2\x80\x9C" "fine\xE2\x80\x9D\xE2\x80\x94really\xE2\x80\xA6\nOK\x01", 100, 100, out, sizeof out);
    CHECK_STR(out, "It's \"fine\", really... OK");
    tts_prepare("Caf\xC3\xA9 \xC2\xA3" "5", 100, 100, out, sizeof out);
    CHECK_STR(out, "Caf\xC3\xA9 \xC2\xA3" "5");  // other characters pass through

    // Speed and pitch markup, only when they aren't 100%.
    tts_prepare("Hi.", 120, 90, out, sizeof out);
    CHECK_STR(out, "<speed level=\"120\"><pitch level=\"90\">Hi.</pitch></speed>");
    tts_prepare("Hi.", 80, 100, out, sizeof out);
    CHECK_STR(out, "<speed level=\"80\">Hi.</speed>");
    tts_prepare("Hi.", 100, 110, out, sizeof out);
    CHECK_STR(out, "<pitch level=\"110\">Hi.</pitch>");

    // Long text is cut on a character boundary, keeping the closing markup.
    char small[24];
    size_t n = tts_prepare("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 100, 100,
                           small, sizeof small);
    CHECK(n == 22 && n == strlen(small));
    CHECK(utf8_truncate(small, n) == n);
    char tagged[48];
    n = tts_prepare("A fairly long sentence that won't fit in the space.", 150, 100, tagged, sizeof tagged);
    CHECK(n == strlen(tagged) && n < sizeof tagged);
    static const char head[] = "<speed level=\"150\">A fairly";
    CHECK(strncmp(tagged, head, sizeof head - 1) == 0);
    CHECK(n >= 8 && strcmp(tagged + n - 8, "</speed>") == 0);
    // No room for the markup at all: nothing rather than unbalanced tags.
    CHECK(tts_prepare("Hi.", 150, 100, small, 16) == 0 && small[0] == '\0');
    CHECK(tts_prepare("", 100, 100, out, sizeof out) == 0 && out[0] == '\0');
    CHECK(tts_prepare("x", 100, 100, out, 0) == 0);
}

// ---------------------------------------------------------------- wake word

static void test_wake_words(void)
{
    CHECK(wake_word_find("hey jarvis") == &WAKE_WORDS[1]);
    CHECK(wake_word_find("Hey_Jarvis") == &WAKE_WORDS[1]);
    CHECK(wake_word_find("HEY, JARVIS!") == &WAKE_WORDS[1]);
    CHECK(wake_word_find("ok nabu") == &WAKE_WORDS[3] && wake_word_find("okay_nabu") == &WAKE_WORDS[3]);
    CHECK(wake_word_find("alexa") == &WAKE_WORDS[0] && wake_word_find("hey mycroft") == &WAKE_WORDS[2]);
    CHECK(wake_word_find("hey claude") == NULL && wake_word_find("off") == NULL && wake_word_find("") == NULL);
    CHECK(wake_word_is_off("off") && wake_word_is_off("None") && wake_word_is_off("") && !wake_word_is_off("alexa"));
    CHECK(wake_sens_from_name("Normal") == WAKE_SENS_NORMAL && wake_sens_from_name("loud") == -1);
    for (int i = 0; i < WAKE_WORD_COUNT; i++) {
        const wake_word_t *w = &WAKE_WORDS[i];
        float lo = wake_word_cutoff(w, WAKE_SENS_LOW), mid = wake_word_cutoff(w, WAKE_SENS_NORMAL),
              hi = wake_word_cutoff(w, WAKE_SENS_HIGH);
        CHECK(mid == w->cutoff && lo > mid && lo < 1.0f && hi < mid && hi >= 0.75f);
    }
}

// A steady level for `ms`, in 32 ms chunks as the recorder reads them.
static endpoint_result_t ep_run(endpoint_t *e, float db, int ms, int *at_ms)
{
    endpoint_result_t r = EP_LISTENING;
    for (int t = 0; t < ms && r == EP_LISTENING; t += 32) {
        r = endpoint_feed(e, db, 32);
        if (at_ms) *at_ms = e->elapsed_ms;
    }
    return r;
}

static void test_endpoint(void)
{
    endpoint_t e;
    int at = 0;
    // A question after a moment's quiet: done about a second after it ends.
    endpoint_init(&e);
    CHECK(ep_run(&e, -55, 500, NULL) == EP_LISTENING);
    CHECK(ep_run(&e, -25, 2500, NULL) == EP_LISTENING);
    CHECK(ep_run(&e, -54, 3000, &at) == EP_DONE);
    CHECK(at >= 3000 + EP_END_SILENCE_MS && at <= 3000 + EP_END_SILENCE_MS + 64);

    // Short pauses between words don't end it.
    endpoint_init(&e);
    ep_run(&e, -50, 300, NULL);
    for (int i = 0; i < 5; i++) {
        CHECK(ep_run(&e, -22, 600, NULL) == EP_LISTENING);
        CHECK(ep_run(&e, -49, 700, NULL) == EP_LISTENING);
    }
    CHECK(ep_run(&e, -49, 2000, NULL) == EP_DONE);

    // Talking straight away, before the background could be heard.
    endpoint_init(&e);
    CHECK(ep_run(&e, -20, 1500, NULL) == EP_LISTENING && e.started);
    CHECK(ep_run(&e, -52, 2000, NULL) == EP_DONE);

    // Nothing said: gives up after EP_NO_SPEECH_MS.
    endpoint_init(&e);
    CHECK(ep_run(&e, -52, 20000, &at) == EP_NOTHING);
    CHECK(at >= EP_NO_SPEECH_MS && at < EP_NO_SPEECH_MS + 64);

    // Clicks and a cough don't count as a question.
    endpoint_init(&e);
    ep_run(&e, -52, 300, NULL);
    for (int i = 0; i < 6; i++) {
        ep_run(&e, -25, 64, NULL);
        ep_run(&e, -52, 800, NULL);
    }
    CHECK(!e.started);
    CHECK(ep_run(&e, -52, 20000, NULL) == EP_NOTHING);

    // A noisy room: speech has to stand out from it.
    endpoint_init(&e);
    ep_run(&e, -42, 300, NULL);
    CHECK(ep_run(&e, -38, 3000, NULL) == EP_LISTENING && !e.started);
    CHECK(ep_run(&e, -24, 1000, NULL) == EP_LISTENING && e.started);
    CHECK(ep_run(&e, -41, 2000, NULL) == EP_DONE);
}

static void test_power_src(void)
{
    power_src_t p;
    int64_t t = 0;
    // Steps of 5 s; returns the verdict at the end.
    #define RUN(host, mv, quiet, secs) \
        do { for (int _i = 0; _i < (secs) / 5; _i++) { t += 5000; power_src_update(&p, host, mv, quiet, t); } } while (0)

    // On battery, slowly discharging: never USB.
    power_src_init(&p, t);
    for (int mv = 4000; mv > 3600; mv -= 2) RUN(false, mv, true, 5);
    CHECK(!p.usb);

    // A computer on the port: USB straight away, battery as soon as it's gone.
    power_src_init(&p, t);
    t += 1000;
    CHECK(power_src_update(&p, true, 3800, true, t));
    t += 1000;
    CHECK(!power_src_update(&p, false, 3800, true, t));

    // A charger with a flat battery: the voltage climbs, so USB within minutes.
    power_src_init(&p, t);
    RUN(false, 3650, true, 120);
    CHECK(!p.usb);
    int mv = 3650;
    int64_t plugged = t;
    while (!p.usb && t - plugged < 30 * 60000) {
        mv += 1;  // ~12 mV a minute while charging
        RUN(false, mv, true, 5);
    }
    CHECK(p.usb && t - plugged < 8 * 60000);

    // Full on the charger: stays USB; unplugged, it sags and goes back to battery.
    power_src_init(&p, t);
    RUN(false, 4190, true, 120);
    CHECK(p.usb);
    RUN(false, 4185, true, 3600);
    CHECK(p.usb);
    int64_t unplugged = t;
    for (mv = 4150; mv > 3900 && p.usb; mv -= 1) RUN(false, mv, true, 10);
    CHECK(!p.usb && t - unplugged < 60 * 60000);

    // On battery, a reply sags the voltage, which then recovers: not mistaken
    // for charging, because readings wait for a minute of quiet.
    power_src_init(&p, t);
    RUN(false, 3900, true, 120);
    RUN(false, 3780, false, 20);
    RUN(false, 3890, false, 10);
    RUN(false, 3895, true, 300);
    CHECK(!p.usb);

    // Failed readings change nothing.
    RUN(false, -1, true, 600);
    CHECK(!p.usb);
    #undef RUN
}

int main(void)
{
    test_sse();
    test_text_stream();
    test_captions();
    test_gfx();
    test_config();
    test_voice_ctl();
    test_audio_level();
    test_utf8_truncate();
    test_tts_prepare();
    test_asr_tidy();
    test_memory();
    test_idle();
    test_wake_words();
    test_endpoint();
    test_power_src();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

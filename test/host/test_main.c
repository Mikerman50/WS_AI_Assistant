// Host unit tests for the portable core: cc builds them, no ESP-IDF needed.
//   make -C test/host test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "audio_level.h"
#include "captions.h"
#include "config_parse.h"
#include "face.h"
#include "gfx.h"
#include "multipart.h"
#include "sse.h"
#include "stt_parse.h"
#include "text_stream.h"
#include "tts_text.h"
#include "utf8.h"
#include "voice_ctl.h"

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
} ts_log_t;

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
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i += chunk) ts_feed(&t, text + i, (len - i) < chunk ? (len - i) : chunk);
    ts_finish(&t);
    return log;
}

static void test_text_stream(void)
{
    for (size_t chunk = 1; chunk <= 9; chunk += 4) {
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
    CHECK_STR(c.elevenlabs_stt_model, "scribe_v2");
    CHECK_STR(c.elevenlabs_language, "");
    CHECK(c.listen_max_seconds == 30 && c.listen_mic_gain_db == 30);
    CHECK(c.speech_enabled && c.speech_volume == 70 && c.speech_speed == 100 && c.speech_pitch == 100);

    // Every field set. (elevenlabs.voice_id is left over from an older version
    // and is ignored, like any unknown setting.)
    const char *full =
        "{\n"
        "  \"wifi\": {\"ssid\": \"Caf\xC3\xA9 net\", \"password\": \"\"},\n"
        "  \"claude\": {\"api_key\": \"sk-ant-xyz\", \"model\": \"claude-sonnet-5-5\", \"max_tokens\": 500,\n"
        "             \"web_search\": true, \"context\": \"Our cat is called Biscuit.\"},\n"
        "  \"elevenlabs\": {\"api_key\": \"el-key\", \"voice_id\": \"v123\", \"stt_model\": \"scribe_v1\",\n"
        "                 \"language\": \"en\"},\n"
        "  \"listen\": {\"max_seconds\": 45, \"mic_gain_db\": 36},\n"
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
    CHECK_STR(c.elevenlabs_api_key, "el-key");
    CHECK_STR(c.elevenlabs_stt_model, "scribe_v1");
    CHECK_STR(c.elevenlabs_language, "en");
    CHECK(c.listen_max_seconds == 45 && c.listen_mic_gain_db == 36);
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
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"listen\": {\"max_seconds\": 90}}",
         "listen.max_seconds must be 5 to 60."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"listen\": {\"mic_gain_db\": -1}}",
         "listen.mic_gain_db must be 0 to 42."},
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
    CHECK(parse("{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"},"
                " \"elevenlabs\": {\"api_key\": \"YOUR_ELEVENLABS_API_KEY\", \"stt_model\": \"\"}}", &c, err));
    CHECK_STR(c.elevenlabs_api_key, "");
    CHECK_STR(c.elevenlabs_stt_model, "scribe_v2");
    CHECK_STR(c.claude_model, "claude-haiku-5-5");
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
}

// ---------------------------------------------------------------- multipart upload framing

static void test_multipart(void)
{
    char head[512], tail[64];
    mp_field_t f[] = {{"model_id", "scribe_v2"}, {"file_format", "pcm_s16le_16"}};
    int n = multipart_head(head, sizeof head, "B0UNDARY", f, 2, "file", "speech.pcm", "application/octet-stream");
    CHECK_STR(head,
              "--B0UNDARY\r\nContent-Disposition: form-data; name=\"model_id\"\r\n\r\nscribe_v2\r\n"
              "--B0UNDARY\r\nContent-Disposition: form-data; name=\"file_format\"\r\n\r\npcm_s16le_16\r\n"
              "--B0UNDARY\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.pcm\"\r\n"
              "Content-Type: application/octet-stream\r\n\r\n");
    CHECK(n == (int)strlen(head));
    int k = multipart_tail(tail, sizeof tail, "B0UNDARY");
    CHECK_STR(tail, "\r\n--B0UNDARY--\r\n");
    CHECK(k == (int)strlen(tail));
    CHECK(multipart_head(head, 40, "B0UNDARY", f, 2, "file", "a", "b") == -1);  // too small
    CHECK(multipart_tail(tail, 8, "B0UNDARY") == -1);
}

// ---------------------------------------------------------------- transcript parsing

static void test_stt_parse(void)
{
    char text[64], err[96];

    const char *ok =
        "{\"language_code\":\"en\",\"language_probability\":0.98,"
        "\"text\":\"  Will it rain\\n tomorrow?  \",\"words\":[{\"text\":\"Will\",\"type\":\"word\",\"logprob\":0}]}";
    CHECK(stt_parse_response(200, ok, text, sizeof text, err, sizeof err) == STT_TEXT);
    CHECK_STR(text, "Will it rain tomorrow?");

    CHECK(stt_parse_response(200, "{\"text\":\"\"}", text, sizeof text, err, sizeof err) == STT_EMPTY);
    CHECK(stt_parse_response(200, "{\"text\":\" ... \"}", text, sizeof text, err, sizeof err) == STT_EMPTY);
    CHECK_STR(text, "");
    CHECK(stt_parse_response(200, "{\"text\":\"Caf\xC3\xA9\"}", text, sizeof text, err, sizeof err) == STT_TEXT);
    CHECK_STR(text, "Caf\xC3\xA9");

    // Long transcripts are cut on a character boundary.
    char small[8];
    CHECK(stt_parse_response(200, "{\"text\":\"abcd\xC3\xA9\xC3\xA9xyz\"}", small, sizeof small, err, sizeof err) == STT_TEXT);
    CHECK_STR(small, "abcd\xC3\xA9");

    CHECK(stt_parse_response(200, "not json", text, sizeof text, err, sizeof err) == STT_ERROR);
    CHECK_STR(err, "ElevenLabs sent an unexpected reply.");
    CHECK(stt_parse_response(401, "{\"detail\":{\"status\":\"invalid_api_key\",\"message\":\"Invalid API key\"}}",
                             text, sizeof text, err, sizeof err) == STT_ERROR);
    CHECK_STR(err, "ElevenLabs didn't accept the API key.");
    CHECK(stt_parse_response(400, "{\"detail\":{\"status\":\"quota_exceeded\",\"message\":\"...\"}}",
                             text, sizeof text, err, sizeof err) == STT_ERROR);
    CHECK_STR(err, "The ElevenLabs account is out of credit.");
    CHECK(stt_parse_response(422, "{\"detail\":[{\"loc\":[\"body\",\"model_id\"],\"msg\":\"bad\",\"type\":\"x\"}]}",
                             text, sizeof text, err, sizeof err) == STT_ERROR);
    CHECK_STR(err, "ElevenLabs couldn't use the recording.");
    CHECK(stt_parse_response(429, NULL, text, sizeof text, err, sizeof err) == STT_ERROR);
    CHECK_STR(err, "ElevenLabs is busy. Try again in a moment.");
    CHECK(stt_parse_response(503, "", text, sizeof text, err, sizeof err) == STT_ERROR);
    CHECK_STR(err, "Transcription failed (error 503).");
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

int main(void)
{
    test_sse();
    test_text_stream();
    test_captions();
    test_gfx();
    test_config();
    test_voice_ctl();
    test_audio_level();
    test_multipart();
    test_stt_parse();
    test_utf8_truncate();
    test_tts_prepare();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

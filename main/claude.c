#include "claude.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "claude_prompt.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "memory.h"
#include "sse.h"
#include "text_stream.h"

static const char *TAG = "claude";

#define API_URL "https://api.anthropic.com/v1/messages"
#define API_VERSION "2023-06-01"
#define HISTORY_TURNS 6
#define HISTORY_RESET_US (5LL * 60 * 1000 * 1000)  // a new conversation after 5 quiet minutes
#define READ_CHUNK 2048
#define SSE_MAX (256 * 1024)  // web search results can make single events large

typedef struct {
    char *user;
    char *assistant;
} turn_t;

static turn_t s_hist[HISTORY_TURNS];
static int s_hist_n;
static int64_t s_last_us;
static volatile bool s_forget;
static char s_api_key[200];
static char s_model[64];
static int s_max_tokens = 300;
static bool s_web_search;
static char *s_system;  // system prompt, personality and household context (memory is added per request)

typedef struct {
    text_stream_t ts;
    claude_sentence_cb_t on_sentence;
    void *ctx;
    char *reply;  // the raw reply, cues included, kept for the conversation history
    size_t reply_len, reply_cap;
    bool done;
    char api_error[160];
    char stop_reason[32];
} stream_t;

static void *ps_malloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static char *ps_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = ps_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void history_clear(void)
{
    for (int i = 0; i < s_hist_n; i++) {
        free(s_hist[i].user);
        free(s_hist[i].assistant);
    }
    s_hist_n = 0;
}

static void history_add(const char *user, const char *assistant)
{
    if (s_hist_n == HISTORY_TURNS) {
        free(s_hist[0].user);
        free(s_hist[0].assistant);
        memmove(&s_hist[0], &s_hist[1], sizeof(turn_t) * (HISTORY_TURNS - 1));
        s_hist_n--;
    }
    turn_t t = {ps_strdup(user), ps_strdup(assistant)};
    if (t.user && t.assistant) {
        s_hist[s_hist_n++] = t;
    } else {
        free(t.user);
        free(t.assistant);
    }
}

void claude_forget(void)
{
    s_forget = true;
}

// Case-insensitive match, ignoring spaces around `a` ("Butler " is "butler").
static bool same_word(const char *a, const char *b)
{
    while (*a == ' ') a++;
    while (*a && *b && tolower((unsigned char)*a) == *b) {
        a++;
        b++;
    }
    while (*a == ' ') a++;
    return *a == '\0' && *b == '\0';
}

void claude_init(const app_config_t *cfg)
{
    // Keep JSON trees out of internal RAM, which Wi-Fi and TLS need.
    cJSON_Hooks hooks = {.malloc_fn = ps_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);

    snprintf(s_api_key, sizeof s_api_key, "%s", cfg->claude_api_key);
    snprintf(s_model, sizeof s_model, "%s", cfg->claude_model);
    s_max_tokens = cfg->claude_max_tokens;
    s_web_search = cfg->claude_web_search;

    // The base prompt, the personality, and whatever the household wrote about
    // itself in config.json.
    static const struct {
        const char *name, *text;
    } k_personalities[] = {CLAUDE_PERSONALITIES};
    const char *persona = cfg->claude_personality;
    for (size_t i = 0; i < sizeof k_personalities / sizeof k_personalities[0]; i++)
        if (same_word(persona, k_personalities[i].name)) persona = k_personalities[i].text;
    static const char style[] = "\nYour personality (keep to it in every reply): ";
    static const char about[] = "\n\nAbout this household (from the owner):\n";
    size_t n = sizeof CLAUDE_SYSTEM_PROMPT + sizeof style + strlen(persona) + sizeof about +
               strlen(cfg->claude_context) + 1;
    s_system = ps_malloc(n);
    if (s_system) {
        snprintf(s_system, n, "%s%s%s%s%s", CLAUDE_SYSTEM_PROMPT, style, persona,
                 cfg->claude_context[0] ? about : "", cfg->claude_context);
    }
    ESP_LOGI(TAG, "model %s, web search %s, personality: %s", s_model, s_web_search ? "on" : "off",
             cfg->claude_personality);
}

static void reply_append(stream_t *st, const char *s)
{
    size_t n = strlen(s);
    if (st->reply_len + n + 1 > st->reply_cap) {
        size_t cap = st->reply_cap ? st->reply_cap * 2 : 1024;
        while (cap < st->reply_len + n + 1) cap *= 2;
        char *p = heap_caps_realloc(st->reply, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) return;
        st->reply = p;
        st->reply_cap = cap;
    }
    memcpy(st->reply + st->reply_len, s, n);
    st->reply_len += n;
    st->reply[st->reply_len] = '\0';
}

static void on_sentence(const char *sentence, const char *cue, void *ctx)
{
    stream_t *st = ctx;
    ESP_LOGI(TAG, "[%s] %s", cue, sentence);
    if (st->on_sentence) st->on_sentence(sentence, cue, st->ctx);
}

static void on_note(const char *note, void *ctx)
{
    (void)ctx;
    memory_note(note);
}

static void on_event(const char *event, const char *data, void *ctx)
{
    stream_t *st = ctx;
    if (strcmp(event, "content_block_delta") == 0) {
        cJSON *j = cJSON_Parse(data);
        cJSON *delta = cJSON_GetObjectItem(j, "delta");
        cJSON *type = cJSON_GetObjectItem(delta, "type");
        cJSON *text = cJSON_GetObjectItem(delta, "text");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "text_delta") == 0 && cJSON_IsString(text)) {
            reply_append(st, text->valuestring);
            ts_feed(&st->ts, text->valuestring, strlen(text->valuestring));
        }
        cJSON_Delete(j);
    } else if (strcmp(event, "message_delta") == 0) {
        cJSON *j = cJSON_Parse(data);
        cJSON *reason = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "delta"), "stop_reason");
        if (cJSON_IsString(reason)) snprintf(st->stop_reason, sizeof st->stop_reason, "%s", reason->valuestring);
        cJSON_Delete(j);
    } else if (strcmp(event, "message_stop") == 0) {
        st->done = true;
    } else if (strcmp(event, "error") == 0) {
        cJSON *j = cJSON_Parse(data);
        cJSON *msg = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "error"), "message");
        snprintf(st->api_error, sizeof st->api_error, "%s", cJSON_IsString(msg) ? msg->valuestring : "stream error");
        cJSON_Delete(j);
        st->done = true;
    }
    // message_start, content_block_start/stop and ping need no action
}

static void add_message(cJSON *msgs, const char *role, const char *text)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", text);
    cJSON_AddItemToArray(msgs, m);
}

static char *build_body(const char *question)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddStringToObject(root, "model", s_model);
    cJSON_AddNumberToObject(root, "max_tokens", s_max_tokens);
    cJSON_AddBoolToObject(root, "stream", 1);
    // What it remembers changes as it learns, so it's added to each request.
    const char *mem = memory_text();
    static const char mem_head[] = "\n\nWhat you remember (your memory file):\n";
    static const char mem_none[] = "\n\nWhat you remember: nothing yet.\n";
    const char *base = s_system ? s_system : CLAUDE_SYSTEM_PROMPT;
    size_t sn = strlen(base) + sizeof mem_head + strlen(mem) + 1;
    char *sys = ps_malloc(sn);
    if (sys) snprintf(sys, sn, "%s%s%s", base, mem[0] ? mem_head : mem_none, mem);
    cJSON_AddStringToObject(root, "system", sys ? sys : base);
    free(sys);
    if (s_web_search) {
        cJSON *tools = cJSON_AddArrayToObject(root, "tools");
        cJSON *search = cJSON_CreateObject();
        cJSON_AddStringToObject(search, "type", "web_search_20250305");
        cJSON_AddStringToObject(search, "name", "web_search");
        cJSON_AddNumberToObject(search, "max_uses", 2);
        cJSON_AddItemToArray(tools, search);
    }
    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");
    for (int i = 0; i < s_hist_n; i++) {
        add_message(msgs, "user", s_hist[i].user);
        add_message(msgs, "assistant", s_hist[i].assistant);
    }
    add_message(msgs, "user", question);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

static void describe_http_error(int status, const char *body, char *err, size_t errsz)
{
    cJSON *j = cJSON_Parse(body);
    cJSON *msg = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "error"), "message");
    ESP_LOGE(TAG, "HTTP %d: %s", status, cJSON_IsString(msg) ? msg->valuestring : body);
    switch (status) {
    case 401: snprintf(err, errsz, "Claude didn't accept the API key."); break;
    case 429: snprintf(err, errsz, "I'm being rate limited. Try again in a minute."); break;
    case 529: snprintf(err, errsz, "Claude is overloaded right now. Try again shortly."); break;
    default: snprintf(err, errsz, "Claude returned error %d.", status); break;
    }
    cJSON_Delete(j);
}

esp_err_t claude_ask(const char *question, claude_sentence_cb_t cb, void *ctx, volatile bool *cancel, char *err,
                     size_t errsz)
{
    esp_err_t ret = ESP_FAIL;
    char *body = NULL, *buf = NULL;
    stream_t *st = NULL;
    sse_t sse;
    bool sse_ok = false;
    esp_http_client_handle_t http = NULL;
    int len = 0, status = 0, zero_reads = 0;
    bool first = true;
    int64_t t0 = esp_timer_get_time();
    esp_http_client_config_t cfg = {
        .url = API_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };

    err[0] = '\0';
    if (s_api_key[0] == '\0') {
        snprintf(err, errsz, "There's no API key set yet.");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_forget || (s_hist_n > 0 && t0 - s_last_us > HISTORY_RESET_US)) {
        history_clear();
        s_forget = false;
    }

    body = build_body(question);
    st = ps_malloc(sizeof *st);
    buf = ps_malloc(READ_CHUNK);
    if (!body || !st || !buf) {
        snprintf(err, errsz, "I ran out of memory.");
        ret = ESP_ERR_NO_MEM;
        goto out;
    }
    memset(st, 0, sizeof *st);
    st->on_sentence = cb;
    st->ctx = ctx;
    ts_init(&st->ts, on_sentence, st);
    st->ts.on_note = on_note;
    sse_ok = sse_init(&sse, SSE_MAX, on_event, st);
    if (!sse_ok) {
        snprintf(err, errsz, "I ran out of memory.");
        ret = ESP_ERR_NO_MEM;
        goto out;
    }

    http = esp_http_client_init(&cfg);
    if (!http) {
        snprintf(err, errsz, "I couldn't start a connection.");
        goto out;
    }
    esp_http_client_set_header(http, "content-type", "application/json");
    esp_http_client_set_header(http, "x-api-key", s_api_key);
    esp_http_client_set_header(http, "anthropic-version", API_VERSION);

    len = (int)strlen(body);
    if (esp_http_client_open(http, len) != ESP_OK) {
        snprintf(err, errsz, "I couldn't connect to Claude.");
        goto out;
    }
    for (int off = 0; off < len;) {
        int w = esp_http_client_write(http, body + off, len - off);
        if (w <= 0) {
            snprintf(err, errsz, "The connection to Claude dropped.");
            goto out;
        }
        off += w;
    }
    esp_http_client_fetch_headers(http);
    status = esp_http_client_get_status_code(http);
    if (status != 200) {
        int n = esp_http_client_read(http, buf, READ_CHUNK - 1);
        buf[n > 0 ? n : 0] = '\0';
        describe_http_error(status, buf, err, errsz);
        goto out;
    }

    while (!st->done) {
        if (cancel && *cancel) break;
        int n = esp_http_client_read(http, buf, READ_CHUNK);
        if (n < 0) {
            snprintf(err, errsz, "The connection to Claude dropped.");
            break;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(http) || ++zero_reads > 3) break;
            continue;
        }
        zero_reads = 0;
        if (first) {
            ESP_LOGI(TAG, "first bytes after %lld ms", (long long)((esp_timer_get_time() - t0) / 1000));
            first = false;
        }
        sse_feed(&sse, buf, (size_t)n);
    }
    if (cancel && *cancel) {
        // Stopped by the user: keep what was said so far so the conversation still makes sense.
        if (st->reply && st->reply_len > 0) history_add(question, st->reply);
        s_last_us = esp_timer_get_time();
        ESP_LOGI(TAG, "reply stopped by the user");
        ret = ESP_ERR_NOT_FINISHED;
        goto out;
    }
    ts_finish(&st->ts);

    if (st->api_error[0]) {
        ESP_LOGE(TAG, "stream error: %s", st->api_error);
        if (!err[0]) snprintf(err, errsz, "Claude ran into a problem. Try again in a moment.");
    } else if (!err[0]) {
        ret = ESP_OK;
        if (st->reply) history_add(question, st->reply);
        s_last_us = esp_timer_get_time();
        ESP_LOGI(TAG, "reply done (%s) after %lld ms", st->stop_reason,
                 (long long)((esp_timer_get_time() - t0) / 1000));
    }

out:
    if (http) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    if (sse_ok) sse_free(&sse);
    if (st) free(st->reply);
    free(st);
    free(buf);
    if (body) cJSON_free(body);
    return ret;
}

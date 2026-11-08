#include "stt_parse.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "utf8.h"

// Copies `src` into `dst` with runs of whitespace turned into single spaces and
// no leading or trailing space, cut on a character boundary if needed.
static void tidy(const char *src, char *dst, size_t dstsz)
{
    size_t n = 0;
    bool space = false;
    for (const char *p = src; *p; p++) {
        if (isspace((unsigned char)*p)) {
            space = n > 0;
            continue;
        }
        if (space) {
            if (n + 1 >= dstsz) break;
            dst[n++] = ' ';
            space = false;
        }
        if (n + 1 >= dstsz) break;
        dst[n++] = *p;
    }
    dst[n] = '\0';
    n = utf8_truncate(dst, n);  // don't leave half a character at the end
    dst[n] = '\0';
    while (n > 0 && dst[n - 1] == ' ') dst[--n] = '\0';
}

// True if the text has at least one letter or digit (any script counts).
static bool has_words(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (isalnum(*p) || *p >= 0x80) return true;
    return false;
}

static const char *detail_status(const cJSON *root)
{
    const cJSON *detail = cJSON_GetObjectItemCaseSensitive(root, "detail");
    const cJSON *status = cJSON_IsObject(detail) ? cJSON_GetObjectItemCaseSensitive(detail, "status") : NULL;
    return cJSON_IsString(status) ? status->valuestring : "";
}

stt_result_t stt_parse_response(int status, const char *body, char *text, size_t textsz, char *err, size_t errsz)
{
    if (textsz) text[0] = '\0';
    if (errsz) err[0] = '\0';
    cJSON *root = body ? cJSON_Parse(body) : NULL;
    stt_result_t result = STT_ERROR;

    if (status == 200) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "text");
        if (!cJSON_IsString(t)) {
            snprintf(err, errsz, "ElevenLabs sent an unexpected reply.");
        } else {
            tidy(t->valuestring, text, textsz);
            result = has_words(text) ? STT_TEXT : STT_EMPTY;
            if (result == STT_EMPTY && textsz) text[0] = '\0';
        }
    } else {
        const char *why = detail_status(root);
        if (status == 401 || strstr(why, "api_key"))
            snprintf(err, errsz, "ElevenLabs didn't accept the API key.");
        else if (status == 402 || strstr(why, "quota") || strstr(why, "credit"))
            snprintf(err, errsz, "The ElevenLabs account is out of credit.");
        else if (status == 429)
            snprintf(err, errsz, "ElevenLabs is busy. Try again in a moment.");
        else if (status == 422 || status == 400)
            snprintf(err, errsz, "ElevenLabs couldn't use the recording.");
        else
            snprintf(err, errsz, "Transcription failed (error %d).", status);
    }
    cJSON_Delete(root);
    return result;
}

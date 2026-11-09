#include "assistant.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "board.h"
#include "claude.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "memory.h"
#include "recorder.h"
#include "speaker.h"
#include "stt.h"
#include "ui.h"
#include "voice_ctl.h"
#include "wake.h"
#include "wifi.h"

static const char *TAG = "assistant";

#define MIN_RECORDING_SAMPLES (AUDIO_SAMPLE_RATE / 2)  // shorter than this is a slip of the finger

typedef enum { CMD_LISTEN, CMD_ASK, CMD_SAY } cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    char *text;       // CMD_ASK and CMD_SAY
    bool hands_free;  // CMD_LISTEN: started by the wake word
} cmd_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_ctl_lock;
static voice_ctl_t s_ctl;
static volatile bool s_stop_listening;  // tap: finished speaking
static volatile bool s_cancel;          // hold while listening, or tap while busy
static int s_max_seconds = 30;
static bool s_voice_ok;
static bool s_chime;
static int s_volume = 70;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static voice_action_t ctl_button(bool long_press)
{
    xSemaphoreTake(s_ctl_lock, portMAX_DELAY);
    voice_action_t a = voice_ctl_button(&s_ctl, long_press, now_ms());
    xSemaphoreGive(s_ctl_lock);
    return a;
}

typedef enum { CTL_BUSY, CTL_LISTEN_ENDED, CTL_IDLE, CTL_RESET } ctl_event_t;

static void ctl(ctl_event_t e)
{
    xSemaphoreTake(s_ctl_lock, portMAX_DELAY);
    switch (e) {
    case CTL_BUSY: voice_ctl_busy(&s_ctl); break;
    case CTL_LISTEN_ENDED: voice_ctl_listen_ended(&s_ctl, now_ms()); break;
    case CTL_IDLE: voice_ctl_idle(&s_ctl); break;
    case CTL_RESET: voice_ctl_reset(&s_ctl); break;
    }
    xSemaphoreGive(s_ctl_lock);
}

static void start_listening(bool hands_free)
{
    s_stop_listening = false;
    s_cancel = false;
    cmd_t c = {.kind = CMD_LISTEN, .hands_free = hands_free};
    if (xQueueSend(s_queue, &c, 0) != pdTRUE) ctl(CTL_RESET);
}

void assistant_wake(void)
{
    if (!s_queue) return;
    xSemaphoreTake(s_ctl_lock, portMAX_DELAY);
    voice_action_t a = s_ctl.state == VS_IDLE ? voice_ctl_button(&s_ctl, false, now_ms()) : VA_NONE;
    xSemaphoreGive(s_ctl_lock);
    if (a == VA_START_LISTENING) start_listening(true);
}

void assistant_button(bool long_press)
{
    if (!s_queue) return;
    switch (ctl_button(long_press)) {
    case VA_START_LISTENING:
        start_listening(false);
        break;
    case VA_FINISH_LISTENING:
        ESP_LOGI(TAG, "finished speaking");
        s_stop_listening = true;
        break;
    case VA_CANCEL_LISTENING:
        ESP_LOGI(TAG, "listening cancelled");
        s_cancel = true;
        s_stop_listening = true;
        break;
    case VA_INTERRUPT:
        ESP_LOGI(TAG, "reply stopped");
        s_cancel = true;
        speaker_stop();
        ui_set_heard(NULL);
        ui_set_mode(MODE_IDLE);  // show it straight away; the reply winds down behind it
        break;
    case VA_NEW_CONVERSATION:
        claude_forget();
        ui_set_pinned("New conversation");
        break;
    case VA_NONE:
        break;
    }
}

static void on_sentence(const char *sentence, const char *cue, void *ctx)
{
    (void)ctx;
    if (!s_cancel) speaker_say(sentence, cue);
}

static void on_level(float level)
{
    ui_set_level(level);
}

static void say_error(const char *message)
{
    speaker_begin_reply();
    speaker_error(message);
    speaker_end_reply();
}

// Sends a question to Claude and queues the reply for the speaker.
static void answer(const char *question)
{
    ESP_LOGI(TAG, "you: %s", question);
    ui_set_heard(question);
    ui_set_expr(EXPR_THINKING);
    ui_set_mode(MODE_THINKING);
    speaker_begin_reply();

    char err[160] = "";
    if (!wifi_wait(10000)) {
        snprintf(err, sizeof err, "I can't reach the Wi-Fi.");
    } else if (claude_ask(question, on_sentence, NULL, &s_cancel, err, sizeof err) == ESP_ERR_NOT_FINISHED) {
        err[0] = '\0';
    }
    if (err[0] && !s_cancel) speaker_error(err);
    speaker_end_reply();
    memory_save();  // anything it chose to remember goes on the card
}

// A short rising two-note chime: "I'm listening".
static void chime(void)
{
    if (audio_speaker_open(s_volume) != ESP_OK) return;
    static const struct {
        float hz;
        int ms;
    } notes[] = {{880.0f, 90}, {1318.5f, 120}};
    int16_t buf[160];  // 10 ms
    for (size_t k = 0; k < sizeof notes / sizeof notes[0]; k++) {
        int total = notes[k].ms * AUDIO_SAMPLE_RATE / 1000, fade = AUDIO_SAMPLE_RATE / 125;  // 8 ms fades
        float phase = 0.0f, step = 2.0f * 3.14159265f * notes[k].hz / AUDIO_SAMPLE_RATE;
        for (int i = 0; i < total;) {
            int n = 0;
            for (; n < 160 && i < total; n++, i++) {
                float env = i < fade ? (float)i / fade : i > total - fade ? (float)(total - i) / fade : 1.0f;
                buf[n] = (int16_t)(9000.0f * env * sinf(phase));
                phase += step;
            }
            audio_speaker_write(buf, (size_t)n);
        }
    }
    audio_speaker_close();
}

static void listen_then_answer(bool hands_free)
{
    if (!s_voice_ok || !stt_ready()) {
        ctl(CTL_LISTEN_ENDED);
        char msg[96];
        if (s_voice_ok) snprintf(msg, sizeof msg, "I can't listen yet. %s.", stt_problem());
        else snprintf(msg, sizeof msg, "I couldn't start the microphone.");
        say_error(msg);
        return;
    }

    wake_pause();  // the microphone is ours until the recording ends
    ui_set_heard(NULL);
    ui_set_hands_free(hands_free);
    ui_set_expr(EXPR_NEUTRAL);
    ui_set_mode(MODE_LISTENING);
    if (hands_free && s_chime) chime();
    board_led(true);
    recording_t rec;
    esp_err_t err = recorder_capture(&rec, s_max_seconds, &s_stop_listening, on_level, hands_free);
    board_led(false);
    wake_resume();

    if (s_cancel) {  // held BOOT to cancel
        recorder_free(&rec);
        ui_set_mode(MODE_IDLE);
        return;
    }
    ctl(CTL_LISTEN_ENDED);  // time limit or mic error: as if BOOT had been tapped
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "recording failed: %s", esp_err_to_name(err));
        say_error("I couldn't use the microphone.");
        return;
    }
    if (rec.samples < MIN_RECORDING_SAMPLES) {
        recorder_free(&rec);
        ui_set_mode(MODE_IDLE);
        return;
    }

    ui_set_heard("\xE2\x80\xA6");  // an ellipsis until the transcript arrives
    ui_set_expr(EXPR_THINKING);
    ui_set_mode(MODE_THINKING);

    char text[1024], msg[128];
    err = stt_transcribe(rec.pcm, rec.samples, text, sizeof text, msg, sizeof msg, &s_cancel);
    recorder_free(&rec);
    if (err == ESP_ERR_NOT_FINISHED || s_cancel) {
        ui_set_heard(NULL);
        ui_set_mode(MODE_IDLE);
        return;
    }
    if (err != ESP_OK) {
        say_error(msg);
        return;
    }
    if (!text[0]) {
        say_error("Sorry, I didn't catch that.");
        return;
    }
    answer(text);
}

static void assistant_task(void *arg)
{
    (void)arg;
    for (;;) {
        cmd_t c;
        xQueueReceive(s_queue, &c, portMAX_DELAY);
        if (c.kind == CMD_LISTEN) {
            listen_then_answer(c.hands_free);
        } else if (c.kind == CMD_ASK) {
            ctl(CTL_BUSY);
            s_cancel = false;
            answer(c.text);
            free(c.text);
        } else {  // CMD_SAY: speak the text as it is, no Claude involved
            ctl(CTL_BUSY);
            s_cancel = false;
            speaker_begin_reply();
            speaker_say(c.text, "");
            speaker_end_reply();
            free(c.text);
        }
        while (speaker_busy()) vTaskDelay(pdMS_TO_TICKS(50));  // let the reply play out
        ctl(CTL_IDLE);
    }
}

static void queue_text(cmd_kind_t kind, const char *text)
{
    if (!text || !text[0]) return;
    if (!s_queue) {
        ESP_LOGW(TAG, "not ready yet: the settings need fixing first");
        return;
    }
    size_t n = strlen(text) + 1;
    char *copy = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return;
    memcpy(copy, text, n);
    cmd_t c = {.kind = kind, .text = copy};
    if (xQueueSend(s_queue, &c, 0) != pdTRUE) {
        ESP_LOGW(TAG, "still busy, dropped");
        free(copy);
    }
}

void assistant_ask(const char *question)
{
    queue_text(CMD_ASK, question);
}

void assistant_say(const char *text)
{
    queue_text(CMD_SAY, text);
}

bool assistant_busy(void)
{
    xSemaphoreTake(s_ctl_lock, portMAX_DELAY);
    bool busy = s_ctl.state != VS_IDLE;
    xSemaphoreGive(s_ctl_lock);
    return busy || uxQueueMessagesWaiting(s_queue) > 0;
}

void assistant_init(const app_config_t *cfg, bool voice_ok)
{
    s_max_seconds = cfg->listen_max_seconds;
    s_voice_ok = voice_ok;
    s_chime = cfg->wake_chime;
    s_volume = cfg->speech_volume;
    voice_ctl_init(&s_ctl);
    s_ctl_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(4, sizeof(cmd_t));
    // TLS and JSON work happen on this task, so give it a roomy stack.
    xTaskCreate(assistant_task, "assistant", 12288, NULL, 5, NULL);
}

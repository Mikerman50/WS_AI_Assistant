#include "assistant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "claude.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "face.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "speaker.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "assistant";

static QueueHandle_t s_queue;
static volatile bool s_busy;

static void on_sentence(const char *sentence, const char *cue, void *ctx)
{
    (void)ctx;
    speaker_say(sentence, cue);
}

static void assistant_task(void *arg)
{
    (void)arg;
    for (;;) {
        char *q = NULL;
        xQueueReceive(s_queue, &q, portMAX_DELAY);
        s_busy = true;
        ESP_LOGI(TAG, "you: %s", q);
        while (speaker_busy()) vTaskDelay(pdMS_TO_TICKS(50));  // let the last reply finish

        ui_set_heard(q);
        ui_set_expr(EXPR_THINKING);
        ui_set_mode(MODE_THINKING);

        char err[160] = "";
        if (!wifi_wait(10000)) snprintf(err, sizeof err, "I can't reach the Wi-Fi.");
        else claude_ask(q, on_sentence, NULL, err, sizeof err);
        if (err[0]) speaker_error(err);
        speaker_end_reply();

        free(q);
        s_busy = false;
    }
}

void assistant_ask(const char *question)
{
    if (!question || !question[0]) return;
    if (!s_queue) {
        ESP_LOGW(TAG, "not ready yet: the settings need fixing first");
        return;
    }
    size_t n = strlen(question) + 1;
    char *q = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!q) return;
    memcpy(q, question, n);
    if (xQueueSend(s_queue, &q, 0) != pdTRUE) {
        ESP_LOGW(TAG, "still busy, question dropped");
        free(q);
    }
}

bool assistant_busy(void)
{
    return s_busy || uxQueueMessagesWaiting(s_queue) > 0;
}

void assistant_init(void)
{
    s_queue = xQueueCreate(4, sizeof(char *));
    // TLS and JSON work happen on this task, so give it a roomy stack.
    xTaskCreate(assistant_task, "assistant", 10240, NULL, 5, NULL);
}

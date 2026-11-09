#include <string.h>

#include "wakeword.h"

extern const uint8_t alexa_start[] __asm__("_binary_alexa_tflite_start");
extern const uint8_t alexa_end[] __asm__("_binary_alexa_tflite_end");
extern const uint8_t hey_jarvis_start[] __asm__("_binary_hey_jarvis_tflite_start");
extern const uint8_t hey_jarvis_end[] __asm__("_binary_hey_jarvis_tflite_end");
extern const uint8_t hey_mycroft_start[] __asm__("_binary_hey_mycroft_tflite_start");
extern const uint8_t hey_mycroft_end[] __asm__("_binary_hey_mycroft_tflite_end");
extern const uint8_t okay_nabu_start[] __asm__("_binary_okay_nabu_tflite_start");
extern const uint8_t okay_nabu_end[] __asm__("_binary_okay_nabu_tflite_end");

bool ww_builtin_model(const char *file, const uint8_t **data, size_t *len)
{
    static const struct {
        const char *file;
        const uint8_t *start, *end;
    } models[] = {
        {"alexa", alexa_start, alexa_end},
        {"hey_jarvis", hey_jarvis_start, hey_jarvis_end},
        {"hey_mycroft", hey_mycroft_start, hey_mycroft_end},
        {"okay_nabu", okay_nabu_start, okay_nabu_end},
    };
    for (size_t i = 0; i < sizeof models / sizeof models[0]; i++) {
        if (strcmp(file, models[i].file) == 0) {
            *data = models[i].start;
            *len = (size_t)(models[i].end - models[i].start);
            return true;
        }
    }
    return false;
}

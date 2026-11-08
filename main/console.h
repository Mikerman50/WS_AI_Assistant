// Type questions over USB (idf.py monitor) until the microphone path exists.
// Lines starting with '/' are commands: /face, /mode, /pin, /full, /forget, /battery,
// /reload (restart and re-read config.json) and /forget-settings (delete the saved copy).
#pragma once

void console_start(void);

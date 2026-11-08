# picotts: on-device text-to-speech

SVOX Pico, the text-to-speech engine Android shipped for years, with its
British English voice (`en-GB_ta.bin` + `en-GB_kh0_sg.bin`, about 1 MB). It
produces 16 kHz, 16-bit mono audio entirely on the ESP32-S3; nothing is sent
anywhere.

Adapted from [DiUS's esp-picotts](https://github.com/DiUS/esp-picotts) 1.1.3
(Apache-2.0), with these changes:

- `picotts_direct.c` replaces DiUS's background task with a synchronous call
  that speaks one piece of text and returns when it's done, so the firmware
  knows exactly where each sentence ends (DiUS's task only reports idle after
  half a second of quiet).
- `esp_picorsrc.c`: fixed byte reading of resource headers (unsigned bytes,
  and `|` where `||` was written). Otherwise unchanged.
- Pico's fast `exp()` approximation, which depends on an endianness check in
  `picopltf.h` and is only accurate to about 4%, is replaced by `expf()`
  (accurate, and quick on the ESP32-S3's floating-point unit).
- A yield hook lets the engine pause briefly every 100 ms on the ESP32, so a
  long sentence doesn't starve other work on its core.
- Only the en-GB voice is included. `voice_data.S` embeds its two files on
  aligned addresses, which the engine needs when it reads them in place.

`pico/lib` is the unmodified upstream SVOX source (Apache-2.0, see
`pico/lib/NOTICE`). The PC tests build the same code: `make -C test/host tts`
speaks a few sentences into `test/host/build/voice_*.wav`.

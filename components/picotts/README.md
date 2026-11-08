# picotts: on-device text-to-speech

SVOX Pico, the text-to-speech engine Android shipped for years, with its
British English voice (`en-GB_ta.bin` + `en-GB_kh0_sg.bin`, about 1 MB, in
`sdcard/models/`: the firmware loads them from the SD card, because the flash is
taken up by the speech recognition model). It produces 16 kHz, 16-bit mono
audio entirely on the ESP32-S3; nothing is sent anywhere.

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
- Only the en-GB voice is included. The engine reads its files in place, so
  they must be loaded on aligned addresses (the firmware uses 16 bytes).
- `picotts_esp.c` starts and stops the engine on demand, so its 1.1 MB of
  working memory is free while the board is listening.
- Memory bugs in the engine that only show off Android, found with valgrind:
  - Pico reads parts of some objects it creates before writing them, which
    worked on Android because fresh memory there is always zero. Here memory is
    reused, so `picoos.c` now clears each allocation, and `picotts_direct.c`
    clears the whole pool before starting.
  - `picopam.c` read the syllable before the first one at a phrase boundary
    (15-20 bytes in front of an array, four places) and checked a syllable
    past the end of a sentence (one place). Both now stop at the ends.

  The changes in `pico/lib` are marked `eink_assistant`. With them the voice is
  bit-for-bit the same wherever its memory is, which `make tts` checks.

`pico/lib` is the upstream SVOX source (Apache-2.0, see `pico/lib/NOTICE`),
unchanged apart from the `picoos.c` and `picopam.c` fixes above. The PC tests build the same code: `make -C test/host tts`
speaks a few sentences into `test/host/build/voice_*.wav`.

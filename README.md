# E-ink Claude assistant (Waveshare ESP32-S3-ePaper-1.54 V2)

A fully standalone home assistant: tap BOOT, ask your question out loud, tap
again, and Claude answers through the board's speaker in a British English
voice, with subtitles over an expressive 1-bit face whose mouth moves with the
sound. The board talks straight to Claude's API and to ElevenLabs (for speech to
text) over Wi-Fi, with no server of your own. All settings come from
`config.json` on the microSD card, so one firmware image works for anyone.

The voice runs entirely on the board: SVOX Pico, the text-to-speech engine
Android shipped for years, turns each sentence into audio on the ESP32-S3
itself. Nothing is sent anywhere to be spoken. Listening still uses ElevenLabs,
because free-form speech recognition doesn't fit in this board's 8 MB of flash
(see [Speech recognition](#speech-recognition)).

![Face states rendered by the firmware's own drawing code](docs/face-preview.png)

## Talking to it

| When | BOOT | What happens |
| --- | --- | --- |
| Idle | Tap | Starts listening: the face says *Tap BOOT when done*, a meter shows your voice level and the green LED lights |
| Listening | Tap (a double tap works too) | Finished: it transcribes what you said, shows it as *you: ...*, then Claude answers out loud |
| Listening | Hold 1.5 s | Cancels without sending anything |
| Thinking or answering | Tap | Stops the reply, cutting the voice off mid-word |
| Idle | Hold 1.5 s | Starts a new conversation (Claude forgets the last few exchanges) |

Recording stops and is sent by itself after `listen.max_seconds` (30 by default).
Taps within 0.6 s of starting are ignored, as are taps within 0.7 s of finishing,
so a double tap never starts, stops or interrupts anything twice.

PWR: a short press does a full refresh to clear ghosting; holding it switches the
board off on battery, leaving the sleeping face on screen.

You can also type questions over USB (see below), which skips the microphone.

## Set up

### 1. Prepare the SD card

1. Format a microSD card as **FAT32**.
2. Copy `sdcard/config.json` to the root of the card.
3. Edit it: your Wi-Fi name and password, your Anthropic API key, and an
   ElevenLabs API key for speech to text (see the field reference below). The
   voice needs no key.
4. Put the card in the board.

On Windows, make sure the file is really called `config.json` and not
`config.json.txt` (the firmware accepts both, but other tools may not).

### 2. Get the firmware image

**From GitHub (no tools needed).** Push this folder to a GitHub repository; the
included workflow (`.github/workflows/build.yml`) builds it with ESP-IDF 5.5.6.

1. Open **Actions → Build firmware → the latest run**.
2. Download **eink-assistant-firmware** under Artifacts.
3. Unzip it to get `eink_assistant-merged.bin`, a single image that goes at
   address 0x0.

The image holds no secrets, so the repository can be public.

The same workflow also runs the PC tests and uploads **voice-samples**: WAV
files of the voice saying a few sentences, so you can hear it before flashing.

**Or build it yourself** with ESP-IDF 5.5 (the codec driver is downloaded from
the ESP Component Registry automatically on the first build):

```sh
idf.py set-target esp32s3
idf.py build merge-bin -o eink_assistant-merged.bin   # image lands in build/
```

### 3. Flash it from the browser

1. Plug the board into your computer by USB.
2. Open https://espressif.github.io/esptool-js/ in Chrome or Edge.
3. Click **Connect** and pick the board's USB serial port. If no port appears,
   hold BOOT while plugging the board in.
4. Under Program, set the flash address to **0x0**, choose
   `eink_assistant-merged.bin`, and click **Program**.
5. When it finishes, press PWR or replug the board.

From a command line instead:

```sh
esptool.py --chip esp32s3 write_flash 0x0 eink_assistant-merged.bin
```

To watch the logs and type questions, open any serial monitor at 115200 baud on
the board's USB port, or run `idf.py monitor`. Type a question and press Enter.

## config.json reference

| Field | Required | Default | Meaning |
| --- | --- | --- | --- |
| `wifi.ssid` | yes | | Your network's name |
| `wifi.password` | | `""` | Network password; leave empty for an open network |
| `claude.api_key` | yes | | Your Anthropic API key from the Claude Console |
| `claude.model` | | `claude-haiku-5-5` | Haiku is fastest; `claude-sonnet-5-5` is smarter but slower |
| `claude.max_tokens` | | `300` | Longest reply (16 to 8192) |
| `claude.web_search` | | `false` | Let Claude search the web for current information; each search is billed |
| `claude.context` | | `""` | Up to 1000 characters about your home, added to Claude's instructions |
| `elevenlabs.api_key` | for talking | | ElevenLabs API key, used for speech to text only. Without it, typed questions over USB still work and are answered out loud |
| `elevenlabs.stt_model` | | `scribe_v2` | ElevenLabs transcription model |
| `elevenlabs.language` | | `""` | Language you speak, e.g. `"en"`; empty means detect it each time (a fixed language is more accurate) |
| `listen.max_seconds` | | `30` | Longest recording before it sends by itself (5 to 60) |
| `listen.mic_gain_db` | | `30` | Microphone gain, 0 to 42 dB, in 6 dB steps |
| `display.rotation` | | `0` | Quarter turns (0 to 3) if the face is sideways or upside down |
| `display.busy_active_high` | | `true` | Set to `false` if the log shows a busy timeout on every refresh |
| `speech.enabled` | | `true` | Speak replies out loud; `false` gives subtitles only |
| `speech.volume` | | `70` | Speaker volume, 0 to 100 |
| `speech.speed` | | `100` | Speaking speed in percent of normal, 50 to 200 (e.g. `85` is slower and clearer) |
| `speech.pitch` | | `100` | Voice pitch in percent of normal, 50 to 200 |
| `speech.pace_chars_per_sec` | | `15` | How fast subtitles advance when the voice is off (5 to 40) |

A `claude.context` example: `"Our cat is called Biscuit. Use metric units. The kids are 7 and 10."`

Unknown fields are ignored, and `//` or `/* */` comments are tolerated. After a
successful read, the board keeps a copy of the settings in its own flash, so it
still starts if you take the card out. Editing `config.json` and restarting
always takes priority over that copy.

### Tuning the microphone

Each recording logs a line like `3.2 s recorded, loudest -18 dBFS`. Normal speech
at arm's length should peak somewhere around -30 to -10. If it's quieter than
about -40 (or transcripts come back empty), raise `listen.mic_gain_db` by 6; if
it's close to 0, lower it.

### The voice

Replies are spoken a sentence at a time. Claude's reply streams in, each
sentence is turned into audio as soon as it's complete, and while one sentence
plays the next is being made, so after the first one there are no gaps. The
first sentence is made in full before the board starts speaking, so there's a
short pause on the thinking face first. Each sentence logs a line like
`3.4 s of speech made in 1.1 s`, which shows how far ahead of real time the
board is.

Subtitles are timed to the actual audio: each sentence's pages are spread over
its real length, and the mouth opens with the loudness of what's playing. A tap
on BOOT cuts the voice off mid-word.

Try it without asking Claude anything: type `/say Hello, can you hear me?` in
the serial monitor. If it's too quiet, raise `speech.volume`; if it sounds
rushed, try `speech.speed` at 85 to 90.

### When something's wrong

Settings problems show a sorry face, the problem in a sentence, and *Fix it,
then press BOOT* in the bottom band. BOOT restarts and re-reads the card.

| Message | What to do |
| --- | --- |
| I can't find an SD card. | Reseat the card |
| The SD card needs to be FAT32. | Reformat as FAT32 (exFAT and NTFS aren't supported) |
| No config.json on the SD card. | Copy the file to the root of the card |
| config.json has a mistake on line N. | Usually a missing comma or quote near that line |
| *field* is missing from / empty in config.json. | Fill in that field |
| Replace the example values in config.json. | The sample placeholders are still there |
| No Wi-Fi: check config.json (bottom band) | The board can't join the network; check the name and password |
| Add an ElevenLabs key to talk (bottom band) | Add `elevenlabs.api_key`; typed questions still work and are answered out loud |

Problems while talking are spoken and shown as subtitles with the sorry face,
for example *Sorry, I didn't catch that*, *ElevenLabs didn't accept the API key*
or *I couldn't start the microphone*. If the voice itself can't start (the log
says why), replies fall back to subtitles only.

## Console commands

Over the USB serial monitor:
- any text without a leading `/` is sent to Claude as a question
- `/say <text>` speaks the text with subtitles, without asking Claude
- `/face <name>` and `/mode <idle|listening|thinking|speaking|asleep|notice>` show a face state
- `/pin <text>` puts text in the bottom band
- `/full` forces a full refresh
- `/forget` starts a new conversation
- `/battery` prints the battery voltage
- `/reload` restarts and re-reads `config.json`
- `/forget-settings` deletes the copy of the settings saved in flash

## How it fits together

```
BOOT taps -> voice_ctl -> assistant -> recorder (ES8311 mic) -> stt (ElevenLabs) -> claude (streamed)
          -> speaker: synth task (Pico, core 1) -> player task (ES8311 speaker + subtitles) -> ui -> epd
```

| File | Role |
| --- | --- |
| `main/assistant.*` | The conversation loop: listen, transcribe, ask Claude, play the reply; cancel and interrupt |
| `main/core/voice_ctl.*` | What each BOOT tap or hold means in each state, including the double-tap guards |
| `main/audio.*` | ES8311 codec and I2S, set up like XiaoZhi's support for this board, via Espressif's `esp_codec_dev`; microphone and speaker share it, and the amplifier is only on while speaking |
| `main/recorder.*` | Records 16 kHz mono into PSRAM with a live level meter |
| `main/stt.*`, `core/stt_parse.*`, `core/multipart.*` | Uploads the recording to ElevenLabs and reads the transcript |
| `main/claude.*`, `claude_prompt.h` | Streaming Claude client, conversation history, the system prompt |
| `main/speaker.*`, `core/tts_text.*` | Speaks each sentence with the on-device voice while the next is made; subtitles timed to the audio, mouth from its loudness, stopping mid-word |
| `components/picotts/` | SVOX Pico with its British English voice (about 1 MB, embedded in the firmware), adapted from DiUS's esp-picotts |
| `main/config_store.*`, `core/config_parse.*` | Reads `config.json` from the SD card, keeps a saved copy in flash |
| `main/board.*` | Pins, power latch, buttons, LED, battery reading, shared I2C bus |
| `main/epd_ssd1681.c`, `main/ui.*` | Panel driver (full and partial refresh) and the task that owns the screen |
| `main/core/` | Portable C, also built on a PC for the tests: face, graphics, fonts, captions, sentence streaming, SSE parsing, settings, level meter |

Pins and power switches were checked against XiaoZhi's support for this exact
board, and the e-paper commands against XiaoZhi's and GxEPD2's drivers for this
panel.

## Speech recognition

Speaking happens on the board, but working out what you said still goes to
ElevenLabs. Open models that can take free-form dictation on an ESP32-S3 need
more flash than this board has: the smallest practical one (Oído, a 13M
parameter Conformer) needs 8 to 14 MB of model data and a 16 MB-flash board,
and runs at about half real time even there. The options are:

- **Keep ElevenLabs for listening** (as now): accurate, any sentence, about a
  second per question, billed by audio length.
- **Fixed phrases on the board** with Espressif's ESP-SR MultiNet: up to 200
  commands written as English text ("what's the time", "good night"), fully
  offline, but it can't take arbitrary questions for Claude.
- **Free-form recognition from the SD card**: port Oído so its model streams
  from the microSD card. Untried; expect it to take two to three times as long
  as you spoke, and its engine is GPLv3, which would make the whole firmware
  GPLv3.
- **A 16 MB board** (ESP32-S3 N16R8) for Oído in flash.

## Roadmap

1. **Speech recognition**: one of the options above.
2. **Live transcripts.** Stream audio to ElevenLabs' realtime transcription while
   you speak, so the *you: ...* strip fills in as you talk.
3. **Tools and power.**
   - Claude tools for timers on the PCF85063 RTC, room readings from the SHTC3,
     pinning notes, lists and QR codes, and searching a conversation log on the
     SD card.
   - Deep sleep between conversations, and a wake word when on USB.

## Tests

```sh
make -C test/host test      # 335 unit checks with sanitisers (gestures, transcripts, settings, captions...)
make -C test/host tts       # the voice on a PC: speaks sample sentences into test/host/build/voice_*.wav
make -C test/host embed     # builds components/picotts through its CMakeLists.txt and speaks
                            # from the embedded voice data (needs cmake)
make -C test/host speaker   # runs main/speaker.c with the real voice on stand-ins for FreeRTOS,
                            # the codec and the screen: order, timing, stopping mid-word, fallbacks
make -C test/host preview   # renders every face state to test/host/build/*.pbm
./test/idf_stub/check.sh    # compiles all firmware files without ESP-IDF, twice:
                            # as GNU C, and as strict C with the ESP32-S3's 32-bit integer
                            # types to catch printf mistakes that only fail on the real toolchain
```

`tools/make_font.py` regenerates the bitmap fonts at other sizes.
`test/host/third_party/cJSON` is cJSON 1.7.18 (MIT), the same library ESP-IDF
ships as its `json` component. `test/idf_stub/esp_codec_dev` holds the headers of
esp_codec_dev 1.6.2 (Apache-2.0). Both are only used for the PC checks.

## Notes

- This code hasn't yet been compiled for the board by ESP-IDF; the first real
  build is the GitHub workflow. Every ESP-IDF and esp_codec_dev function and
  setting it uses has been checked against their actual sources (ESP-IDF 5.5.6,
  esp_codec_dev 1.6.2). The voice engine, its embedding and the text it's given
  are tested on a PC with the same code the firmware runs.
- The voice needs about 1.1 MB of PSRAM while running, and its data adds 1 MB
  to the firmware, which should come to roughly 2.5 MB of the 3 MB app slot.
  The workflow prints the real figure (`idf.py size`), and the build stops with
  a clear message if it ever doesn't fit.
- Recordings go to ElevenLabs over HTTPS for transcription and aren't stored on
  the device. ElevenLabs bills transcription by audio length.
- `config.json` holds your API keys in plain text on the card. Keep the card at
  home, and set spend limits for both keys.
- Fonts are rendered from DejaVu Sans Bold (Bitstream Vera licence with
  public-domain changes).
- The voice is SVOX Pico (Apache-2.0, `components/picotts/pico/lib/NOTICE`) by
  way of DiUS's esp-picotts (Apache-2.0); see `components/picotts/README.md` for
  what was changed.

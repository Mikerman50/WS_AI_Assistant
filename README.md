# E-ink Claude assistant (Waveshare ESP32-S3-ePaper-1.54 V2)

A fully standalone home assistant: tap BOOT, ask your question out loud, tap
again, and Claude answers through the board's speaker in a British English
voice, with subtitles over an expressive 1-bit face whose mouth moves with the
sound. The only service it talks to is Claude's API, so the only key it needs is
your Anthropic one. There's no server of your own. All settings come from
`config.json` on the microSD card, so one firmware image works for anyone.

Everything else runs on the ESP32-S3 itself:

- **Listening:** [Oído](https://github.com/lokutor-ai/oido) by Lokutor, open
  speech recognition for this chip. Its model is NVIDIA's Conformer-CTC,
  fine-tuned and cut down to 4 bits. It writes down any sentence, with no
  command list, and nothing you say leaves the board.
- **Speaking:** SVOX Pico, the text-to-speech engine Android shipped for years.

Oído's engine is GPLv3, so the firmware is GPLv3 as a whole if you share it
(see [Licences](#licences)).

![Face states rendered by the firmware's own drawing code](docs/face-preview.png)

## Talking to it

| When | BOOT | What happens |
| --- | --- | --- |
| Idle | Tap | Starts listening: the face says *Tap BOOT when done*, a meter shows your voice level and the green LED lights |
| Listening | Tap (a double tap works too) | Finished: the board works out what you said, shows it as *you: ...*, then Claude answers out loud |
| Listening | Hold 1.5 s | Cancels without sending anything |
| Thinking or answering | Tap | Stops the reply, cutting the voice off mid-word |
| Idle | Hold 1.5 s | Starts a new conversation (Claude forgets the last few exchanges) |

Recording stops by itself after `listen.max_seconds` (20 by default).
Taps within 0.6 s of starting are ignored, as are taps within 0.7 s of finishing,
so a double tap never starts, stops or interrupts anything twice.

PWR: a short press does a full refresh to clear ghosting; holding it switches the
board off on battery, leaving the sleeping face on screen.

You can also type questions over USB (see below), which skips the microphone.

## Set up

### 1. Prepare the SD card

1. Format a microSD card as **FAT32**.
2. Copy everything in `sdcard/` to the root of the card: `config.json` and the
   `models` folder. The folder holds the voice (about 1 MB) and the language
   model for listening (1.3 MB).
3. Edit `config.json`: your Wi-Fi name and password and your Anthropic API key
   (see the field reference below).
4. Put the card in the board.

On Windows, make sure the file is really called `config.json` and not
`config.json.txt` (the firmware accepts both, but other tools may not).

The board reads the card at start-up and then lets it go, so you can take the
card out once it's running. Without the `models` folder, replies are shown as
subtitles only, and listening makes a few more mistakes.

### 2. Get the firmware image

**From GitHub (no tools needed).** Push this folder to a GitHub repository; the
included workflow (`.github/workflows/build.yml`) builds it with ESP-IDF 5.5.6.

1. Open **Actions → Build firmware → the latest run**.
2. Download **eink-assistant-firmware** under Artifacts.
3. Unzip it to get `eink_assistant-merged.bin`, a single 8 MB image that goes
   at address 0x0. It includes the speech recognition model, which lives in its
   own part of the flash. The `sdcard` folder next to it is the same as the one
   in this project.

The image holds no secrets, so the repository can be public (but see
[Licences](#licences)).

The same workflow also runs the PC tests and uploads **voice-samples**: WAV
files of the voice saying a few sentences, so you can hear it before flashing.

**Or build it yourself** with ESP-IDF 5.5 (the codec driver is downloaded from
the ESP Component Registry automatically on the first build):

```sh
idf.py set-target esp32s3
idf.py build merge-bin -o eink_assistant-merged.bin   # image lands in build/
```

`idf.py flash` also writes the model, so a plain flash works too.

### 3. Flash it from the browser

1. Plug the board into your computer by USB.
2. Open https://espressif.github.io/esptool-js/ in Chrome or Edge.
3. Click **Connect** and pick the board's USB serial port. If no port appears,
   hold BOOT while plugging the board in.
4. Under Program, set the flash address to **0x0**, choose
   `eink_assistant-merged.bin`, and click **Program**. The whole 8 MB takes a
   minute or two.
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
| `claude.personality` | | `friendly` | How it talks: `friendly`, `witty`, `calm`, `butler`, `grumpy`, `pirate` or `teacher`, or your own description in up to 300 characters, e.g. `"Dry, laconic and fond of puns"` |
| `claude.context` | | `""` | Up to 1000 characters about your home, added to Claude's instructions |
| `listen.max_seconds` | | `20` | Longest recording before it stops by itself (5 to 30) |
| `listen.mic_gain_db` | | `30` | Microphone gain, 0 to 42 dB, in 6 dB steps |
| `listen.language_model` | | `true` | Use `models/oido_v2.tlm` to pick between words that sound alike; `false` is a little quicker and a little less accurate |
| `idle.activity` | | `calm` | What the face does between conversations: `off` (stays still), `calm` (blinks, glances, the occasional mood) or `lively` (more of everything) |
| `idle.doze_minutes` | | `10` | Gets drowsy after this many minutes with nothing happening, and falls asleep after three times as long (0 = never) |
| `display.rotation` | | `0` | Quarter turns (0 to 3) if the face is sideways or upside down |
| `display.busy_active_high` | | `true` | Set to `false` if the log shows a busy timeout on every refresh |
| `speech.enabled` | | `true` | Speak replies out loud; `false` gives subtitles only |
| `speech.volume` | | `70` | Speaker volume, 0 to 100 |
| `speech.speed` | | `100` | Speaking speed in percent of normal, 50 to 200 (e.g. `85` is slower and clearer) |
| `speech.pitch` | | `100` | Voice pitch in percent of normal, 50 to 200 |
| `speech.pace_chars_per_sec` | | `15` | How fast subtitles advance when the voice is off (5 to 40) |

A `claude.context` example: `"Our cat is called Biscuit. Use metric units. The kids are 7 and 10."`

Unknown fields are ignored (including the `elevenlabs` section from earlier
versions), and `//` or `/* */` comments are tolerated. After a successful read,
the board keeps a copy of the settings in its own flash, so it still starts if
you take the card out. Editing `config.json` and restarting always takes
priority over that copy.

### Between conversations

The face doesn't just sit there. It blinks, glances around, and now and then
has a passing mood: a smile, a curious look with a "?", a thoughtful "...", a
surprised "!" or a happy hum. After `idle.doze_minutes` with nothing happening
its eyelids get heavy and the odd "z" drifts up; after three times as long it
falls asleep and the screen stops changing, which rests the panel and the
battery. A tap on a button, or a reply, wakes it straight back up.

Every change is a partial refresh of the e-ink panel, with a full (flashing)
refresh every so often to clear ghosting, so a livelier face means a few more
of those and a little more power. Use `off` if you'd rather it stayed still.

### Memory

The assistant keeps a memory in `memory.txt` on the SD card: plain text, one
short fact per line, like `- Mike takes his tea black`. Everything in it is
given to Claude with every question, so it can answer things like "how do I
take my tea?" and get to know the household over time.

Claude decides what's worth keeping: names, who's who, likes and dislikes,
routines, birthdays, how you like it to talk. It saves a fact by putting a note
in its reply that isn't spoken, and the board writes it to the card when the
reply has finished. You can also just say "remember that..." or "forget
that...". The log shows `memory: remembered: ...` each time.

You can read or edit `memory.txt` on a computer whenever you like (keep one
fact per line), or start it off yourself. It keeps the newest 6 KB, about 100
facts; older lines make way. Over the console, `/memory` shows it and
`/forget-memory` empties it. If the card is out, new facts last until the next
restart.

### Listening

When you tap to finish, the board turns the recording's speech to a standard
level and runs Oído over all of it, which is more accurate than transcribing as
you go. The log shows a line like
`3.2 s of speech transcribed in 1.9 s (level x2.7): "What's the weather like tomorrow"`.
How long that takes on this board hasn't been timed yet. Lokutor's ESP32-S3
runs this model faster than real time in its streaming mode, which suggests a
second or two for a typical question.

It writes what it hears in lower case without punctuation, and the board tidies
that up a little. Claude is told the words come from speech recognition and may
contain misheard words, and asks if something doesn't make sense. Upstream's
measurements for this model, with the language model, are about 6 words in 100
wrong on clean read speech and 14 in 100 on harder speech. In a PC test with the
board's own voice asking eight questions, it got about 9 words in 10 right at
normal and loud levels. It's English only.

Each recording logs a line like `3.2 s recorded, loudest -18 dBFS`. Normal
speech at arm's length should peak somewhere around -30 to -10. The level is
evened out before recognition, but background hiss isn't, so speak within a
metre or two. If recordings are quieter than about -40, raise
`listen.mic_gain_db` by 6; if they're close to 0, lower it.

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
| No speech model: reflash (bottom band) | The flash doesn't hold the recognition model; flash the full merged image at 0x0 |
| No voice files: subtitles only (bottom band) | Copy the `models` folder to the card |

Problems while talking are spoken and shown as subtitles with the sorry face,
for example *Sorry, I didn't catch that* or *I couldn't start the microphone*.
If the voice itself can't start (the log says why), replies fall back to
subtitles only.

## Console commands

Over the USB serial monitor:
- any text without a leading `/` is sent to Claude as a question
- `/say <text>` speaks the text with subtitles, without asking Claude
- `/face <name>` and `/mode <idle|listening|thinking|speaking|asleep|notice>` show a face state
- `/pin <text>` puts text in the bottom band
- `/full` forces a full refresh
- `/forget` starts a new conversation
- `/memory` shows what it remembers; `/forget-memory` empties `memory.txt`
- `/battery` prints the battery voltage
- `/reload` restarts and re-reads `config.json`
- `/forget-settings` deletes the copy of the settings saved in flash

## How it fits together

```
BOOT taps -> voice_ctl -> assistant -> recorder (ES8311 mic) -> stt (Oído, core 1) -> claude (streamed)
          -> speaker: synth task (Pico, core 1) -> player task (ES8311 speaker + subtitles) -> ui -> epd
```

| File | Role |
| --- | --- |
| `main/assistant.*` | The conversation loop: listen, transcribe, ask Claude, play the reply; cancel and interrupt |
| `main/core/voice_ctl.*` | What each BOOT tap or hold means in each state, including the double-tap guards |
| `main/audio.*` | ES8311 codec and I2S, set up like XiaoZhi's support for this board, via Espressif's `esp_codec_dev`; microphone and speaker share it, and the amplifier is only on while speaking |
| `main/recorder.*` | Records 16 kHz mono into PSRAM with a live level meter |
| `main/stt.*`, `core/asr_text.*`, `core/audio_level.*` | Evens out the recording's level, runs Oído on its own task, tidies the text |
| `components/oido/` | Oído's engine (GPLv3) and its 5.9 MB model, which the build writes to the `asr` flash partition |
| `main/claude.*`, `claude_prompt.h` | Streaming Claude client, conversation history, the system prompt and personalities |
| `core/idle.*` | What the face does between conversations: blinks, glances, moods, dozing and sleep |
| `main/memory.*`, `core/memory_text.*` | The memory file: loaded at start-up, sent with every question, updated from notes in replies |
| `main/speaker.*`, `core/tts_text.*` | Speaks each sentence with the on-device voice while the next is made; subtitles timed to the audio, mouth from its loudness, stopping mid-word |
| `components/picotts/` | SVOX Pico, adapted from DiUS's esp-picotts, with fixes for memory bugs that only show off Android |
| `main/sdcard.*`, `main/config_store.*`, `core/config_parse.*` | Reads `config.json` and the `models` folder from the card, keeps a copy of the settings in flash |
| `main/board.*` | Pins, power latch, buttons, LED, battery reading, shared I2C bus |
| `main/epd_ssd1681.c`, `main/ui.*` | Panel driver (full and partial refresh) and the task that owns the screen |
| `main/core/` | Portable C, also built on a PC for the tests: face, graphics, fonts, captions, sentence streaming, SSE parsing, settings, levels |

Pins and power switches were checked against XiaoZhi's support for this exact
board, and the e-paper commands against XiaoZhi's and GxEPD2's drivers for this
panel.

### Memory

The 8 MB of flash holds the firmware (a 2.25 MB slot, no second slot for
updates over the air) and the 5.9 MB speech model, read in place. The 8 MB of
PSRAM holds the voice and language model files from the card (2.3 MB) and the
language model's search state (0.75 MB), plus, only while they're needed, the
recording and the recogniser's working memory (up to about 3 MB for 30 s) or
the voice engine's 1.1 MB while a reply is spoken. The voice engine is stopped
between replies, so the two never need their memory at the same time.

## Roadmap

1. **Transcripts while you speak.** Oído also has a streaming mode, which would
   fill in the *you: ...* strip as you talk, at some cost in accuracy.
2. **Tools and power.**
   - Claude tools for timers on the PCF85063 RTC, room readings from the SHTC3,
     pinning notes, lists and QR codes, and searching a conversation log on the
     SD card.
   - Deep sleep between conversations, and a wake word when on USB.

## Tests

```sh
make -C test/host test        # 388 unit checks with sanitisers (gestures, settings, captions, memory, idle...)
make -C test/host tts         # the voice on a PC: speaks sample sentences into test/host/build/voice_*.wav,
                              # and checks it's bit-for-bit repeatable wherever its memory is
make -C test/host asr         # listening end to end: the voice asks questions at three levels, the
                              # firmware's level adjustment and Oído transcribe them, word errors counted
make -C test/host speaker     # runs main/speaker.c with the real voice on stand-ins for FreeRTOS,
                              # the codec and the screen: order, timing, stopping mid-word, fallbacks
make -C test/host components  # builds components/picotts and components/oido through their own
                              # CMakeLists.txt, and checks the model fits its partition (needs cmake)
make -C test/host preview     # renders every face state to test/host/build/*.pbm
./test/idf_stub/check.sh      # compiles all firmware files without ESP-IDF, twice:
                              # as GNU C, and as strict C with the ESP32-S3's 32-bit integer
                              # types to catch printf mistakes that only fail on the real toolchain
```

`tools/make_font.py` regenerates the bitmap fonts at other sizes.
`test/host/third_party/cJSON` is cJSON 1.7.18 (MIT), the same library ESP-IDF
ships as its `json` component. `test/idf_stub/esp_codec_dev` holds the headers of
esp_codec_dev 1.6.2 (Apache-2.0). Both are only used for the PC checks.

## Licences

- **Oído's engine** (`components/oido`, GPLv3) is linked into the firmware, so
  the firmware as a whole is under the GPLv3 if you give it to anyone else:
  share its source under the same licence. For your own use there are no
  obligations.
- **Speech model** (`components/oido/model/oido_v2b.tnm`): CC-BY-SA-4.0, by
  Lokutor from NVIDIA's NeMo model. **Language model**
  (`sdcard/models/oido_v2.tlm`): CC-BY-4.0, by Lokutor.
- **Voice:** SVOX Pico (Apache-2.0, `components/picotts/pico/lib/NOTICE`) by way
  of DiUS's esp-picotts (Apache-2.0).
- **Fonts:** rendered from DejaVu Sans Bold (Bitstream Vera licence with
  public-domain changes).

## Notes

- This code hasn't yet been compiled for the board by ESP-IDF; the first real
  build is the GitHub workflow. Every ESP-IDF and esp_codec_dev function and
  setting it uses has been checked against their actual sources (ESP-IDF 5.5.6,
  esp_codec_dev 1.6.2). Both engines, and the code around them, are tested on a
  PC with the same C the firmware runs; Oído's ESP32-S3 vector kernels are the
  ones Lokutor measured on their boards.
- The app slot is 2.25 MB; the firmware should come to about 1.7 MB. The
  workflow prints the real figure (`idf.py size`), and the build stops with a
  clear message if the firmware or the model doesn't fit.
- Lokutor saw brownouts on some boards when both cores ran heavy vector code at
  once. The firmware runs the recogniser on one core, as Lokutor's own firmware
  does. If the board ever resets while working out what you said, that's the
  first thing to look at.
- `config.json` holds your Anthropic key in plain text on the card. Keep the
  card at home, and set a spend limit for the key.

# oido: speech recognition on the ESP32-S3

[Oído](https://github.com/lokutor-ai/oido) by Lokutor (Hashing Works, S.L.L.):
open-vocabulary English speech-to-text that runs on the ESP32-S3 itself, with
no cloud service and no command list. The engine here is its `tinyasr`
component, taken from commit `2feb715` (4 November 2026, release v2), with the
"v2b" model.

## Licences

- **Engine** (the `.c` and `.h` files): GNU GPL v3, see `LICENSE` and `NOTICE`.
  Because the firmware links it in, the firmware as a whole is GPLv3 if you give
  it to anyone else: share its source under the same licence. For your own use
  there are no obligations.
- **Model** (`model/oido_v2b.tnm`): CC-BY-SA-4.0. NVIDIA's
  `stt_en_conformer_ctc_small` (NeMo, CC-BY-4.0), fine-tuned and quantised to
  4 bits by Lokutor; see `NOTICE` for the training data.
- **Language model** (`sdcard/models/oido_v2.tlm` in this project): CC-BY-4.0,
  by Lokutor, trained on the public-domain LibriSpeech LM corpus.

## Changes

- `kernels.c` includes `sdkconfig.h` itself, so the ESP32-S3 vector (PIE)
  kernels are always built for the board whatever the include order.
- `CMakeLists.txt` is new: it also writes the model into the `asr` flash
  partition when the board is flashed.

Everything else is unchanged.

## The model

`oido_v2b.tnm`: 12 Conformer blocks, 80 ms frames, 4-bit weights, 5.9 MB. It is
the smallest Oído English model and the only one that fits this board's 8 MB of
flash next to the firmware. Upstream's measurements (LibriSpeech word error
rate, full context with the language model): 6.2 % on clean speech, 14.4 % on
harder speech. On Lokutor's ESP32-S3 board its streaming mode runs faster than
real time.

The firmware uses it in full-context mode on the whole recording once you tap
BOOT to finish, which is more accurate than its streaming mode.

Files the board loads from the SD card at start-up (copy this whole "models"
folder to the root of the card, next to config.json):

en-GB_ta.bin, en-GB_kh0_sg.bin
    The British English voice (SVOX Pico, Apache-2.0). Without them, replies
    are shown as subtitles only.

oido_v2.tlm
    Language model for speech recognition (Oído, by Lokutor, CC-BY-4.0,
    https://github.com/lokutor-ai/oido). Without it, recognition still works
    but makes a few more mistakes.
SHA-256 8329a4c94992653f6d1592be1d7747f2d3009473767d0d25c81c17a72af91283

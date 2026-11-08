/*
 * The British English voice, embedded in the app image and read in place from
 * flash. Pico reads the data areas inside these files with aligned loads (see
 * esp_picorsrc.c), so each file must start on an aligned address, which ESP-IDF's
 * EMBED_FILES / target_add_binary_data don't promise. CMakeLists.txt sets
 * PICOTTS_TA_FILE and PICOTTS_SG_FILE to the files' full paths.
 */
    .section .rodata.embedded, "a"

    .balign 16
    .global picotts_ta
picotts_ta:
    .incbin PICOTTS_TA_FILE
    .global picotts_ta_end
picotts_ta_end:

    .balign 16
    .global picotts_sg
picotts_sg:
    .incbin PICOTTS_SG_FILE
    .global picotts_sg_end
picotts_sg_end:

#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack, "", @progbits  /* PC test builds: no executable stack */
#endif

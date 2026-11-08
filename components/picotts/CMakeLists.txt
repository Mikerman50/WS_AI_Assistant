# SVOX Pico text-to-speech (British English voice) for ESP-IDF.
# Adapted from DiUS's esp-picotts 1.1.3 (Apache-2.0); see README.md.

set(PICO_SRCS
    picopr.c picosa.c picokpr.c picodata.c picokfst.c picodbg.c picokdt.c picoctrl.c
    picoktab.c picoacph.c picokdbg.c picoos.c picospho.c picotok.c picosig.c picocep.c
    picofftsg.c picotrns.c picosig2.c picoextapi.c picopal.c picokpdf.c picobase.c
    picopam.c picowa.c picoklex.c picoapi.c picoknow.c)
# picorsrc.c is compiled through esp_picorsrc.c, which loads resources from flash.
list(TRANSFORM PICO_SRCS PREPEND "pico/lib/")

idf_component_register(
    SRCS "picotts_direct.c" "picotts_esp.c" "esp_picorsrc.c" ${PICO_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "pico/lib" "."
    PRIV_REQUIRES heap)

# The engine is third-party C: keep its warnings out of the build, and optimise
# it for speed even in a debug-optimised (-Og) build, since it does the heavy
# lifting while a reply is being spoken.
set_source_files_properties(${PICO_SRCS} PROPERTIES COMPILE_FLAGS "-w -O2")

# Pico's own fast exp() depends on an endianness check that can misfire: rename
# it, and picotts_direct.c supplies an accurate one (see there).
set_source_files_properties("pico/lib/picoos.c" PROPERTIES COMPILE_OPTIONS "-Dpicoos_quick_exp=picoos_quick_nope")

# The voice data (about 1 MB) is loaded from the SD card at start-up (see
# main/main.c): the flash is taken up by the speech recognition model.

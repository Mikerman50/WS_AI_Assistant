# esp-micro-speech-features 1.2.3 by Kevin Ahrendt (Apache-2.0), from
# https://github.com/esphome-libs/esp-micro-speech-features: TensorFlow's audio
# front end, which turns sound into the spectrogram the wake word models expect.
file(GLOB SRC_FILES "src/*.c")
idf_component_register(SRCS ${SRC_FILES} INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src")

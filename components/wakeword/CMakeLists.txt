# Wake word spotting: microWakeWord models (Apache-2.0, see models/LICENSE and
# models/NOTICE) on TensorFlow Lite Micro with Espressif's ESP-NN kernels,
# which idf_component.yml fetches from the ESP component registry. Pinned to
# the versions ESPHome builds its micro_wake_word component with.
idf_component_register(
    SRCS "wakeword.cc" "builtin_models.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES micro_speech_features
    EMBED_FILES
        "models/alexa.tflite"
        "models/hey_jarvis.tflite"
        "models/hey_mycroft.tflite"
        "models/okay_nabu.tflite"
)
# Must match how the library itself is built, or its structures differ.
target_compile_definitions(${COMPONENT_LIB} PRIVATE TF_LITE_STATIC_MEMORY TF_LITE_DISABLE_X86_NEON)
# TensorFlow's headers aren't warning-free; our own code is checked with
# -Wall -Wextra -Werror by test/host (make wake).
set_source_files_properties(wakeword.cc PROPERTIES COMPILE_OPTIONS "-Wno-error")

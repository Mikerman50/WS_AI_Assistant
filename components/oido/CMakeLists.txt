# Oído speech recognition engine ("tinyasr") by Lokutor, GPLv3: see README.md,
# LICENSE and NOTICE. Taken from github.com/lokutor-ai/oido at commit 2feb715.
idf_component_register(
    SRCS "tinyasr.c" "kernels.c" "tinyasr_lm.c" "tasr_nemo.c" "tasr_seg.c"
    INCLUDE_DIRS "include" ".")

# As upstream builds it: fast, and with the floating-point arithmetic that its
# PC build reproduces exactly.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffp-contract=off -Wno-unused-function)

# The speech model (5.9 MB) is written to the "asr" flash partition along with
# the firmware, so flashing the merged image installs it too. It is read in
# place from flash; nothing is copied into RAM.
if(NOT BOOTLOADER_BUILD)
    set(OIDO_MODEL "${CMAKE_CURRENT_LIST_DIR}/model/oido_v2b.tnm")
    partition_table_get_partition_info(asr_offset "--partition-name asr" "offset")
    partition_table_get_partition_info(asr_size "--partition-name asr" "size")
    if(NOT asr_offset OR NOT asr_size)
        message(FATAL_ERROR "partitions.csv needs an \"asr\" partition for the speech model")
    endif()
    file(SIZE "${OIDO_MODEL}" model_size)
    math(EXPR asr_size_dec "${asr_size}" OUTPUT_FORMAT DECIMAL)
    if(model_size GREATER asr_size_dec)
        message(FATAL_ERROR "the speech model (${model_size} bytes) doesn't fit the asr partition (${asr_size_dec})")
    endif()
    esptool_py_flash_target_image(flash asr "${asr_offset}" "${OIDO_MODEL}")
endif()

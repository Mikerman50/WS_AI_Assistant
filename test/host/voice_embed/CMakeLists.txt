# Builds components/picotts through its own CMakeLists.txt on the PC, with a
# small stand-in for ESP-IDF's idf_component_register, then speaks a sentence
# from the embedded voice data. Checks the embedding (paths, quoting,
# alignment) that only the firmware build would otherwise exercise.
#   make -C test/host embed
cmake_minimum_required(VERSION 3.16)
project(voice_embed C ASM)

function(idf_component_register)
    cmake_parse_arguments(_ "" "" "SRCS;INCLUDE_DIRS;PRIV_INCLUDE_DIRS;PRIV_REQUIRES;REQUIRES" ${ARGN})
    list(FILTER __SRCS EXCLUDE REGEX "picotts_esp\\.c$")  # needs ESP-IDF
    add_library(__idf_picotts STATIC ${__SRCS})
    target_include_directories(__idf_picotts PUBLIC ${__INCLUDE_DIRS} PRIVATE ${__PRIV_INCLUDE_DIRS})
    set(COMPONENT_LIB __idf_picotts PARENT_SCOPE)
endfunction()

add_subdirectory(../../../components/picotts picotts)
add_executable(voice_embed main.c)
target_link_libraries(voice_embed __idf_picotts m)

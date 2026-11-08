# Builds components/picotts and components/oido on a PC through their own
# CMakeLists.txt files, with small stand-ins for the ESP-IDF functions they call,
# then runs both engines once. Checks what only the firmware build would
# otherwise exercise: the component files themselves, and that the speech model
# fits the "asr" partition in partitions.csv.
#   make -C test/host components
cmake_minimum_required(VERSION 3.16)
project(components_check C)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

function(idf_component_register)
    cmake_parse_arguments(_ "" "" "SRCS;INCLUDE_DIRS;PRIV_INCLUDE_DIRS;PRIV_REQUIRES;REQUIRES" ${ARGN})
    list(FILTER __SRCS EXCLUDE REGEX "_esp\\.c$")  # the ESP-IDF glue needs ESP-IDF
    get_filename_component(name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    add_library(__idf_${name} STATIC ${__SRCS})
    target_include_directories(__idf_${name} PUBLIC ${__INCLUDE_DIRS} PRIVATE ${__PRIV_INCLUDE_DIRS})
    set(COMPONENT_LIB __idf_${name} PARENT_SCOPE)
endfunction()

# Reads partitions.csv the way ESP-IDF's partition table helper answers
# "--partition-name <name>" queries for "offset" and "size".
function(partition_table_get_partition_info result args info)
    string(REGEX MATCH "--partition-name ([a-z_0-9]+)" _ "${args}")
    set(want "${CMAKE_MATCH_1}")
    file(STRINGS "${PROJECT_ROOT}/partitions.csv" lines)
    set(value "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^#")
            continue()
        endif()
        string(REPLACE " " "" line "${line}")
        string(REPLACE "," ";" fields "${line}")
        list(GET fields 0 pname)
        if(pname STREQUAL want)
            if(info STREQUAL "offset")
                list(GET fields 3 value)
            elseif(info STREQUAL "size")
                list(GET fields 4 value)
            endif()
        endif()
    endforeach()
    set(${result} "${value}" PARENT_SCOPE)
endfunction()

function(esptool_py_flash_target_image target name offset image)
    message(STATUS "would flash ${image} to the ${name} partition at ${offset}")
    set_property(GLOBAL APPEND PROPERTY FLASH_IMAGES "${name}=${image}")
endfunction()

add_subdirectory("${PROJECT_ROOT}/components/picotts" picotts)
add_subdirectory("${PROJECT_ROOT}/components/oido" oido)

get_property(images GLOBAL PROPERTY FLASH_IMAGES)
if(NOT images MATCHES "asr=.*oido_v2b\\.tnm")
    message(FATAL_ERROR "components/oido didn't ask for the model to be flashed")
endif()

add_executable(components_check main.c)
target_compile_definitions(components_check PRIVATE MODEL_FILE="${PROJECT_ROOT}/components/oido/model/oido_v2b.tnm")
target_link_libraries(components_check __idf_picotts __idf_oido m)

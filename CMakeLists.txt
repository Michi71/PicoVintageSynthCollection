cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# The version every instrument shows on its splash, taken from the repository
# rather than declared. It used to be a per-instrument "0.1" that no release
# ever moved, which made a screenshot in a bug report worthless: it named no
# build.
#
# git describe gives exactly what is wanted. On a release tag it is the release
# ("1.7.0"); anywhere else it says how far past it and which commit
# ("1.7.0-4-ga39504b"); a build with uncommitted changes gets a trailing "+".
# Sixteen or seventeen characters, and the splash font fits twenty-one.
set(PICOFACE_VERSION "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty=+
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _pf_git_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _pf_git_rc)
    if(_pf_git_rc EQUAL 0 AND _pf_git_version)
        string(REGEX REPLACE "^v" "" PICOFACE_VERSION "${_pf_git_version}")
    endif()
    # Reconfigure when the commit moves, or the string goes stale in a tree
    # that is only ever rebuilt -- which is the workflow this exists to serve.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git/packed-refs)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     ${CMAKE_CURRENT_SOURCE_DIR}/.git/packed-refs)
    endif()
endif()
message(STATUS "PicoFace version: ${PICOFACE_VERSION}")

# Fallback only: an unconditional set() would silently override both -D on the
# command line and the environment variable.
if(NOT DEFINED PICO_EXTRAS_PATH AND NOT DEFINED ENV{PICO_EXTRAS_PATH})
    set(PICO_EXTRAS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/pico-extras)
endif()
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/pico-sdk)
endif()

if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM "rp2350-arm-s")
endif()
# --- Board variant -----------------------------------------------------------
# PICOFACE_BOARD selects the physical board the firmware is built for.
#   pico             (default) any Pico-2-format RP2350A board: I2S on GP26/27/28
#   rp2350b_plus_w   Waveshare RP2350B-Plus-W: an RP2350B whose Pico header carries
#                    GP40/41/42 where a Pico has GP26/27/28. A Pico-Audio HAT on it
#                    is driven from GP40/41/42, and PIO0 has to be re-based to
#                    GPIO 16..47 to reach them (RP2350 PIO addresses 0-31 or 16-47).
set(PICOFACE_BOARD "pico" CACHE STRING "Board variant: pico | rp2350b_plus_w")
if(PICOFACE_BOARD STREQUAL "rp2350b_plus_w")
    set(PICO_BOARD "waveshare_rp2350b_plus_w")
    list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards)
    set(PICOFACE_I2S_DATA_PIN 40)
    set(PICOFACE_I2S_CLOCK_PIN_BASE 41)
    set(PICOFACE_PIO_GPIO_BASE 16)
elseif(PICOFACE_BOARD STREQUAL "pico")
    if(NOT DEFINED PICO_BOARD)
        set(PICO_BOARD "sparkfun_promicro_rp2350")
    endif()
    set(PICOFACE_I2S_DATA_PIN 26)
    set(PICOFACE_I2S_CLOCK_PIN_BASE 27)
    set(PICOFACE_PIO_GPIO_BASE 0)
else()
    message(FATAL_ERROR "PICOFACE_BOARD must be 'pico' or 'rp2350b_plus_w' (got '${PICOFACE_BOARD}')")
endif()

include(cmake/pico_sdk_import.cmake)
include(cmake/pico_extras_import.cmake)

set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -Wl,--print-memory-usage")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--print-memory-usage")

project(PicoVintageSynthCollection C CXX ASM)

pico_sdk_init()

include(PicoFaceInstrument)

# Shared third-party STATIC libraries. These do not depend on the
# per-instrument project_config.h and are therefore built once for all targets.
add_subdirectory(lib/audio)
add_subdirectory(lib/encoder)
add_subdirectory(lib/u8g2)

# Publishes PICOFACE_CORE_SOURCES / PICOFACE_CORE_INCLUDE_DIR. Must come before
# the instruments.
add_subdirectory(core)

set(PICOFACE_INSTRUMENTS_FILTER "" CACHE STRING
    "Build only these instruments (list of directory names); empty = build all")

# Collect all instrument directories; CONFIGURE_DEPENDS ensures that
# adding a new instrument directory triggers a re-configure
file(GLOB _instrument_dirs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/instruments/*)
list(SORT _instrument_dirs)

# Include every instrument directory matching the optional name filter
foreach(_dir ${_instrument_dirs})
    if(IS_DIRECTORY ${_dir} AND EXISTS ${_dir}/instrument.cmake)
        get_filename_component(_name ${_dir} NAME)
        if(NOT PICOFACE_INSTRUMENTS_FILTER OR _name IN_LIST PICOFACE_INSTRUMENTS_FILTER)
            set(PICOFACE_CURRENT_INSTRUMENT_DIR ${_dir})
            include(${_dir}/instrument.cmake)
        endif()
    endif()
endforeach()

# Verify that at least one instrument was configured
get_property(_instruments GLOBAL PROPERTY PICOFACE_INSTRUMENTS)
if(NOT _instruments)
    message(FATAL_ERROR "No instruments configured (PICOFACE_INSTRUMENTS_FILTER='${PICOFACE_INSTRUMENTS_FILTER}')")
endif()
list(LENGTH _instruments _num_instruments)
message(STATUS "Building ${_num_instruments} instrument(s): ${_instruments}")

# Aggregate target that builds all instruments
# (add_dependencies is required for target-level dependencies of custom targets)
add_custom_target(all_instruments)
add_dependencies(all_instruments ${_instruments})

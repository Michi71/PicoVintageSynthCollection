# PicoFaceJV - Roland JV-880, native engine over the original PCM data.
#
# This instrument cannot be built from the repository alone: it needs a JV-880
# ROM set, which is not distributable. Put the five files in roms/ (gitignored)
# and the instrument appears in the build; without them the whole thing is
# skipped with a note, so everyone else's build stays green.
#
# The 4.25 MB blob is embedded with .incbin rather than as a C array -- an
# initialiser list that size takes minutes and gigabytes to compile.

set(_jv_dir ${PICOFACE_CURRENT_INSTRUMENT_DIR})
set(_jv_roms ${_jv_dir}/roms)
set(_jv_required
    ${_jv_roms}/jv880_rom2.bin
    ${_jv_roms}/jv880_waverom1.bin
    ${_jv_roms}/jv880_waverom2.bin)

set(_jv_have_roms TRUE)
foreach(_f IN LISTS _jv_required)
    if(NOT EXISTS ${_f})
        set(_jv_have_roms FALSE)
    endif()
endforeach()

if(NOT _jv_have_roms)
    message(STATUS
        "PicoFaceJV: skipped - no ROM set in ${_jv_roms} "
        "(needs jv880_rom2.bin, jv880_waverom1.bin, jv880_waverom2.bin; "
        "see instruments/PicoFaceJV/README.md)")
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Opt-in build for the 4 MB Pico 2. The full instrument needs 4.33 MB of flash
# and only fits a 16 MB board; dropping the user bank and rebuilding the wave
# blob from just the samples banks A and B reach brings it to 3.76 MB, which
# leaves 234 KB spare. Banks A and B are bit-identical to the full build --
# nothing is resampled or requantised, only relocated. What is lost is the
# 64 user patches, and with them 22 samples that nothing else uses.
option(PICOFACEJV_4MB
    "PicoFaceJV: fit a 4 MB Pico 2 by shipping banks A and B only" OFF)

if(PICOFACEJV_4MB)
    set(_jv_gen ${CMAKE_CURRENT_BINARY_DIR}/PicoFaceJV_rom_ab)
    set(_jv_banks --banks=A,B)
else()
    set(_jv_gen ${CMAKE_CURRENT_BINARY_DIR}/PicoFaceJV_rom)
    set(_jv_banks "")
endif()
set(_jv_blob_s ${_jv_gen}/jv_blob.S)

# Run at configure time, not build time: the generated .S has to exist before
# the target is declared, and the conversion is a one-off of a few seconds.
# The two variants generate into different directories, so switching the option
# back and forth does not re-convert and cannot pick up the wrong blob.
if(NOT EXISTS ${_jv_blob_s})
    message(STATUS "PicoFaceJV: converting ROM set (this takes a moment)")
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/jv_extract/jv_make_blob.py
                ${_jv_roms} ${_jv_gen} ${_jv_banks}
        RESULT_VARIABLE _jv_rc
        OUTPUT_VARIABLE _jv_out
        ERROR_VARIABLE  _jv_err)
    if(NOT _jv_rc EQUAL 0)
        message(FATAL_ERROR "PicoFaceJV: ROM conversion failed\n${_jv_out}${_jv_err}")
    endif()
    message(STATUS "PicoFaceJV: ${_jv_out}")
endif()

picoface_add_instrument(
    NAME PicoFaceJV
    PROGRAM_NAME "PicoFaceJV"
    VERSION "0.1"
    USB_PID 0x1058
    DIR ${_jv_dir}

    SOURCES
        src/JV_Instrument.cpp
        src/JV_Bridge.cpp
        src/JV_Controller.cpp
        src/JV_Display.cpp
        src/JV_Midi.cpp
        src/jv_engine/jv_engine.cpp
        ${_jv_blob_s}

    INCLUDE_DIRS
        include
        # jv_calibration.h and jv_tone_map.h are shared with the host toolchain
        # and live there, so that measurement and firmware cannot drift apart.
        ${CMAKE_SOURCE_DIR}/tools/jv_extract
)

if(PICOFACEJV_4MB)
    # The panel and the MIDI bank select both have to stop offering the user
    # bank: its samples are not in the blob, so its patches would be silent.
    target_compile_definitions(PicoFaceJV PRIVATE JV_BANKS_AB_ONLY=1)
endif()

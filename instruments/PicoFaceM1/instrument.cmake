# PicoFaceM1 - Korg M1 (the plain M1, no Plus+1 expansion), native engine over
# the original PCM data.
#
# Like PicoFaceD5 and PicoFaceJV this cannot be built from the repository
# alone: it needs the M1's PCM ROMs, its coefficient ROM, its OS and the
# factory preload dump, none of which are distributable. Put them in roms/
# (gitignored) and the instrument appears in the build; without them the whole
# thing is skipped with a note, so everyone else's build stays green.
#
# The 4 MB PCM blob is embedded with .incbin, like the JV's. It needs a board
# with more than 4 MB of flash.

set(_m1_dir ${PICOFACE_CURRENT_INSTRUMENT_DIR})
set(_m1_roms ${_m1_dir}/roms)

set(_m1_have_roms TRUE)
foreach(_n IC36 IC37 IC38 IC39 IC40 IC41 IC42 IC43)
    if(NOT EXISTS ${_m1_roms}/${_n}.bin)
        set(_m1_have_roms FALSE)
    endif()
endforeach()
file(GLOB _m1_syx ${_m1_roms}/*.syx ${_m1_roms}/*.SYX)
if(NOT _m1_syx)
    set(_m1_have_roms FALSE)
endif()

if(NOT _m1_have_roms)
    message(STATUS
        "PicoFaceM1: skipped - no ROM set in ${_m1_roms} "
        "(needs IC36-IC43.bin, IC30.bin, the two Rev 19 OS halves and the "
        "factory preload M1preld.syx; see instruments/PicoFaceM1/README.md)")
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(_m1_gen ${CMAKE_CURRENT_BINARY_DIR}/PicoFaceM1_rom)
set(_m1_blob_s ${_m1_gen}/m1_blob.S)

# Run at configure time, not build time: the generated .S and the table header
# have to exist before the target is declared. Rerun when the converter changes,
# not only when the output is missing -- a stale table header once sent a stale
# image to a hardware test on the D5.
file(GLOB _m1_gen_scripts ${CMAKE_SOURCE_DIR}/tools/m1_extract/*.py)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_m1_gen_scripts})
if(EXISTS ${_m1_blob_s})
    foreach(_f ${_m1_gen_scripts} ${_m1_syx})
        if(${_f} IS_NEWER_THAN ${_m1_gen}/m1_rom_tables.h)
            file(REMOVE ${_m1_blob_s})
        endif()
    endforeach()
endif()
if(NOT EXISTS ${_m1_blob_s})
    message(STATUS "PicoFaceM1: converting ROM set (this takes a moment)")
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/m1_extract/m1_make_blob.py
                ${_m1_roms} ${_m1_gen}
        RESULT_VARIABLE _m1_rc
        OUTPUT_VARIABLE _m1_out
        ERROR_VARIABLE  _m1_err)
    if(NOT _m1_rc EQUAL 0)
        message(FATAL_ERROR "PicoFaceM1: ROM conversion failed\n${_m1_out}${_m1_err}")
    endif()
    message(STATUS "PicoFaceM1: ${_m1_out}")
endif()

picoface_add_instrument(
    NAME PicoFaceM1
    PROGRAM_NAME "PicoFaceM1"
    USB_PID 0x105A
    DIR ${_m1_dir}

    SOURCES
        src/M1_Instrument.cpp
        src/M1_Bridge.cpp
        src/M1_Controller.cpp
        src/M1_Midi.cpp
        src/M1_SysEx.cpp
        src/m1_memory.cpp
        src/m1_params.cpp
        src/m1_engine/m1_engine.cpp
        src/m1_engine/m1_fx.cpp
        ${_m1_blob_s}

    INCLUDE_DIRS
        include
        ${_m1_gen}          # m1_rom_tables.h, generated beside the blob

    # ---- Optional core modules -------------------------------------------
    CORE_MODULES
        ui_kit     # shared panel look: header, knobs, two-value body, lists
        ui_menu    # ListView for the menu and the edit-page list

)

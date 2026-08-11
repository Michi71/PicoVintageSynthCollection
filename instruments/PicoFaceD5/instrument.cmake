# PicoFaceD5 - Roland D-50, native LA engine over the original PCM data.
#
# Like PicoFaceJV this cannot be built from the repository alone: it needs the
# D-50's two PCM ROMs, which are not distributable. Put them in roms/
# (gitignored) and the instrument appears in the build; without them the whole
# thing is skipped with a note, so everyone else's build stays green.
#
# The files are identified by content, not by name: any dump of the pair works,
# including the 512 KB read-outs that contain each 256 KB chip twice.

set(_d5_dir ${PICOFACE_CURRENT_INSTRUMENT_DIR})
set(_d5_roms ${_d5_dir}/roms)

# The conversion tool reports what it found, so the gate here only has to be
# sure there is something to look at.
file(GLOB _d5_rom_files ${_d5_roms}/*.bin ${_d5_roms}/*.BIN)
if(NOT _d5_rom_files)
    message(STATUS
        "PicoFaceD5: skipped - no ROM images in ${_d5_roms} "
        "(needs the two PCM ROMs and a program EPROM; "
        "see instruments/PicoFaceD5/README.md)")
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(_d5_gen ${CMAKE_CURRENT_BINARY_DIR}/PicoFaceD5_rom)
set(_d5_blob_s ${_d5_gen}/d5_blob.S)

# Run at configure time, not build time: the generated .S and the table header
# have to exist before the target is declared, and the conversion is a one-off
# of a few seconds.
if(NOT EXISTS ${_d5_blob_s})
    message(STATUS "PicoFaceD5: converting ROM set (this takes a moment)")
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/d5_extract/d5_make_blob.py
                ${_d5_roms} ${_d5_gen}
        RESULT_VARIABLE _d5_rc
        OUTPUT_VARIABLE _d5_out
        ERROR_VARIABLE  _d5_err)
    if(NOT _d5_rc EQUAL 0)
        message(FATAL_ERROR "PicoFaceD5: ROM conversion failed\n${_d5_out}${_d5_err}")
    endif()
    message(STATUS "PicoFaceD5: ${_d5_out}")
endif()

# A patch bank is optional: with a .syx in roms/ the instrument plays those
# patches, without one it falls back to the hand-built presets in the source
# tree. Any D-50 bulk dump works -- the converter checks its checksums and
# parameter ranges and refuses anything that is not one.
file(GLOB _d5_syx ${_d5_roms}/*.syx ${_d5_roms}/*.SYX)
if(_d5_syx)
    list(GET _d5_syx 0 _d5_bank)
    if(NOT EXISTS ${_d5_gen}/d5_patch_data.h)
        message(STATUS "PicoFaceD5: converting patch bank")
        execute_process(
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tools/d5_extract/d5_syx_to_patches.py
                    ${_d5_bank} ${_d5_gen}
            RESULT_VARIABLE _d5_syx_rc
            OUTPUT_VARIABLE _d5_syx_out
            ERROR_VARIABLE  _d5_syx_err)
        if(NOT _d5_syx_rc EQUAL 0)
            message(FATAL_ERROR
                "PicoFaceD5: patch bank conversion failed\n${_d5_syx_out}${_d5_syx_err}")
        endif()
        message(STATUS "PicoFaceD5: ${_d5_syx_out}")
    endif()
endif()

picoface_add_instrument(
    NAME PicoFaceD5
    PROGRAM_NAME "PicoFaceD5"
    VERSION "0.1"
    USB_PID 0x1059
    DIR ${_d5_dir}

    SOURCES
        src/D5_Instrument.cpp
        src/D5_Bridge.cpp
        src/D5_Controller.cpp
        src/D5_Display.cpp
        src/D5_Midi.cpp
        ${_d5_blob_s}

    INCLUDE_DIRS
        include
        ${_d5_gen}          # d5_pcm_table.h, generated beside the blob
)

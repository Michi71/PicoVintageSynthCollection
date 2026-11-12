# PicoFaceCM - Roland CM-32L: mt32emu's synthesis core over the original ROMs,
# rendering on both cores.
#
# Like PicoFaceD5 this cannot be built from the repository alone: it needs the
# CM-32L's control ROM (v1.02) and PCM ROM, which are not distributable. Put
# them in roms/ (gitignored) and the instrument appears in the build; without
# them it is skipped with a note, so everyone else's build stays green.

set(_cm_dir ${PICOFACE_CURRENT_INSTRUMENT_DIR})
set(_cm_roms ${_cm_dir}/roms)

file(GLOB _cm_rom_files ${_cm_roms}/*)
if(NOT _cm_rom_files)
    message(STATUS
        "PicoFaceCM: skipped - no ROM images in ${_cm_roms} "
        "(needs the CM-32L control ROM v1.02 and PCM ROM; "
        "see instruments/PicoFaceCM/README.md)")
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(_cm_gen ${CMAKE_CURRENT_BINARY_DIR}/PicoFaceCM_rom)
set(_cm_blob_s ${_cm_gen}/cm_blob.S)
set(_cm_gen_script ${CMAKE_SOURCE_DIR}/tools/cm_extract/cm_make_blob.py)

# At configure time, as for the D5: the .S has to exist before the target is
# declared. Rerun when the script changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_cm_gen_script})
if(NOT EXISTS ${_cm_blob_s} OR ${_cm_gen_script} IS_NEWER_THAN ${_cm_blob_s})
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${_cm_gen_script} ${_cm_roms} ${_cm_gen}
        RESULT_VARIABLE _cm_rc
        OUTPUT_VARIABLE _cm_out
        ERROR_VARIABLE  _cm_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT _cm_rc EQUAL 0)
        # Files that are not the right ROM set are no reason to break the build
        # of the other instruments.
        message(STATUS "PicoFaceCM: skipped - ${_cm_err}")
        return()
    endif()
    message(STATUS "PicoFaceCM: ${_cm_out}")
endif()

# The engine's lookup tables are computed, not committed: three of the six
# coincide with tables in the control ROM, and only generating rules belong in
# the repository. The script stops if its result is not upstream mt32emu's.
set(_cm_tables_script ${CMAKE_SOURCE_DIR}/tools/cm_extract/cm_make_tables.py)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_cm_tables_script})
if(NOT EXISTS ${_cm_gen}/TablesData.inc OR
   ${_cm_tables_script} IS_NEWER_THAN ${_cm_gen}/TablesData.inc)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${_cm_tables_script} ${_cm_gen}/TablesData.inc
        RESULT_VARIABLE _cm_rc
        ERROR_VARIABLE  _cm_err
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT _cm_rc EQUAL 0)
        message(STATUS "PicoFaceCM: skipped - ${_cm_err}")
        return()
    endif()
endif()

# The engine: mt32emu's synthesis core, see mt32emu/README.md for what was
# left out and what was changed. MidiStreamParser is not among the sources -
# the core parses MIDI.
# HotPath.cpp is the per-sample path as one translation unit: it includes
# LA32Ramp, LA32WaveGenerator, TVA, TVF, TVP and Partial, which are therefore
# not listed here.
set(_cm_engine
    Analog.cpp BReverbModel.cpp Display.cpp File.cpp HotPath.cpp
    LA32FloatWaveGenerator.cpp
    Part.cpp PartialManager.cpp Poly.cpp ROMInfo.cpp
    Synth.cpp Tables.cpp sha1/sha1.cpp)
list(TRANSFORM _cm_engine PREPEND mt32emu/)

picoface_add_instrument(
    NAME PicoFaceCM
    PROGRAM_NAME "PicoFaceCM"
    USB_PID 0x105C
    DIR ${_cm_dir}

    SOURCES
        src/CM_Instrument.cpp
        src/CM_Controller.cpp
        ${_cm_engine}
        ${_cm_blob_s}

    INCLUDE_DIRS
        include
        mt32emu
        ${_cm_gen}          # TablesData.inc, generated

    DEFINES
        # The renderer keeps six buffers of this length; the core asks for
        # far shorter blocks than upstream's 4096.
        MT32EMU_MAX_SAMPLES_PER_RUN=256
        # core1 runs half of the engine; give both cores a whole scratch bank.
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x1000

    CORE_MODULES
        ui_kit     # shared panel look: header, two-value body, name page, diagnostics
)

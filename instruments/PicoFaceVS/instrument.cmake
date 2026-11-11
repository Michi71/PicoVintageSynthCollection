# PicoFaceVS - Sequential Prophet VS (vector synthesis), native engine.
#
# Builds from the repository alone: the waves are recomputed from a harmonic
# table (tools/vs_data/vs_wave_harmonics.txt) and the firmware's lookup tables
# from their generating rules. Files of your own in roms/ (gitignored) are
# picked up when they are there and refine the build:
#
#   Prophet VS OS r1.2 ROM image      waves and tables exactly as the original
#   Behringer Pro VS Mini firmware    waves 0-31, ADSR times, chord mode
#   .syx files                        program banks, embedded into the image
#
# The generators need Python 3 with numpy. Without it the instrument is
# skipped with a note, so everyone else's build stays green.

set(_vs_dir ${PICOFACE_CURRENT_INSTRUMENT_DIR})
set(_vs_roms ${_vs_dir}/roms)
set(_vs_tools ${CMAKE_SOURCE_DIR}/tools/vs_data)
set(_vs_gen ${CMAKE_CURRENT_BINARY_DIR}/PicoFaceVS_gen)
set(_vs_hdr ${_vs_gen}/provs/gen)
set(_vs_stamp ${_vs_gen}/vs_banks.h)

# python3 from PATH, the one a manual call would use; find_package(Python3)
# may pick an interpreter without numpy.
find_program(PICOFACE_VS_PYTHON NAMES python3 python)
if(NOT PICOFACE_VS_PYTHON)
    message(STATUS "PicoFaceVS: skipped - no python3 found")
    return()
endif()

# Run at configure time: the headers have to exist before the target is
# declared. Rerun when a generator or anything in roms/ changes.
file(GLOB _vs_scripts ${_vs_tools}/*.py ${_vs_tools}/*.txt)
file(GLOB _vs_inputs ${_vs_roms}/*)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_vs_scripts} ${_vs_inputs})
if(EXISTS ${_vs_stamp})
    foreach(_f ${_vs_scripts} ${_vs_inputs})
        if(${_f} IS_NEWER_THAN ${_vs_stamp})
            file(REMOVE ${_vs_stamp})
        endif()
    endforeach()
endif()
if(NOT EXISTS ${_vs_stamp})
    file(REMOVE_RECURSE ${_vs_hdr})
    execute_process(
        COMMAND ${PICOFACE_VS_PYTHON} ${_vs_tools}/gen_data.py --out ${_vs_hdr} --roms ${_vs_roms}
        RESULT_VARIABLE _vs_rc OUTPUT_VARIABLE _vs_out ERROR_VARIABLE _vs_err)
    if(NOT _vs_rc EQUAL 0)
        message(STATUS "PicoFaceVS: skipped - data generation failed "
                       "(needs Python 3 with numpy)\n${_vs_out}${_vs_err}")
        return()
    endif()
    message(STATUS "PicoFaceVS: ${_vs_out}")
    # Behringer parts: only from roms/, never searched for elsewhere here.
    execute_process(
        COMMAND ${PICOFACE_VS_PYTHON} ${_vs_tools}/gen_pvm.py --out ${_vs_hdr} --roms ${_vs_roms}
                --no-auto --search ${_vs_roms}
        RESULT_VARIABLE _vs_rc OUTPUT_VARIABLE _vs_out ERROR_VARIABLE _vs_err)
    if(NOT _vs_rc EQUAL 0)
        message(FATAL_ERROR "PicoFaceVS: gen_pvm.py failed\n${_vs_out}${_vs_err}")
    endif()
    message(STATUS "PicoFaceVS: ${_vs_out}")
    execute_process(
        COMMAND ${PICOFACE_VS_PYTHON} ${_vs_tools}/embed_banks.py ${_vs_roms} ${_vs_stamp}
        RESULT_VARIABLE _vs_rc OUTPUT_VARIABLE _vs_out ERROR_VARIABLE _vs_err)
    if(NOT _vs_rc EQUAL 0)
        message(FATAL_ERROR "PicoFaceVS: embed_banks.py failed\n${_vs_out}${_vs_err}")
    endif()
    message(STATUS "PicoFaceVS: ${_vs_out}")
endif()

picoface_add_instrument(
    NAME PicoFaceVS
    PROGRAM_NAME "PicoFaceVS"
    USB_PID 0x105B
    DIR ${_vs_dir}

    SOURCES
        src/VS_Instrument.cpp
        src/provs_engine.cpp

    INCLUDE_DIRS
        include
        ${_vs_gen}          # provs/gen/*.h and vs_banks.h, generated

    CORE_MODULES
        ui_kit     # shared panel look
)

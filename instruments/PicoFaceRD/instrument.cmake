# PicoFaceRD - Roland RD / MKS-20 sample-based piano emulation.
#
# Like PicoFaceJV and PicoFaceD5 this cannot be built from the repository
# alone. It needs a local MKS-20 / MK-80 ROM set and the descriptor packs
# captured from it; neither is distributable, both live in roms/ (gitignored),
# and without them the instrument removes itself from the build so everyone
# else's stays green.
#
# What used to be here instead was 44 MB of generated C: three decoded ROM sets
# and the packs, as initialiser lists. They are now a pair of blobs built at
# configure time and pulled in with .incbin -- the same arrangement the JV uses,
# and for the same two reasons.
#
# pico_hw.cpp and veeprom.cpp diverged from the shared core versions in the
# original repository and are therefore replaced locally here; the
# consolidation is noted in docs/ARCHITECTURE.md.

set(_rd_dir ${PICOFACE_CURRENT_INSTRUMENT_DIR})
set(_rd_roms ${_rd_dir}/roms)

# The three sample chips of each model, and the sixteen packs.
set(_rd_required
    ${_rd_roms}/mks20_15179736.BIN ${_rd_roms}/mks20_15179737.BIN
    ${_rd_roms}/mks20_15179738.BIN ${_rd_roms}/mks20_15179739.BIN
    ${_rd_roms}/mks20_15179740.BIN ${_rd_roms}/mks20_15179741.BIN
    ${_rd_roms}/MK80_IC5.bin ${_rd_roms}/MK80_IC6.bin ${_rd_roms}/MK80_IC7.bin)
foreach(_i RANGE 15)
    list(APPEND _rd_required ${_rd_roms}/pack_p${_i}.rdp)
endforeach()

set(_rd_missing "")
foreach(_f IN LISTS _rd_required)
    if(NOT EXISTS ${_f})
        get_filename_component(_n ${_f} NAME)
        list(APPEND _rd_missing ${_n})
    endif()
endforeach()
if(_rd_missing)
    list(LENGTH _rd_missing _rd_n)
    message(STATUS
        "PicoFaceRD: skipped - ${_rd_n} file(s) missing from ${_rd_roms} "
        "(needs the nine sample ROMs and the sixteen .rdp packs; "
        "see instruments/PicoFaceRD/README.md)")
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(_rd_gen ${CMAKE_CURRENT_BINARY_DIR}/PicoFaceRD_rom)
set(_rd_rom_blob ${_rd_gen}/rd_rom.blob)
set(_rd_rom_s    ${_rd_gen}/rd_rom_blob.S)
set(_rd_tbl_s    ${_rd_gen}/rd_rom_tables.S)
set(_rd_packs_s  ${_rd_gen}/rd_packs_blob.S)
set(_rd_packs_c  ${_rd_gen}/rd_packs_tables.cpp)

# Configure time, not build time: the generated .S files have to exist before
# the target is declared. Both steps are seconds and rerun when their inputs
# move -- a build that silently kept the previous blob would send a stale image
# to a hardware test, which is the mistake the D5 already made once.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_rd_required})

# The ROM blob needs the reference emulator, which is where the descrambling
# lives and which is not vendored here. RDPIANO points at a checkout; see
# instruments/PicoFaceRD/README.md.
if(NOT EXISTS ${_rd_rom_s})
    if(NOT DEFINED ENV{RDPIANO} AND NOT DEFINED RDPIANO)
        message(FATAL_ERROR
            "PicoFaceRD: the ROM blob has to be built once, and that needs the "
            "reference emulator. Set RDPIANO to a checkout of "
            "https://github.com/Michi71/rdpiano and configure again.")
    endif()
    if(NOT DEFINED RDPIANO)
        set(RDPIANO $ENV{RDPIANO})
    endif()
    file(MAKE_DIRECTORY ${_rd_gen})
    message(STATUS "PicoFaceRD: building the ROM blob (this takes a moment)")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env RDPIANO=${RDPIANO}
                ${CMAKE_SOURCE_DIR}/tools/rd_extract/rd_make_rom.sh
                ${_rd_roms} ${_rd_rom_blob}
        RESULT_VARIABLE _rd_rc OUTPUT_VARIABLE _rd_out ERROR_VARIABLE _rd_err)
    if(NOT _rd_rc EQUAL 0)
        message(FATAL_ERROR "PicoFaceRD: ROM conversion failed\n${_rd_out}${_rd_err}")
    endif()
    message(STATUS "PicoFaceRD: ${_rd_out}")
endif()

# The packs need nothing but Python.
execute_process(
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/rd_extract/rd_embed_packs.py
            ${_rd_roms} ${_rd_gen}
    RESULT_VARIABLE _rd_pk_rc OUTPUT_VARIABLE _rd_pk_out ERROR_VARIABLE _rd_pk_err)
if(NOT _rd_pk_rc EQUAL 0)
    message(FATAL_ERROR "PicoFaceRD: pack embedding failed\n${_rd_pk_out}${_rd_pk_err}")
endif()
message(STATUS "PicoFaceRD: ${_rd_pk_out}")

picoface_add_instrument(
    NAME PicoFaceRD
    PROGRAM_NAME "PicoFaceRD"
    USB_PID 0x1052
    DIR ${_rd_dir}

    SOURCES
        # adapter
        src/RD_Instrument.cpp

        # instrument logic
        src/RD_Controller.cpp
        src/RD_Display.cpp
        src/RD_Midi.cpp
        src/RD_Synth_Bridge_v2.cpp
        src/rd_effects.cpp

        # replaced core sources
        src/veeprom.cpp

        # libstdc++ exception stubs; only RD pulls in std::vector
        src/rd_cxx_stubs.cpp

        # engine
        src/rd_engine/rd_new_engine.cpp

        # generated beside the build: sample banks, chip tables, packs
        ${_rd_rom_s}
        ${_rd_tbl_s}
        ${_rd_packs_s}
        ${_rd_packs_c}

    INCLUDE_DIRS
        include
        effects
        # The engine sources include their own headers flat ("rom_tables.h",
        # not "rd_engine/rom_tables.h"), so this directory has to be on the
        # search path as well.
        include/rd_engine

    CORE_EXCLUDE
        veeprom.cpp

    DEFINES
    # RD is the only instrument that does not run at 444 MHz. Both values
    # below must move together; see core/src/pico_hw.cpp.
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x1000
        TARGET_RP2350=1
        RD_CLOCK_504=1
        PICOFACE_SYS_CLOCK_HZ=480000000
        PICOFACE_QMI_M0_TIMING_TARGET=PICOFACE_QMI_M0_TIMING_RD
)

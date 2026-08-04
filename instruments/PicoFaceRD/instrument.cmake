# PicoFaceRD - Roland RD / MKS-20 sample-based piano emulation.
#
# pico_hw.cpp and veeprom.cpp diverged from the shared core versions in
# the original repository and are therefore replaced locally here; the
# consolidation is noted in docs/ARCHITECTURE.md.
#
# src/rd_engine contains large generated sample and table data.

picoface_add_instrument(
    NAME PicoFaceRD
    PROGRAM_NAME "PicoFaceRD"
    VERSION "0.1"
    USB_PID 0x1052
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}

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
        src/pico_hw.cpp
        src/veeprom.cpp

        # engine
        src/rd_engine/mcu.cpp
        src/rd_engine/mk80_tables.cpp
        src/rd_engine/mks20a_tables.cpp
        src/rd_engine/mks20b_tables.cpp
        src/rd_engine/program_tables.cpp
        src/rd_engine/rd_new_engine.cpp
        src/rd_engine/rd_packs_data.cpp
        src/rd_engine/rd_samples_ilv_a.cpp
        src/rd_engine/rd_samples_ilv_b.cpp
        src/rd_engine/rd_samples_ilv_m.cpp
        src/rd_engine/rd_samples_pk4_a.cpp
        src/rd_engine/rd_samples_pk4_b.cpp
        src/rd_engine/rd_samples_pk4_m.cpp
        src/rd_engine/sound_chip.cpp

    INCLUDE_DIRS
        include
        effects
        # The engine sources include their own headers flat ("rom_tables.h",
        # not "rd_engine/rom_tables.h"), so this directory has to be on the
        # search path as well.
        include/rd_engine

    CORE_EXCLUDE
        pico_hw.cpp
        veeprom.cpp
)

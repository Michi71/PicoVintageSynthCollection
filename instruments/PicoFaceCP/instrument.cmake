# Yamaha reface CP electric piano emulation, based on the mdaEPiano engine.
#
# pico_hw.cpp is replaced by an instrument-specific variant: in the original
# PicoFaceCP repository this file diverged from the version shared by the other
# instruments. It is excluded from the core via CORE_EXCLUDE and the local
# drop-in in src/ is compiled instead. Converging it back onto the core version
# is tracked in docs/ARCHITECTURE.md.
picoface_add_instrument(
    NAME PicoFaceCP
    PROGRAM_NAME "PicoFaceCP"
    VERSION "0.1"
    USB_PID 0x1051
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}

    # CP_Instrument.cpp: adapter implementing picoface::Instrument
    SOURCES
        src/CP_Instrument.cpp
        src/mdaEPiano.cpp
        src/presets.cpp
        src/pico_program_select.cpp
        src/pico_hw.cpp

    INCLUDE_DIRS
        include
        effects

    CORE_MODULES
        ui_panel
        midi_reface

    # Core source superseded by the local variant in src/
    CORE_EXCLUDE
        pico_hw.cpp
)

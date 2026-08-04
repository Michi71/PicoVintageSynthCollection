# Yamaha reface CP electric piano emulation, based on the mdaEPiano engine.
#
# This instrument replaces no core source at all. midi_reface and the settings
# layer used to live in the core as optional modules, but PicoFaceCP was their
# only user - they moved here with the switch to the standard runtime model,
# and with them the last CP-specific types out of the core headers.
picoface_add_instrument(
    NAME PicoFaceCP
    PROGRAM_NAME "PicoFaceCP"
    VERSION "0.1"
    USB_PID 0x1051
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}

    # CP_Instrument.cpp: adapter implementing picoface::Instrument
    SOURCES
        src/CP_Instrument.cpp
        src/CP_Ui.cpp
        src/mdaEPiano.cpp
        src/presets.cpp
        src/midi_reface.cpp
        src/settings.cpp

    INCLUDE_DIRS
        include
        effects

    # Non-blocking selection list for the menu tree in CP_Ui.cpp.
    CORE_MODULES
        ui_menu

    DEFINES
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x1000
)

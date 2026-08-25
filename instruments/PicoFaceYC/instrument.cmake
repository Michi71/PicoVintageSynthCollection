# Yamaha reface YC drawbar organ emulation.
#
# The drawbar engine (yc_engine) is header-only under include/yc_engine, so it
# needs no SOURCES entry -- the "include" dir below suffices.
#
# midi_input_usb.cpp is replaced by an instrument-specific variant: in the
# original PicoFaceYC repository it diverged from the version shared by the
# other instruments (note-on with velocity 0 is turned into a note-off there).
# It is excluded from the core via CORE_EXCLUDE and the local drop-in in src/
# is compiled instead. Converging it back onto the core version is tracked in
# docs/ARCHITECTURE.md.
#
# settings.cpp and midi_reface.cpp in src/ carry the same names as core
# sources but no longer replace them: since the move to the standard runtime
# model this instrument requests neither the ui_panel nor the midi_reface
# module, so there is nothing to exclude.
picoface_add_instrument(
    NAME PicoFaceYC
    PROGRAM_NAME "PicoFaceYC"
    USB_PID 0x1050
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}

    # YC_Instrument.cpp: adapter implementing picoface::Instrument
    SOURCES
        src/YC_Instrument.cpp
        src/YC_Controller.cpp
        src/YC_GUI.cpp
        src/YC_Synth_Bridge.cpp
        src/YC_Ui.cpp
        src/midi_input_usb.cpp
        src/settings.cpp
        src/midi_reface.cpp

    INCLUDE_DIRS
        include
        effects

    # Non-blocking selection list for the menu tree in YC_Ui.cpp.
    CORE_MODULES
        ui_menu

    CORE_EXCLUDE
        midi_input_usb.cpp

    DEFINES
        PICO_USE_SW_SPIN_LOCKS=1
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x1000
)

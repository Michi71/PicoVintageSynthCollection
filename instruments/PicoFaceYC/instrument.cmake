# Yamaha reface YC drawbar organ emulation.
#
# The drawbar engine (yc_engine) is header-only under include/yc_engine, so it
# needs no SOURCES entry -- the "include" dir below suffices.
#
# midi_reface.cpp in src/ is the YC's part of the reface MIDI layer on top
# of the core's reface module (picoface::RefaceMidiBase); settings.cpp is its
# record layout over the core's settings autosave. Neither replaces a core
# source. The instrument's own midi_input_usb.cpp - one line different from
# the core's, a note-on with velocity 0 turned into a note-off - is gone: the
# shared layer does that for every reface port.
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
        src/settings.cpp
        src/midi_reface.cpp

    INCLUDE_DIRS
        include
        effects

    # Non-blocking selection list for the menu tree in YC_Ui.cpp.
    CORE_MODULES
        ui_menu
        ui_kit     # shared panel look; ui_menu draws through it
        reface     # the reface MIDI dialect; src/midi_reface.cpp is the YC on top of it

    DEFINES
        PICO_USE_SW_SPIN_LOCKS=1
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x1000
)

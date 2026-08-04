# Yamaha reface DX FM synthesizer emulation.
#
# The FM engine (dx_engine) is header-only under include/dx_engine, so it needs
# no SOURCES entry -- the "include" dir below suffices. effects/ holds ram_hot.h,
# the RAM_HOT macro DX_Synth_Bridge.cpp puts on its render path.
#
# Like PicoFaceYC and PicoFaceCP this instrument replaces no core source:
# midi_reface.cpp, settings.cpp and presets.cpp carry names of their own and the
# reface MIDI layer is per-instrument (the DX one speaks the DX SysEx address
# map, which YC's and CP's do not).
picoface_add_instrument(
    NAME PicoFaceDX
    PROGRAM_NAME "PicoFaceDX"
    VERSION "0.1"
    USB_PID 0x1057
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}

    # DX_Instrument.cpp: adapter implementing picoface::Instrument
    SOURCES
        src/DX_Instrument.cpp
        src/DX_Controller.cpp
        src/DX_GUI.cpp
        src/DX_Synth_Bridge.cpp
        src/DX_Ui.cpp
        src/midi_reface.cpp
        src/presets.cpp
        src/settings.cpp

    INCLUDE_DIRS
        include
        effects

    # Non-blocking selection list for the menu tree in DX_Ui.cpp.
    CORE_MODULES
        ui_menu

    DEFINES
        # Both stacks into the otherwise unused halves of the 4 KB scratch
        # banks, as in the original repository. PICO_CORE1_STACK_SIZE buys
        # nothing here now that core1 is never started - same as for YC and CP,
        # and left alone for the same reason: changing a stack layout without a
        # device to test it on is not worth the 2 KB of scratch_x it costs.
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x1000
)

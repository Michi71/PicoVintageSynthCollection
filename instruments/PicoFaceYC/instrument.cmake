# Yamaha reface YC drawbar organ emulation.
#
# The drawbar engine (yc_engine) is header-only under include/yc_engine, so it
# needs no SOURCES entry -- the "include" dir below suffices.
#
# pico_hw.cpp and midi_input_usb.cpp are replaced by instrument-specific
# variants: in the original PicoFaceYC repository both files diverged from the
# versions shared by the other instruments. They are excluded from the core via
# CORE_EXCLUDE and the local drop-ins in src/ are compiled instead. Converging
# them back onto the core versions is tracked in docs/ARCHITECTURE.md.
picoface_add_instrument(
    NAME PicoFaceYC
    PROGRAM_NAME "PicoFaceYC"
    VERSION "0.1"
    USB_PID 0x1050
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}

    # YC_Instrument.cpp: adapter implementing picoface::Instrument
    SOURCES
        src/YC_Instrument.cpp
        src/YC_Controller.cpp
        src/YC_GUI.cpp
        src/YC_Synth_Bridge.cpp
        src/midi_input_usb.cpp
        src/pico_frontpanel.cpp
        src/settings.cpp
        src/midi_reface.cpp

    INCLUDE_DIRS
        include
        effects

    # YC still uses the shared panel/selection widgets from ui_panel
    # (pico_selection_list.cpp, pico_input_value.cpp).
    CORE_MODULES
        ui_panel
        midi_reface

    # Core and module sources superseded by local variants in src/.
    # pico_frontpanel.cpp, settings.cpp and midi_reface.cpp come from the
    # requested modules; without excluding them here the link would fail with
    # duplicate symbols.
    CORE_EXCLUDE
        midi_input_usb.cpp
        pico_frontpanel.cpp
        settings.cpp
        midi_reface.cpp

    DEFINES
        PICO_USE_SW_SPIN_LOCKS=1
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x1000
)

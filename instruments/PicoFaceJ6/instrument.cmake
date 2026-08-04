# Roland Juno-6 emulation.

picoface_add_instrument(
    NAME PicoFaceJ6
    PROGRAM_NAME "PicoFaceJ6"
    VERSION "0.1"
    USB_PID 0x1053
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}
    SOURCES
        # Adapter implementing picoface::Instrument
        src/J6_Instrument.cpp
        src/J6_Controller.cpp
        src/J6_Display.cpp
        src/J6_Midi.cpp
        src/J6_Synth_Bridge.cpp
        src/j6_patchstore.cpp
        src/juno/juno.cpp
        src/juno/juno_fx.cpp
        src/juno/juno_params.cpp
        src/juno/juno_presets.cpp
    INCLUDE_DIRS include

    DEFINES
        PICO_USE_SW_SPIN_LOCKS=1
        TARGET_RP2350=1
)

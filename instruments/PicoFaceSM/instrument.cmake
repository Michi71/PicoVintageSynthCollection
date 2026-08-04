# ARP/Eminent Solina String Ensemble emulation.

picoface_add_instrument(
    NAME PicoFaceSM
    PROGRAM_NAME "PicoFaceSM"
    VERSION "0.1"
    USB_PID 0x1055
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}
    SOURCES
        # Adapter implementing picoface::Instrument
        src/SM_Instrument.cpp
        src/SM_Controller.cpp
        src/SM_Display.cpp
        src/SM_Midi.cpp
        src/SM_Synth_Bridge.cpp
        src/solina/solina.cpp
        src/solina/solina_ensemble.cpp
        src/solina/solina_keyboard.cpp
        src/solina/solina_phaser.cpp
        src/solina/solina_registers.cpp
    INCLUDE_DIRS include

    DEFINES
        PICO_USE_SW_SPIN_LOCKS=1
        TARGET_RP2350=1
)

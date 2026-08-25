# ARP/Eminent Solina String Ensemble emulation.

picoface_add_instrument(
    NAME PicoFaceSM
    PROGRAM_NAME "PicoFaceSM"
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

    # No double-tap RESET: on the speaker-driven prototype the inrush current
    # browns the chip out on plug-in and the library reads that as a double
    # tap, leaving the device in BOOTSEL. Matches the standalone repository,
    # where pico_bootsel_via_double_reset was deliberately not linked. The
    # BOOTSEL button keeps working.
    NO_DOUBLE_RESET

    DEFINES
        PICO_USE_SW_SPIN_LOCKS=1
        TARGET_RP2350=1
)

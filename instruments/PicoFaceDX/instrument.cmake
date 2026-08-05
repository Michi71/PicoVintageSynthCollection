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
# Optional: enumerate as a real reface DX over USB instead of as PicoFaceDX.
# Editors such as Soundmondo appear to filter MIDI ports by USB descriptor
# rather than by the SysEx Identity Reply - the ESP32 reference
# (copych/RDX-Reface-DX-emu, setupMidi() in RDX/RDX_Midi.h) overrides vendor ID,
# product ID and both strings, which would be pointless otherwise.
#
# Off by default, and deliberately so: it borrows Yamaha's vendor ID, and it
# gives up the per-instrument PID that lets a host tell the eight images of this
# collection apart. Turn it on only for a session with such an editor:
#
#   cmake -S . -B build -G Ninja -DPICOFACE_DX_REFACE_USB_IDENTITY=ON
#
option(PICOFACE_DX_REFACE_USB_IDENTITY
       "PicoFaceDX: enumerate as a Yamaha reface DX over USB (for editors that filter by USB descriptor)"
       OFF)

set(_dx_usb_pid 0x1057)
set(_dx_usb_defines "")
if(PICOFACE_DX_REFACE_USB_IDENTITY)
    # The values the ESP32 reference uses. Its own comment calls the PID a
    # guess: the reface range is most likely 0x1622..0x1625, and 0x1624 was
    # picked because the DX is 0x53 within the identity range 0x51..0x54.
    set(_dx_usb_pid 0x1624)
    list(APPEND _dx_usb_defines
        PICOFACE_USB_VID=0x0499
        "PICOFACE_USB_MANUFACTURER=\"Yamaha Corp.\""
        "PICOFACE_USB_PRODUCT=\"reface DX\""
    )
    message(STATUS "PicoFaceDX: USB identity overridden to Yamaha reface DX "
                   "(VID 0x0499, PID ${_dx_usb_pid})")
endif()

picoface_add_instrument(
    NAME PicoFaceDX
    PROGRAM_NAME "PicoFaceDX"
    VERSION "0.1"
    USB_PID ${_dx_usb_pid}
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

        # Empty unless PICOFACE_DX_REFACE_USB_IDENTITY is on, see above.
        ${_dx_usb_defines}
)

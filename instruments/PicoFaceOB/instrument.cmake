# Oberheim OB-X emulation, engine ported from OB-Xf
# (https://github.com/surge-synthesizer/OB-Xf).
#
# LICENSING: unlike the rest of this repository, which is MIT, PicoFaceOB is
# GPL-3.0-or-later, because the OB-Xf engine is. The binary PicoFaceOB.uf2 is
# therefore a GPL-3 work; the core and the other six instruments are not
# affected (MIT is GPL compatible, so the combination is fine). See
# instruments/PicoFaceOB/LICENSE and README.md.
#
# The engine under include/obxf is upstream code with the original copyright
# headers plus a note per file listing what the port changed. It replaces no
# core source.
picoface_add_instrument(
    NAME PicoFaceOB
    PROGRAM_NAME "PicoFaceOB"
    USB_PID 0x1056
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}

    # OB_Instrument.cpp: adapter implementing picoface::Instrument
    SOURCES
        src/OB_Instrument.cpp
        src/OB_Engine.cpp
        src/OB_Ui.cpp

    INCLUDE_DIRS
        include

    # Non-blocking selection list for the menu tree in OB_Ui.cpp.
    CORE_MODULES
        ui_menu

    DEFINES
        PICO_USE_SW_SPIN_LOCKS=1
)

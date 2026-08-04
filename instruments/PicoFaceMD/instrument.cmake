# instruments/PicoFaceMD/instrument.cmake
#
# PicoFaceMD - Minimoog Model D emulation for the PicoFace platform.
# Docks onto the shared PicoFace core via the picoface::Instrument interface.
#
# This file is the REFERENCE registration - all other instruments orient
# themselves on this structure. It is loaded from the root CMakeLists.txt
# via include(); beforehand the root sets PICOFACE_CURRENT_INSTRUMENT_DIR
# to the absolute directory of this instrument.

picoface_add_instrument(
    # ---- Identity -------------------------------------------------------
    NAME PicoFaceMD             # unique instrument ID (targets, build folders)
    PROGRAM_NAME "PicoFaceMD"   # program string shown on the display
    VERSION "0.1"               # instrument firmware version string
    USB_PID 0x1054              # USB Product ID (must be unique per instrument!)

    # ---- Location -------------------------------------------------------
    DIR ${PICOFACE_CURRENT_INSTRUMENT_DIR}  # absolute instrument dir, set by root

    # ---- Sources (all paths relative to DIR) -----------------------------
    SOURCES
        src/MD_Instrument.cpp    # adapter implementing picoface::Instrument
        src/MD_Controller.cpp
        src/MD_Display.cpp
        src/MD_Midi.cpp
        src/MD_Synth_Bridge.cpp
        src/moog/moog.cpp
        src/moog/moog_fx.cpp
        src/moog/moog_params.cpp
        src/moog/moog_presets.cpp
        src/moog/moog_voice.cpp

    # ---- Private include directories (relative to DIR) -------------------
    INCLUDE_DIRS include

    # ---- Optional core modules -------------------------------------------
    # Names of optional core modules to additionally compile in.
    CORE_MODULES   # MD uses no optional core modules

    # ---- Excluded core sources -------------------------------------------
    # Here an instrument enters the filename of a core source that it
    # replaces with its own variant in src/ (e.g. pico_hw.cpp).
    CORE_EXCLUDE   # MD uses the base core sources unmodified
)

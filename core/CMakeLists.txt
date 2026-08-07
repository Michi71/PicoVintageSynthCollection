# ============================================================================
# PicoFace core - source list & variable definitions (no targets!)
#
# This file deliberately exports the core code as plain VARIABLES (source
# lists) instead of building a STATIC library:
#
#   * Core sources (picoface_main.cpp, pico_hw.cpp, ...) #include
#     "project_config.h", which each instrument provides in its own
#     instrument/include directory. A prebuilt static library could not be
#     recompiled against a different project_config.h per instrument, so the
#     core is re-translated from source for every instrument target.
#   * Because of that, every instrument target MUST place its own
#     "instrument/include" directory BEFORE "core/include" in the include
#     search order, so the instrument-specific project_config.h wins.
#
# Usage: add_subdirectory(core) from the root CMakeLists.txt, then use the
# variables below (exported to PARENT_SCOPE) via picoface_add_instrument().
#
# Exported variables:
#   PICOFACE_CORE_INCLUDE_DIR                - core include directory
#   PICOFACE_CORE_SOURCES                    - base core sources (absolute)
#   PICOFACE_CORE_MODULE_UI_MENU_SOURCES     - module "ui_menu" sources
#   PICOFACE_CORE_MODULES                    - list of available module names
# ============================================================================

# --- include directory -------------------------------------------------------
set(PICOFACE_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# --- base sources ------------------------------------------------------------
set(PICOFACE_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/get_serial.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/usb_descriptors.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pico_hw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/veeprom.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midi_input_usb.cpp"
    # Buffered USB MIDI transmit. Base source, not a module: writing straight to
    # TinyUSB silently truncates anything past one TX FIFO (see the header).
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midi_output_usb.cpp"
    # DIN MIDI on uart1. Base source, not an optional module: the hardware is
    # on every board and an idle RX interrupt costs nothing.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midi_serial.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/picoface_main.cpp"
    # display.cpp lives under src/ui/ next to the optional ui_menu module, but
    # it is a BASE source: picoface_main.cpp always instantiates the facade.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/display.cpp"
)

# --- module: ui_menu ----------------------------------------------------------
# Non-blocking UI widgets for instruments that draw from uiTick().
#
# Successor of the module ui_panel, which held the blocking widgets that owned
# the encoders in their own loop (pico_frontpanel.cpp, pico_selection_list.cpp,
# pico_input_value.cpp) plus the PicoFaceCP settings layer. With PicoFaceCP on
# the standard runtime model that module had no users left; its CP-specific
# parts moved to instruments/PicoFaceCP, the rest is gone.
set(PICOFACE_CORE_MODULE_UI_MENU_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/list_view.cpp"
)

# --- available modules --------------------------------------------------------
set(PICOFACE_CORE_MODULES "ui_menu")

# --- sanity check: every listed source file must exist ------------------------
foreach(_picoface_src IN LISTS
        PICOFACE_CORE_SOURCES
        PICOFACE_CORE_MODULE_UI_MENU_SOURCES)
    if(NOT EXISTS "${_picoface_src}")
        message(FATAL_ERROR "PicoFace core source missing: ${_picoface_src}")
    endif()
endforeach()

# --- export everything to the including (parent) scope ------------------------
set(PICOFACE_CORE_INCLUDE_DIR                "${PICOFACE_CORE_INCLUDE_DIR}"                PARENT_SCOPE)
set(PICOFACE_CORE_SOURCES                    "${PICOFACE_CORE_SOURCES}"                    PARENT_SCOPE)
set(PICOFACE_CORE_MODULE_UI_MENU_SOURCES     "${PICOFACE_CORE_MODULE_UI_MENU_SOURCES}"     PARENT_SCOPE)
set(PICOFACE_CORE_MODULES                    "${PICOFACE_CORE_MODULES}"                    PARENT_SCOPE)

# --- summary ------------------------------------------------------------------
list(LENGTH PICOFACE_CORE_SOURCES _picoface_num_core_sources)
list(LENGTH PICOFACE_CORE_MODULES _picoface_num_modules)
message(STATUS "PicoFace core: ${_picoface_num_core_sources} base sources, "
               "${_picoface_num_modules} modules (${PICOFACE_CORE_MODULES})")

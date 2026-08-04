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
#   PICOFACE_CORE_MODULE_UI_PANEL_SOURCES    - module "ui_panel" sources
#   PICOFACE_CORE_MODULE_MIDI_REFACE_SOURCES - module "midi_reface" sources
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/picoface_main.cpp"
    # display.cpp lives under src/ui/ next to the optional ui_panel module, but
    # it is a BASE source: picoface_main.cpp always instantiates the facade.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/display.cpp"
)

# --- module: ui_panel ---------------------------------------------------------
set(PICOFACE_CORE_MODULE_UI_PANEL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pico_frontpanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pico_selection_list.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pico_input_value.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/settings.cpp"
)

# --- module: midi_reface ------------------------------------------------------
set(PICOFACE_CORE_MODULE_MIDI_REFACE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midi/midi_reface.cpp"
)

# --- available modules --------------------------------------------------------
set(PICOFACE_CORE_MODULES "ui_panel;midi_reface")

# --- sanity check: every listed source file must exist ------------------------
foreach(_picoface_src IN LISTS
        PICOFACE_CORE_SOURCES
        PICOFACE_CORE_MODULE_UI_PANEL_SOURCES
        PICOFACE_CORE_MODULE_MIDI_REFACE_SOURCES)
    if(NOT EXISTS "${_picoface_src}")
        message(FATAL_ERROR "PicoFace core source missing: ${_picoface_src}")
    endif()
endforeach()

# --- export everything to the including (parent) scope ------------------------
set(PICOFACE_CORE_INCLUDE_DIR                "${PICOFACE_CORE_INCLUDE_DIR}"                PARENT_SCOPE)
set(PICOFACE_CORE_SOURCES                    "${PICOFACE_CORE_SOURCES}"                    PARENT_SCOPE)
set(PICOFACE_CORE_MODULE_UI_PANEL_SOURCES    "${PICOFACE_CORE_MODULE_UI_PANEL_SOURCES}"    PARENT_SCOPE)
set(PICOFACE_CORE_MODULE_MIDI_REFACE_SOURCES "${PICOFACE_CORE_MODULE_MIDI_REFACE_SOURCES}" PARENT_SCOPE)
set(PICOFACE_CORE_MODULES                    "${PICOFACE_CORE_MODULES}"                    PARENT_SCOPE)

# --- summary ------------------------------------------------------------------
list(LENGTH PICOFACE_CORE_SOURCES _picoface_num_core_sources)
list(LENGTH PICOFACE_CORE_MODULES _picoface_num_modules)
message(STATUS "PicoFace core: ${_picoface_num_core_sources} base sources, "
               "${_picoface_num_modules} modules (${PICOFACE_CORE_MODULES})")

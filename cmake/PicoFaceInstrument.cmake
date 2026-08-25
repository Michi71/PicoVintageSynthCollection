# PicoFaceInstrument.cmake
#
# Provides picoface_add_instrument(), the single entry point used by every
# instrument sub-project to create a complete RP2350 firmware target.
#
# All settings are applied target-locally - nothing global - so that all
# instruments with conflicting project_config.h files can coexist in one build.
#
# Requires before invocation:
#   - pico-sdk initialized (pico_sdk_init())
#   - PICOFACE_CORE_SOURCES     set by core/CMakeLists.txt (absolute paths)
#   - PICOFACE_CORE_INCLUDE_DIR set by core/CMakeLists.txt (absolute path)

include_guard(GLOBAL)

function(picoface_add_instrument)
    set(_options NO_DOUBLE_RESET)
    set(_one_value NAME PROGRAM_NAME VERSION USB_PID DIR)
    set(_multi_value SOURCES INCLUDE_DIRS DEFINES LINK_LIBRARIES PIO_SOURCES
                     CORE_EXCLUDE CORE_MODULES)
    cmake_parse_arguments(PF "${_options}" "${_one_value}" "${_multi_value}" ${ARGN})

    # --- mandatory arguments -------------------------------------------------
    if(NOT PF_NAME)
        message(FATAL_ERROR "picoface_add_instrument: NAME is required")
    endif()
    if(NOT PF_USB_PID)
        message(FATAL_ERROR "picoface_add_instrument(${PF_NAME}): USB_PID is required")
    endif()

    # --- defaults ------------------------------------------------------------
    if(NOT PF_PROGRAM_NAME)
        set(PF_PROGRAM_NAME "${PF_NAME}")
    endif()
    # Instruments do not declare this: it is the repository's version, derived
    # from git in the top-level CMakeLists so the splash names the build. An
    # explicit VERSION argument still wins, for anything that needs its own.
    if(NOT PF_VERSION)
        set(PF_VERSION "${PICOFACE_VERSION}")
    endif()
    if(NOT PF_DIR)
        set(PF_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT PF_INCLUDE_DIRS)
        set(PF_INCLUDE_DIRS include)
    endif()

    # --- core sources must have been published by core/CMakeLists.txt ---------
    if(NOT PICOFACE_CORE_SOURCES OR NOT PICOFACE_CORE_INCLUDE_DIR)
        message(FATAL_ERROR
            "picoface_add_instrument(${PF_NAME}): PICOFACE_CORE_SOURCES / "
            "PICOFACE_CORE_INCLUDE_DIR not set - add core/ before instruments/")
    endif()

    # --- resolve instrument sources to absolute paths -------------------------
    set(_sources "")
    foreach(_src IN LISTS PF_SOURCES)
        if(IS_ABSOLUTE "${_src}")
            list(APPEND _sources "${_src}")
        else()
            list(APPEND _sources "${PF_DIR}/${_src}")
        endif()
    endforeach()

    # --- assemble the core sources for this instrument -----------------------
    # Start with a copy of all core sources
    set(_core_sources ${PICOFACE_CORE_SOURCES})

    # Append sources of requested core modules.
    # This happens BEFORE the CORE_EXCLUDE filter so that an instrument can
    # also replace an individual module source with its own variant; otherwise
    # both translation units would be compiled and the link would fail with
    # duplicate symbols.
    foreach(_mod IN LISTS PF_CORE_MODULES)
        if(NOT _mod IN_LIST PICOFACE_CORE_MODULES)
            message(FATAL_ERROR "picoface_add_instrument(${PF_NAME}): unknown core module '${_mod}', available: ${PICOFACE_CORE_MODULES}")
        endif()
        string(TOUPPER "${_mod}" _mod_upper)
        list(APPEND _core_sources ${PICOFACE_CORE_MODULE_${_mod_upper}_SOURCES})
    endforeach()

    # Remove files whose basename is listed in CORE_EXCLUDE
    set(_excluded_hits "")
    set(_filtered_sources "")
    foreach(_src IN LISTS _core_sources)
        get_filename_component(_src_name "${_src}" NAME)
        if(_src_name IN_LIST PF_CORE_EXCLUDE)
            list(APPEND _excluded_hits "${_src_name}")
        else()
            list(APPEND _filtered_sources "${_src}")
        endif()
    endforeach()
    set(_core_sources ${_filtered_sources})

    # Every exclude entry must have matched a core or module source (catch typos)
    foreach(_excl IN LISTS PF_CORE_EXCLUDE)
        if(NOT _excl IN_LIST _excluded_hits)
            message(FATAL_ERROR "picoface_add_instrument(${PF_NAME}): CORE_EXCLUDE entry '${_excl}' does not match any core or module source")
        endif()
    endforeach()

    add_executable(${PF_NAME} ${_sources} ${_core_sources})

    # --- include path order: instrument first, core second, build dir third ---
    set(_include_dirs "")
    foreach(_dir IN LISTS PF_INCLUDE_DIRS)
        if(IS_ABSOLUTE "${_dir}")
            list(APPEND _include_dirs "${_dir}")
        else()
            list(APPEND _include_dirs "${PF_DIR}/${_dir}")
        endif()
    endforeach()
    target_include_directories(${PF_NAME} PRIVATE
        ${_include_dirs}
        ${PICOFACE_CORE_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    # --- per-instrument boot stage 2 (QMI flash timing differs per platform) --
    if(PICO_PLATFORM STREQUAL "rp2040")
        set(_boot2_clkdiv 4)
    else()
        set(_boot2_clkdiv 3)
    endif()
    pico_define_boot_stage2(picoface_boot2_${PF_NAME} ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    target_compile_definitions(picoface_boot2_${PF_NAME} PRIVATE
        PICO_FLASH_SPI_CLKDIV=${_boot2_clkdiv})
    pico_set_boot_stage2(${PF_NAME} picoface_boot2_${PF_NAME})

    # --- program metadata / stdio ---------------------------------------------
    pico_set_program_name(${PF_NAME} "${PF_PROGRAM_NAME}")
    pico_set_program_version(${PF_NAME} "${PF_VERSION}")
    pico_enable_stdio_uart(${PF_NAME} 1)
    pico_enable_stdio_usb(${PF_NAME} 0)

    # --- link libraries ---------------------------------------------------------
    target_link_libraries(${PF_NAME} PRIVATE
        pico_stdlib
        # pico_hw.h includes hardware/adc.h, hardware/spi.h and hardware/interp.h
        # in every instrument, so these belong to the shared baseline rather
        # than to a single instrument.
        hardware_adc
        hardware_spi
        hardware_interp
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_timer
        hardware_watchdog
        hardware_clocks
        hardware_sync_spin_lock
        hardware_sync
        hardware_flash
        pico_unique_id
        pico_util_buffer
        pico_multicore
        pico_stdio_uart
        tinyusb_device
        tinyusb_board
        Audio
        RotaryEncoder
        u8g2
        ${PF_LINK_LIBRARIES}
    )

    # --- double-tap RESET into BOOTSEL ---------------------------------------
    # Linked by default, because the library is what makes a board without an
    # accessible BOOTSEL button reflashable. NO_DOUBLE_RESET opts out, and two
    # instruments do: with a Waveshare Pico Audio board driving 3 W speakers,
    # the inrush current on plug-in dips the supply, the chip browns out, and
    # the library reads that reset as a double tap - the device then sits in
    # BOOTSEL instead of running the program. The flag survives the dip in the
    # POWMAN register, so shortening the detection window does not help. See
    # instruments/PicoFaceMD/README.md, "Double-tap RESET".
    if(NOT PF_NO_DOUBLE_RESET)
        target_link_libraries(${PF_NAME} PRIVATE pico_bootsel_via_double_reset)
    endif()

    # --- compile definitions -----------------------------------------------------
    set(_defines
        USE_AUDIO_I2S=1
        PICO_AUDIO_I2S_DATA_PIN=26
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=27
    )
    # PICO_USE_SW_SPIN_LOCKS and the enlarged stacks are deliberately NOT set
    # here. In the original repositories they were per-instrument: YC, J6, MD
    # and SM used software spin locks, CP and RD did not; YC, CP and RD raised
    # the stacks, J6, MD and SM did not. Applying them to everything changed
    # locking primitives and stack layout for instruments that never had them,
    # which is not something to do untested on a multicore audio build. Each
    # instrument therefore passes what it used to have via DEFINES.
    list(APPEND _defines
        PICOFACE_INSTRUMENT_NAME=\"${PF_PROGRAM_NAME}\"
        PICOFACE_USB_PID=${PF_USB_PID}
        PICOFACE_VERSION=\"${PF_VERSION}\"
    )
    target_compile_definitions(${PF_NAME} PRIVATE ${_defines} ${PF_DEFINES})

    # --- compile / link options (target-local only) ------------------------------
    target_compile_options(${PF_NAME} PRIVATE
        -Wall
        -Wno-format
        -Wno-unused-function
        -ffast-math
    )
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${PF_NAME} PRIVATE -Wno-maybe-uninitialized)
    endif()
    target_link_options(${PF_NAME} PRIVATE -Xlinker --print-memory-usage)

    # --- optional PIO headers ------------------------------------------------------
    foreach(_pio IN LISTS PF_PIO_SOURCES)
        if(IS_ABSOLUTE "${_pio}")
            pico_generate_pio_header(${PF_NAME} "${_pio}")
        else()
            pico_generate_pio_header(${PF_NAME} "${PF_DIR}/${_pio}")
        endif()
    endforeach()

    # --- .uf2 / .elf / .bin / .elf.map ------------------------------------------------
    pico_add_extra_outputs(${PF_NAME})

    # --- register instrument in the global list ---------------------------------------
    set_property(GLOBAL APPEND PROPERTY PICOFACE_INSTRUMENTS ${PF_NAME})

    message(STATUS "PicoFace instrument: ${PF_NAME} (PID ${PF_USB_PID})")
endfunction()

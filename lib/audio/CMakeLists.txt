
add_library(Audio STATIC
	src/audio.cpp
	src/audio_i2s.c
	src/audio_subsystem.cpp
)

target_include_directories(Audio PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

pico_generate_pio_header(Audio ${CMAKE_CURRENT_LIST_DIR}/piosrc/audio_i2s.pio)

# The I2S pins and the PIO GPIO base come from the board variant selected in
# the top-level CMakeLists (PICOFACE_BOARD). They must reach THIS target: the
# library is compiled once, separately from the instruments, and a define put
# only on the instrument targets never arrives here -- the header defaults
# (26/27) then silently win, which is exactly how a board variant fails to
# take effect while looking correct everywhere else.
if(NOT DEFINED PICOFACE_I2S_DATA_PIN OR NOT DEFINED PICOFACE_I2S_CLOCK_PIN_BASE OR NOT DEFINED PICOFACE_PIO_GPIO_BASE)
    message(FATAL_ERROR "lib/audio: PICOFACE_I2S_DATA_PIN / PICOFACE_I2S_CLOCK_PIN_BASE / PICOFACE_PIO_GPIO_BASE must be set by the board variant before add_subdirectory(lib/audio)")
endif()
target_compile_definitions(Audio PUBLIC
    PICO_AUDIO_I2S_DATA_PIN=${PICOFACE_I2S_DATA_PIN}
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=${PICOFACE_I2S_CLOCK_PIN_BASE}
    PICOFACE_PIO_GPIO_BASE=${PICOFACE_PIO_GPIO_BASE}
)

target_link_libraries(Audio pico_stdlib pico_util_buffer pico_multicore hardware_pio hardware_dma)

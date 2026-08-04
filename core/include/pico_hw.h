// pico_hw.h - hardware access for the PicoFace platform.
//
// Shared by every instrument. The three original variants differed only
// in which SDK headers they pulled in and whether they declared
// pico_fpu_ftz_enable(); this is the union of both.
#ifndef __PICO_HW_H__
#define __PICO_HW_H__

#include <stdio.h>
#include <cstdlib>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/interp.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "hardware/structs/rosc.h"

#if __has_include("bsp/board_api.h")
#include "bsp/board_api.h"
#else
#include "bsp/board.h"
#endif

#include "u8g2.h"

// Helper methods which are called from C code
#ifdef __cplusplus
extern "C" {
#endif

uint8_t u8x8_byte_pico_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg,uint8_t arg_int, void *arg_ptr);

#ifdef __cplusplus
}
#endif

// Enable FPU flush-to-zero (FZ) + default-NaN (DN) on the CALLING core.
// FPSCR is per-core state, so every core that touches floats must run this;
// without it, decaying IIR states in the engine (reverb comb/allpass feedback,
// rotary crossover low-pass) settle into the subnormal float range, where the
// Cortex-M33 FPU falls back to a much slower software path -- audible as
// intermittent hiss/jitter under load. Flushing denormals is inaudible (values
// are already below the noise floor) and standard practice for real-time DSP.
static inline void pico_fpu_ftz_enable(void)
{
    uint32_t fpscr;
    __asm__ volatile ("vmrs %0, fpscr" : "=r" (fpscr));
    fpscr |= (1u << 24) | (1u << 25);
    __asm__ volatile ("vmsr fpscr, %0" : : "r" (fpscr));
}

void pico_init();

#endif // __PICO_HW_H__

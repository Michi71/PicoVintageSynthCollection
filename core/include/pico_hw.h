// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

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

// What the bootrom left in the QMI, captured at the first instruction of
// pico_init() before anything is changed, plus what we concluded from it.
// The three registers are diagnostics; the last two are what the flash is
// actually running with, and every site that restores M0_TIMING after a flash
// write must use picoface_qmi_timing_effective rather than the compile-time
// target -- on a board where the bootrom did not establish quad mode, the
// target is not what we are running.
extern uint32_t picoface_boot_qmi_timing;
extern uint32_t picoface_boot_qmi_rfmt;
extern uint32_t picoface_boot_qmi_rcmd;
extern bool     picoface_flash_is_quad;
extern uint32_t picoface_qmi_timing_effective;

// False when no rung of the boot-time ladder reproduced the reference checksum
// and the bootrom's own rescaled timing had to be taken on trust. Boards that
// verify -- every one tested so far -- set this true.
extern bool     picoface_flash_verified;

// Flash identification and the quad story. picoface_flash_jedec holds the
// 0x9F answer (manufacturer, type, capacity). picoface_flash_quad_by_us is
// true when the bootrom left the flash in dual and pico_init() set the part's
// quad-enable bit and switched it to EBh itself; such boards run the
// CAUTIOUS rung. picoface_flash_after_write() must be called after every
// flash program/erase: the SDK re-enters XIP in 03h serial there, and the
// timing alone does not bring the mode back.
extern uint32_t picoface_flash_jedec;
extern bool     picoface_flash_quad_by_us;
void picoface_flash_after_write(void);

#endif // __PICO_HW_H__

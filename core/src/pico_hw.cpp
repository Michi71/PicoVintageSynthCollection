// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// pico_hw.cpp - boot, clock and flash timing for the PicoFace platform.
//
// One board for every instrument. The only per-instrument choice left is
// the clock target and its matching flash timing (see the two macros below);
// PicoFaceRD overrides both through DEFINES in its instrument.cmake.
#pragma GCC optimize("Ofast")
#include <pico/time.h>
#include "hardware/clocks.h"
#include <pico/stdlib.h>
#include <hardware/vreg.h>
#include <hardware/sync.h>
#include <pico/multicore.h>

#include "pico_hw.h"
#include "project_config.h"

#if PICO_RP2040
// #include "../../memops_opt/memops_opt.h"
#else
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
#endif


// PICOFACE_SYS_CLOCK_HZ and PICOFACE_QMI_M0_TIMING_TARGET come from
// project_config.h, which is where the flash-write sites can see them too.

// A transfer is at most 32 bytes, so 300 us at 1 MHz and 750 us at 400 kHz.
// 5 ms is more than an order of magnitude of headroom and still bounded.
static const uint32_t kOledXferTimeoutUs = 5000;
// Twenty in a row: a healthy bus never produces one.
static const uint8_t  kOledMaxFailures   = 20;
static uint8_t s_oledFailures = 0;
static bool    s_oledDead     = false;

uint8_t u8x8_byte_pico_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buffer[32]; /* u8g2/u8x8 will never send more than 32 bytes between START_TRANSFER and END_TRANSFER */
    static uint8_t buf_idx;
    uint8_t *data;

    switch (msg)
    {
    case U8X8_MSG_BYTE_SEND:
        data = (uint8_t *)arg_ptr;
        while (arg_int > 0)
        {
            buffer[buf_idx++] = *data;
            data++;
            arg_int--;
        }
        break;
    case U8X8_MSG_BYTE_INIT:
        i2c_init(i2c1, PICOFACE_OLED_I2C_HZ);
        gpio_set_function(PIN_OLED_SDA, GPIO_FUNC_I2C);
        gpio_set_function(PIN_OLED_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(PIN_OLED_SDA);
        gpio_pull_up(PIN_OLED_SCL);
        break;
    case U8X8_MSG_BYTE_SET_DC:
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        buf_idx = 0;
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        // Bounded, and it gives up. i2c_write_blocking() waits forever for the
        // TX FIFO to empty, and nothing bounds that wait: any wiring that holds
        // SDA or SCL low spins there for good. The first transfer happens in
        // u8g2_InitDisplay(), which runs BEFORE the splash loop -- the only
        // place tud_task() gets called during startup -- so a shorted display
        // cable takes the whole instrument down without so much as enumerating
        // over USB. It reads exactly like "the firmware does not boot".
        //
        // A missing display was never the problem: the SDK checks
        // tx_abrt_source and returns an error on a NACK, so an absent panel
        // just leaves the screen dark. It is a held line that hangs.
        //
        // After kOledMaxFailures consecutive timeouts the panel is written off
        // for this power cycle. A broken display should cost you the display,
        // not the MIDI, the audio and the encoders. There is deliberately no
        // retry: a loose wire that comes and goes would otherwise pay the
        // timeout on every transfer forever, and a reboot is the honest cure.
        if (!s_oledDead) {
            const int r = i2c_write_timeout_us(i2c1, u8x8_GetI2CAddress(u8x8) >> 1,
                                               buffer, buf_idx, false,
                                               kOledXferTimeoutUs);
            if (r < 0) {
                if (++s_oledFailures >= kOledMaxFailures) s_oledDead = true;
            } else {
                s_oledFailures = 0;
            }
        }
        break;
    default:
        return 0;
        break;
    }
    return 1;
}

uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg,uint8_t arg_int, void *arg_ptr) 
{
  return 1;
}

void pico_init()
{
    // FPU flush-to-zero + default-NaN for THIS core (see pico_hw.h for the
    // rationale; core 1 repeats the call at the top of core1_main).
    pico_fpu_ftz_enable();

#if PICO_RP2350
    // NOTE: 1.60 V is +45% over the nominal 1.1 V DVDD and above the SDK's
    // "at your own risk" limit -- it was adopted for the overclock but never
    // bisected. With hardware at hand: step down (1.50 -> 1.40 -> 1.35) using
    // a sustained full-polyphony soak with the CPU-load page as pass
    // criterion, then keep one 50 mV step of margin. Lower voltage means less
    // die heating and slower aging.
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);
    sleep_ms(10);   // switching regulator settles in tens of microseconds

    // Raise clk_sys with the flash held at a slack timing across the switch.
    //
    // Ordering matters and is not free: the M0_TIMING write goes out over APB,
    // while the instruction fetches that follow reach the same peripheral over
    // the XIP path. Those are two routes to one endpoint and are not ordered
    // against each other, so __dsb() is what actually guarantees the new timing
    // is in effect before the next fetch, and __isb() discards anything already
    // prefetched under the old one.
    //
    // set_sys_clock_hz() is called with required=false: on an unreachable
    // target it returns false and silently leaves clk_sys at its default.
    // 444 MHz is reachable via the PLL (VCO 1332 / 3), but if that ever changes
    // we must not tighten the flash timing for a clock the part never got to --
    // so keep the slack timing in that case. The symptom is then loud rather
    // than subtle: the synth runs at 150 MHz and the load percentage on the
    // status line goes through the roof.
    qmi_hw->m[0].timing = PICOFACE_QMI_M0_TIMING_SAFE;
    __dsb();
    // The datasheet asks for one more thing here, which we were not doing:
    //
    //   "If software is increasing CLKDIV in anticipation of an increase in the
    //    system clock frequency, a dummy access to either memory window (and
    //    appropriate processor barriers/fences) must be inserted after the
    //    Mx_TIMING write to ensure the SCK divisor change is in effect _before_
    //    the system clock is changed."
    //
    // That is exactly this write: CLKDIV goes 4 -> 8 ahead of the clk_sys jump.
    // The barriers order the APB write, but they do not make the QMI run a
    // transaction, and until it does the new divisor need not be in effect. The
    // only flash access that followed was the instruction fetch of the code
    // below -- which the XIP cache may well serve, in which case no transaction
    // happens at all and clk_sys moves while the old divisor still stands. That
    // is a board-dependent failure by construction, and it sits in the same
    // window that hung the 480 MHz build (see the MD README).
    //
    // Hence the read through XIP_NOCACHE_NOALLOC_BASE rather than XIP_BASE: a
    // cached read is allowed to be silent, and a silent dummy access is not one.
    (void)*(volatile uint32_t *)XIP_NOCACHE_NOALLOC_BASE;
    __dsb();
    __isb();

    const bool clockOk = set_sys_clock_hz(PICOFACE_SYS_CLOCK_HZ, false);
    // The SAFE timing stays in place if the target turned out to be unreachable.
    qmi_hw->m[0].timing = clockOk ? PICOFACE_QMI_M0_TIMING_TARGET
                                  : PICOFACE_QMI_M0_TIMING_SAFE;
    __dsb();
    __isb();
#else
    hw_set_bits(&vreg_and_chip_reset_hw->vreg, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    sleep_ms(33);
    set_sys_clock_khz(402 * 1000, true);
#endif

    // Initialize stdio
    stdio_init_all();

    // LED on GPIO25
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);

    uint32_t rand_seed = 0;
    for (int i = 0; i < 32; i++)
    {
        bool randomBit = rosc_hw->randombit;
        rand_seed = rand_seed | (randomBit << i);
    }

    srand(rand_seed);
}

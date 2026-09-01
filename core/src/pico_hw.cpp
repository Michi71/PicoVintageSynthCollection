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

uint32_t picoface_boot_qmi_timing      = 0;
uint32_t picoface_boot_qmi_rfmt        = 0;
uint32_t picoface_boot_qmi_rcmd        = 0;
bool     picoface_flash_is_quad        = true;   // assumed until pico_init() looks
uint32_t picoface_qmi_timing_effective = PICOFACE_QMI_M0_TIMING_SAFE;
bool     picoface_flash_verified       = false;  // did the chosen timing prove itself?

#if PICO_RP2350
// --- Boot-time flash timing self-calibration -------------------------------
//
// The timing this project wants is faster than the part is specified for: at
// 148 MHz the device is given 3.88 ns where a 133 MHz part may ask for 6 in
// the worst case. It has worked on every board tested, because real silicon at
// room temperature beats its worst case by a wide margin -- but "works on the
// boards we own" is not a property one can put in a hardware requirement, and
// two issues report boards that will not start.
//
// So stop asserting it and measure it instead. Checksum a slab of flash at the
// timing the bootrom itself was using, raise the clock, then walk a ladder from
// fastest to slowest and keep the first rung that reproduces the checksum. A
// board that cannot hold 148 MHz lands on 111 or 55 instead of not booting, and
// a board that can is left exactly where it was -- this must never slow down
// hardware that already works.
//
// Everything from here to the end of the ladder runs from SRAM. That is the
// part that makes it safe rather than merely clever: while a candidate timing
// is in force every flash read is suspect, so a wrong one has to corrupt DATA,
// never the instruction stream. Code in flash would fetch garbage and fault.

// 32 KB of the firmware image itself -- varied data rather than a pattern, and
// long enough that a marginal sample point shows up as a mismatch rather than
// getting lucky. Costs about 2 ms per rung at full speed.
#define PICOFACE_FLASH_PROBE_WORDS (8u * 1024u)

// NOTE on __attribute__((noinline)): pico-sdk's __not_in_flash_func() sets the
// section and nothing else. Under this file's `#pragma GCC optimize("Ofast")`
// the compiler happily inlines these three into pico_init(), which lives in
// flash -- and then the routine that must not execute from flash does exactly
// that, silently, with no diagnostic. The build looks identical and the design
// is gone. Keep noinline, and keep the check in the PR that reads the symbol
// addresses back out of the ELF.

// Read through the no-cache window: a cached second pass would be answered
// from SRAM and would "verify" any timing at all, including a broken one.
static __attribute__((noinline)) uint32_t __not_in_flash_func(picoface_flash_probe)(void)
{
    const volatile uint32_t *p = (const volatile uint32_t *)XIP_NOCACHE_NOALLOC_BASE;
    uint32_t h = 2166136261u;                       // FNV-1a over words
    for (uint32_t i = 0; i < PICOFACE_FLASH_PROBE_WORDS; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static __attribute__((noinline)) bool __not_in_flash_func(picoface_flash_try)(uint32_t timing, uint32_t ref)
{
    qmi_hw->m[0].timing = timing;
    __dsb();
    (void)*(volatile uint32_t *)XIP_NOCACHE_NOALLOC_BASE;   // make the divisor take effect
    __dsb();
    __isb();
    return picoface_flash_probe() == ref;
}

// Rungs arrive as arguments, not as a table: a flash-resident array would have
// to be read while a candidate timing is in force, which is the one thing this
// routine may not do. The literals are materialised by the caller, before any
// of them is applied.
static __attribute__((noinline)) uint32_t __not_in_flash_func(picoface_flash_autotune)(uint32_t ref,
                                                             uint32_t fast,
                                                             uint32_t mid,
                                                             uint32_t slow,
                                                             uint32_t fallback,
                                                             bool *verified)
{
    *verified = true;
    if (picoface_flash_try(fast, ref)) return fast;
    if (picoface_flash_try(mid,  ref)) return mid;
    if (picoface_flash_try(slow, ref)) return slow;
    *verified = false;
    // Nothing verified. The bootrom's own configuration, rescaled to hold the
    // flash clock it chose, is the most trustworthy thing left; apply it and
    // take it either way, because there is nothing below it to fall to.
    (void)picoface_flash_try(fallback, ref);
    return fallback;
}
#endif

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
    // What did the bootrom actually leave here? On RP2350 boot_stage2 is
    // compiled but never runs -- the bootrom configures the flash, and every
    // firmware inherits its read mode, command byte, address and dummy widths
    // without looking (raspberrypi/pico-sdk#1903). This one used to inherit
    // them and then overwrite CLKDIV as if quad mode were a given.
    //
    // It is not. A board in issue #107 reported RCMD=0xBB and two-bit widths
    // with DUMMY_LEN=0 and CLKDIV=35 -- the bootrom had failed to establish
    // quad on that flash and fallen back to a dual-I/O configuration with no
    // dummy cycles, which is a low-clock arrangement. Writing CLKDIV=3 over
    // that runs the part twelve times faster than its own bootrom judged safe,
    // in a mode we never checked. The board does not boot.
    picoface_boot_qmi_timing = qmi_hw->m[0].timing;
    picoface_boot_qmi_rfmt   = qmi_hw->m[0].rfmt;
    picoface_boot_qmi_rcmd   = qmi_hw->m[0].rcmd;

    // Quad means the EBh read command AND a four-bit data phase. Either alone
    // is not enough to trust: the command byte says what was asked for, the
    // width says what the QMI will actually clock.
    const uint32_t bootDataWidth =
        (picoface_boot_qmi_rfmt & QMI_M0_RFMT_DATA_WIDTH_BITS) >> QMI_M0_RFMT_DATA_WIDTH_LSB;
    picoface_flash_is_quad =
        ((picoface_boot_qmi_rcmd & 0xFFu) == 0xEBu) && (bootDataWidth == 2u);

    // The reference: a checksum of a slab of flash taken at the timing the
    // bootrom itself was using. The chip booted and is executing from flash
    // with it, which makes it the one configuration known to be correct here.
    const uint32_t flashRef = picoface_flash_probe();

    // The last resort, computed while the old clock still stands: the bootrom's
    // own timing with CLKDIV scaled so the flash keeps the clock it chose
    // across the jump. Every other field it picked is preserved -- RXDELAY,
    // cooldown, deselect -- because none of them was chosen by us.
    uint32_t bootDiv = picoface_boot_qmi_timing & QMI_M0_TIMING_CLKDIV_BITS;
    if (bootDiv == 0u) bootDiv = 256u;                 // 0 encodes 256
    const uint32_t clkNow = clock_get_hz(clk_sys);
    // ceil(target * bootDiv / clkNow), clamped into the 1..255 the field holds
    uint64_t want = ((uint64_t)PICOFACE_SYS_CLOCK_HZ * bootDiv + clkNow - 1u) / clkNow;
    if (want < 1u)   want = 1u;
    if (want > 255u) want = 255u;
    const uint32_t scaledBoot =
        (picoface_boot_qmi_timing & ~QMI_M0_TIMING_CLKDIV_BITS) | (uint32_t)want;

    // Slack across the clock switch. A board the bootrom put in quad gets SAFE,
    // which is what has always been used here and is known to survive the jump;
    // anything else gets its own bootrom's settings rescaled, because SAFE's
    // other fields were never chosen with that flash in mind.
    const uint32_t slack = picoface_flash_is_quad ? PICOFACE_QMI_M0_TIMING_SAFE
                                                  : scaledBoot;
    qmi_hw->m[0].timing = slack;
    __dsb();
    // The datasheet asks for one more thing here, which we were not doing:
    //
    //   "If software is increasing CLKDIV in anticipation of an increase in the
    //    system clock frequency, a dummy access to either memory window (and
    //    appropriate processor barriers/fences) must be inserted after the
    //    Mx_TIMING write to ensure the SCK divisor change is in effect _before_
    //    the system clock is changed."
    //
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

    // set_sys_clock_hz() is called with required=false: on an unreachable
    // target it returns false and silently leaves clk_sys at its default.
    // 444 MHz is reachable via the PLL (VCO 1332 / 3), but if that ever changes
    // we must not tighten the flash timing for a clock the part never got to.
    const bool clockOk = set_sys_clock_hz(PICOFACE_SYS_CLOCK_HZ, false);

    if (!clockOk) {
        // clk_sys never moved, so the slack timing is already the right one and
        // nothing faster may be applied. The symptom is loud rather than
        // subtle: the synth runs at 150 MHz and the load percentage on the
        // status line goes through the roof.
        picoface_qmi_timing_effective = slack;
        picoface_flash_verified       = false;
    } else if (!picoface_flash_is_quad) {
        // The bootrom could not establish quad on this flash. Do NOT probe
        // upward here.
        //
        // The upward ladder rests on an assumption that hardware has now
        // refuted: that a too-fast timing returns bad data, so the checksum
        // mismatches and the next rung gets a turn. On a board in issue #107 it
        // does not -- it takes the chip down, and the ladder never reaches its
        // second rung. Two 444 MHz images differing only in their FIRST rung
        // decide it: with a slow one the board boots, with RX4 it is dead.
        // A faulting XIP read is not confined to data either; the fault handler
        // itself lives in flash, which is exactly what is broken at that moment.
        //
        // So on these boards keep what the bootrom chose, rescaled to hold its
        // clock. That is slow and it boots, which beats fast and dead. The
        // ladder stays for boards the bootrom put in quad, where every rung-one
        // verification so far has succeeded and no bad timing is ever applied.
        picoface_qmi_timing_effective = scaledBoot;
        picoface_flash_verified       = false;
    } else {
        picoface_qmi_timing_effective =
            picoface_flash_autotune(flashRef,
                                    PICOFACE_QMI_M0_TIMING_TARGET,
                                    PICOFACE_QMI_M0_TIMING_CD4,
                                    PICOFACE_QMI_M0_TIMING_SAFE,
                                    scaledBoot,
                                    &picoface_flash_verified);
    }
    qmi_hw->m[0].timing = picoface_qmi_timing_effective;
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

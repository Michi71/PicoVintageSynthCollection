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
        // SH1106 supports Fast Mode up to 1 MHz for faster display refresh
        i2c_init(i2c1, 1000 * 1000);
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
        i2c_write_blocking(i2c1, u8x8_GetI2CAddress(u8x8) >> 1, buffer, buf_idx, false);
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
    // "at your own risk" limit -- it was adopted for the 480 MHz overclock but
    // never bisected. With hardware at hand: step down (1.50 -> 1.40 -> 1.35)
    // using run_regression.sh plus a sustained full-polyphony soak with the
    // P/U diagnostics footer as pass criterion, then keep one 50 mV step of
    // margin. Lower voltage means less die heating and slower aging.
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
    // target it returns false and silently leaves clk_sys at its default. Both
    // targets here are reachable via the PLL (480 = VCO 960 / 2,
    // 444 = VCO 1332 / 3), but if that ever changes we must not tighten the
    // flash timing for a clock the part never got to -- so keep the slack
    // timing in that case. The symptom is then loud rather than subtle: the
    // synth runs at 150 MHz and the load percentage on the status line goes
    // through the roof.
    qmi_hw->m[0].timing = PICOFACE_QMI_M0_TIMING_SAFE;
    __dsb();
    __isb();

#ifdef RD_CLOCK_504
    // RD target: 480 MHz with flash divider 4 (120 MHz QSPI, within spec).
    // 504 MHz did not boot on this particular chip; 480 is the fallback step.
    const bool clockOk = set_sys_clock_hz(480000000, false);
    qmi_hw->m[0].timing = clockOk ? PICOFACE_QMI_M0_TIMING_RD
                                  : PICOFACE_QMI_M0_TIMING_SAFE;
#else
    const bool clockOk = set_sys_clock_hz(444000000, false);
    qmi_hw->m[0].timing = clockOk ? PICOFACE_QMI_M0_TIMING_OC
                                  : PICOFACE_QMI_M0_TIMING_SAFE;
#endif
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
}

// libstdc++ exception stubs: the firmware builds with -fno-exceptions, but
// std::vector's failure paths still reference std::__throw_* from libstdc++,
// which drags the ARM unwinder (~2 KB RAM) and the C++ name demangler
// (~30 KB flash) into the link. A failed allocation or a length error on
// this system is fatal either way -- panic instead of unwinding.
namespace std {
__attribute__((noreturn)) void __throw_length_error(const char* what) { panic("std: %s", what); }
__attribute__((noreturn)) void __throw_logic_error(const char* what)  { panic("std: %s", what); }
__attribute__((noreturn)) void __throw_bad_alloc()                    { panic("std: bad_alloc"); }
__attribute__((noreturn)) void __throw_bad_array_new_length()         { panic("std: bad_array_new_length"); }
}

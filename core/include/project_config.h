// project_config.h - the PicoFace hardware platform.
//
// One board for all six instruments: pin map and flash timing live here in
// the core, not per instrument. Every pin below was already identical in all
// six original repositories; only comments and one extra timing constant
// differed, which is why this used to be copied six times.
#ifndef __PROJECT_CONFIG_H__
#define __PROJECT_CONFIG_H__

#define PIN_MIDI_RX 5

// Pimoroni Pico Audio
//#define PIN_I2S_DOUT  9
//#define PIN_I2S_BCK   10
//#define PIN_I2S_WS    11

// Waveshare Pico Audio
#define PIN_I2S_DOUT  26
#define PIN_I2S_BCK   27
#define PIN_I2S_WS    28

#define  PIN_LED	  25

#define PIN_OLED_SDA  2
#define PIN_OLED_SCL  3

//#define PIN_POT_0     28
#define PIN_POT_1     29

// Selector encoder
#define PIN_SEL_CLK   6
#define PIN_SEL_DT    7
#define PIN_SEL_SW    8

// Param A encoder
#define PIN_PA_CLK    10
#define PIN_PA_DT     11
#define PIN_PA_SW     14   // optional switch

// Param B encoder
#define PIN_PB_CLK    12
#define PIN_PB_DT     13
#define PIN_PB_SW     15   // optional switch

// QMI M0_TIMING values. Bit layout (see hardware/regs/qmi.h):
//   CLKDIV  [7:0]   flash clock = clk_sys / CLKDIV
//   RXDELAY [10:8]  read data sample point, in clk_sys cycles
// The upper bits (COOLDOWN=1, PAGEBREAK=2, MIN_DESELECT=7) are identical in
// all three values below; only CLKDIV and RXDELAY differ.

// Set BEFORE the clk_sys change, and left in place if the change fails.
// CLKDIV=8, RXDELAY=2 -- deliberately slack, because this is the timing the
// flash runs with in the window between "clk_sys has jumped to its target"
// and "the final timing has been written". At 444 MHz that window is
// 444/8 = 55 MHz, where RXDELAY=2 has ample margin.
//
// It used to be CLKDIV=4 here, putting that window at 111 MHz. RXDELAY
// compensates a round-trip delay that is fixed in nanoseconds, so the value
// needed grows with clk_sys, and 2 is only barely enough at 111 MHz. A 480 MHz
// build of this same code put the window at 120 MHz, where RXDELAY=2 is not
// enough at all: the core hung on the first instruction fetch after the clock
// switch and the board would not boot. 444 MHz stayed on the working side of
// that edge, but with no margin worth the name. See README, "The 480 MHz boot
// failure".
#define PICOFACE_QMI_M0_TIMING_SAFE 0x60007208u

// 444 MHz target: CLKDIV=3, RXDELAY=3 -> 148 MHz flash (above the chip's
// nominal 133 MHz, hence "overclock"; measured stable on this board).
// Single source of truth for boot (pico_hw.cpp) AND the post-flash-write
// restore in veeprom.cpp -- these MUST match or the device runs with
// wrong flash timing after the first settings save.
#define PICOFACE_QMI_M0_TIMING_OC 0x60007303u

// 480 MHz target: CLKDIV=4, RXDELAY=3 -> 120 MHz flash, within spec. Used by
// PicoFaceRD, whose engine needs the higher core clock; the other five run at
// 444 MHz and use the OC value above. Same single-source-of-truth rule: boot
// (pico_hw.cpp) and the post-flash-write restore in veeprom.cpp must agree.
#define PICOFACE_QMI_M0_TIMING_RD 0x60007304u

// Which of the two target values an instrument uses is a software decision in
// its pico_hw.cpp, not a hardware difference - the board is identical for all
// six.

#endif // __PROJECT_CONFIG_H__

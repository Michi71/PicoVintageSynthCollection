// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// project_config.h - the PicoFace hardware platform.
//
// One board for every instrument: pin map and flash timing live here in
// the core, not per instrument. Every pin below was already identical in all
// original repositories; only comments and one extra timing constant
// differed, which is why this used to be copied once per instrument.
#ifndef __PROJECT_CONFIG_H__
#define __PROJECT_CONFIG_H__

// DIN MIDI on uart1, behind the opto-coupler on the board. uart0 (GPIO 0/1)
// stays reserved for stdio, so the two never collide.
#define PIN_MIDI_RX 5
#define PIN_MIDI_TX 4

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

// OLED bus speed. 1 MHz is Fast-mode Plus, and it is what the reference board
// runs: the display push is paced in half tile rows of roughly 1.5 ms of I2C
// each, so the rate is directly the UI's frame time -- 400 kHz would make a
// full screen 60 ms instead of 24.
//
// It is also above what an SH1106 datasheet promises (400 kHz), and the only
// pull-ups are the chip's internal ones at around 50 kOhm. On the short, tidy
// wiring of the prototype that is fine and has been for every board tested
// here. On longer jumper leads it is the first thing to suspect when a panel
// stays dark while the instrument is otherwise alive -- drop it to 400000 and
// see. Worth adding real 2.2k-4.7k pull-ups before blaming the display.
#ifndef PICOFACE_OLED_I2C_HZ
#define PICOFACE_OLED_I2C_HZ (1000 * 1000)
#endif

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
// its instrument.cmake, not a hardware difference - the board is identical for
// every instrument.

// The clock/timing pair the build actually uses. Five instruments take the
// defaults below; PicoFaceRD overrides both through DEFINES in its
// instrument.cmake. They MUST move together -- the timing encodes a divider of
// clk_sys, so a mismatched pair either runs the flash out of spec or leaves
// performance on the table.
//
// These live here rather than in pico_hw.cpp because the boot is not the only
// place that writes M0_TIMING: every flash write re-inits boot2, which clobbers
// the register, and each of those sites has to restore the SAME value. With the
// default sitting in pico_hw.cpp, a -DPICOFACE_QMI_M0_TIMING_TARGET=... on the
// command line reached the boot and nothing else, so the device silently
// reverted to the OC timing at the first settings save.
#ifndef PICOFACE_SYS_CLOCK_HZ
#define PICOFACE_SYS_CLOCK_HZ 444000000
#endif

#ifndef PICOFACE_QMI_M0_TIMING_TARGET
#define PICOFACE_QMI_M0_TIMING_TARGET PICOFACE_QMI_M0_TIMING_OC
#endif

// Slower flash timings, for boards whose QSPI part does not survive the 148 MHz
// the OC value asks for. RXDELAY counts HALF clk_sys cycles and a value of 0
// samples on the SCK edge that launched the command, so the time a flash device
// has to get its data back is (half an SCK period) + (RXDELAY half-cycles):
//
//   OC   at 444 MHz  148.0 MHz SCK   6.76 ns   <- the collection's tightest
//   RD   at 480 MHz  120.0 MHz SCK   7.29 ns
//   RX4  at 444 MHz  148.0 MHz SCK   7.88 ns   full speed, later sample point
//   CD4  at 444 MHz  111.0 MHz SCK   7.88 ns   within any 133 MHz part's spec
//   SAFE at 444 MHz   55.5 MHz SCK  11.26 ns   slack enough for anything
//
// A W25Q128JV is specified at 6 ns clock-to-Q plus the pad round trip, so the
// OC value has no margin worth the name and depends on the individual part.
#define PICOFACE_QMI_M0_TIMING_RX4 0x60007403u   // CLKDIV=3, RXDELAY=4
#define PICOFACE_QMI_M0_TIMING_CD4 0x60007404u   // CLKDIV=4, RXDELAY=4

#endif // __PROJECT_CONFIG_H__

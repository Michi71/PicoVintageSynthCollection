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
//   RXDELAY [10:8]  read data sample point, in HALF clk_sys cycles
//
// The budget, which the RP2350 datasheet makes computable rather than a matter
// of taste. QMI samples on the rising SCK edge and RXDELAY pushes that later,
// so the time a flash device has to get its data back to the sampling register
// is (half an SCK period) + (RXDELAY half clk_sys cycles), and into that must
// fit the pads plus the device. Datasheet Table 1292 gives the pads at
// VDDIO 3.3 V, worst case over process, voltage and temperature: system clock
// to QSPI output 2.5 ns, QSPI input to system clock 1.5 ns. A 133 MHz QSPI part
// specifies 6 ns clock-to-output. The requirement is therefore 10.0 ns, and at
// 444 MHz the four values below come out as:
//
//   name  CLKDIV RXDELAY      SCK     budget   left for the device
//   SAFE     8      2      55.5 MHz   11.26      7.26 ns
//   CD4      4      5     111.0 MHz   10.14      6.14 ns   <- the default
//   RX4      3      4     148.0 MHz    7.88      3.88 ns
//   OC       3      3     148.0 MHz    6.76      2.76 ns
//
// The datasheet is explicit that this ceiling belongs to the board rather than
// to the chip: "the maximum SCK frequency is constrained by the limits of the
// attached QSPI device, the signal integrity afforded by the PCB layout, and IO
// delays in the pads". OC leaves 2.76 ns where the part asks for 6, and has run
// on every board tested here -- real pads and a real flash at room temperature
// are far better than their worst-case numbers. But that is exactly the profile
// of a setting that works on one board and not the next, and two issues report
// boards that will not boot.
//
// Sampling too LATE is the other failure, and none of these is close to it:
// the data stays valid for an SCK period plus the device's output hold, so
// even CD4's sample at 10.14 ns sits inside a window running past 12 ns.
//
// Which one is the default is a trade, and it was measured rather than argued.
// On the D5 -- the most XIP-bound of the ten, because it reads its PCM from
// flash -- the boot benchmark went from B51 at 148 MHz to B59 at 111 MHz.
// Four voices, so 10.0 % per voice against 12.0 %: about 1.3 voices of
// headroom before the governor starts trimming tails. That is a real price for
// margin nobody has yet shown we need, since neither reporter of a
// non-booting board has tested anything.
//
// So RX4 is the default. Against the OC it replaces it is strictly better and
// free: same SCK, same throughput, 1.1 ns more for the device purely by
// sampling later. CD4 is the next rung and costs those 1.3 voices; it is
// where to go if a board still will not boot on RX4.
//
// The upper bits (COOLDOWN=1, PAGEBREAK=2, MIN_DESELECT=7) are identical in
// all values below; only CLKDIV and RXDELAY differ.

// Set BEFORE the clk_sys change, and left in place if the change fails.
// CLKDIV=8, RXDELAY=2 -- deliberately slack, because this is the timing the
// flash runs with in the window between "clk_sys has jumped to its target"
// and "the final timing has been written". At 444 MHz that window is
// 444/8 = 55 MHz, where RXDELAY=2 has ample margin.
//
// It used to be CLKDIV=4 here, putting that window at 111 MHz. RXDELAY
// compensates a round trip that is fixed in nanoseconds, so the value needed
// grows with clk_sys, and 2 is only barely enough at 111 MHz. A 480 MHz
// build of this same code put the window at 120 MHz, where RXDELAY=2 is not
// enough at all: the core hung on the first instruction fetch after the clock
// switch and the board would not boot. 444 MHz stayed on the working side of
// that edge, but with no margin worth the name. See README, "The 480 MHz boot
// failure".
#define PICOFACE_QMI_M0_TIMING_SAFE 0x60007208u

// 148 MHz flash at 444 MHz, sampled as early as it goes. The default until
// Table 1292 made the sum above computable. Superseded by RX4, which runs the
// same SCK and gives the device 1.1 ns more for nothing -- there is no reason
// to choose this one, and it is kept only because the 480 MHz boot-failure
// note above refers to it.
#define PICOFACE_QMI_M0_TIMING_OC 0x60007303u

// 480 MHz target: CLKDIV=4, RXDELAY=3 -> 120 MHz flash. Used by PicoFaceRD,
// whose engine needs the higher core clock. By the budget above this leaves
// 3.29 ns for the device, which is OC's territory rather than CD4's. It is
// hardware-confirmed on the reference board and left alone here; RXDELAY=6
// would cost nothing in throughput and buy 3.1 ns, and is the obvious next
// thing to try if an RD ever fails to boot.
#define PICOFACE_QMI_M0_TIMING_RD 0x60007304u

// The default. Full flash speed with a later sample point: 3.88 ns for the
// device against OC's 2.76, bought by moving RXDELAY and nothing else, so the
// throughput is identical. Still under the 10.0 ns worst case -- this is more
// margin, not enough margin.
#define PICOFACE_QMI_M0_TIMING_RX4 0x60007403u   // CLKDIV=3, RXDELAY=4

// The next rung, and the first value here that satisfies the worst-case sum:
// 6.14 ns for the device. Costs 111 MHz instead of 148, which measured as 1.3
// voices on the D5. RXDELAY=5, not 4: at 4 the budget is 9.01 ns against a
// requirement of 10.0 -- that value was picked against the part's 133 MHz
// rating, before the pad delays made the real sum computable.
#define PICOFACE_QMI_M0_TIMING_CD4 0x60007504u   // CLKDIV=4, RXDELAY=5

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
#define PICOFACE_QMI_M0_TIMING_TARGET PICOFACE_QMI_M0_TIMING_RX4
#endif


#endif // __PROJECT_CONFIG_H__

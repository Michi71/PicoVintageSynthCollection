// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// Waveshare RP2350B-Plus-W: an RP2350B (QFN-80, 48 GPIOs) on a Pico-format
// board with 16 MB flash and Radio Module 2. Pico-compatible 40-pin header,
// with one consequence that matters for this project: the header positions
// where a Pico carries GP26/GP27/GP28 (pins 31/32/34) carry GP40/GP41/GP42
// here, so the RP2350B's ADC0-2 keep the Pico's ADC positions. GP26-GP28 exist
// only on the castellated pads underneath. A Pico-Audio HAT on this board is
// therefore driven from GP40/41/42, which is what the rp2350b_plus_w build
// variant selects. Everything else this project uses (MIDI GP4/5, display
// GP2/3, encoders GP6-15) sits where a Pico has it.
//
// Modelled on the SDK's sparkfun_promicro_rp2350.h / solderparty_rp2350_stamp_xl.h.

#ifndef _BOARDS_WAVESHARE_RP2350B_PLUS_W_H
#define _BOARDS_WAVESHARE_RP2350B_PLUS_W_H

// For board detection
#define WAVESHARE_RP2350B_PLUS_W

// --- RP2350 VARIANT ---
#define PICO_RP2350A 0      // 1 for RP2350A, 0 for RP2350B (48 GPIOs -> PIO GPIO base available)

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// no PICO_DEFAULT_LED_PIN (the on-board LEDs are not on a GPIO this project owns)
// no PICO_DEFAULT_WS2812_PIN

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- SPI ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- FLASH ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif

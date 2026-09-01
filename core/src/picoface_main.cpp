// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/**
 * Shared entry point for every instrument in the collection.
 * Brings up hardware, USB, display, audio pool, encoders and persistence,
 * then runs the audio producer loop. The instrument itself is only ever
 * reached through picoface::instrument().
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#if PICO_RP2350
#include "hardware/structs/qmi.h"
#endif

#include "project_config.h"
#include "pico_hw.h"
#include "veeprom.h"
#include "get_serial.h"
#include "midi_input_usb.h"
#include "midi_output_usb.h"
#include "midi_serial.h"
#include "audio_subsystem.h"
#include "u8g2.h"
#include "encoder.h"
#include "push_button.h"
#include "picoface/instrument.h"

#if __has_include("bsp/board_api.h")
#include "bsp/board_api.h"
#else
#include "bsp/board.h"
#endif

#ifndef PICO_AUDIO_I2S_DMA_IRQ
#define PICO_AUDIO_I2S_DMA_IRQ 0
#endif

Encoder g_encSel(pio1, 0, {PIN_SEL_CLK, PIN_SEL_DT}, PIN_UNUSED, NORMAL_DIR, ROTARY_CPR, false, 444);
Encoder g_encA(pio1, 1, {PIN_PA_CLK, PIN_PA_DT}, PIN_UNUSED, NORMAL_DIR, ROTARY_CPR, false, 444);
Encoder g_encB(pio1, 2, {PIN_PB_CLK, PIN_PB_DT}, PIN_UNUSED, NORMAL_DIR, ROTARY_CPR, false, 444);
PushButton g_btSel(PIN_SEL_SW, 50);
PushButton g_btA(PIN_PA_SW, 50);
PushButton g_btB(PIN_PB_SW, 50);
static audio_buffer_pool_t* g_audio_pool = nullptr;
static u8g2_t g_u8g2;
static MIDIInputUSB g_usbmidi;
static volatile uint32_t g_pio_stall_count = 0;
picoface::ui::Display g_display(&g_u8g2);   // facade, holds only the u8g2 pointer

// 16 = idle, 0..15 = next half tile row. Display::flush() sets this to 0 and
// the main loop pushes the buffer out row by row instead of calling a blocking
// SendBuffer. Deliberately not static: core/src/ui/display.cpp refers to it.
uint8_t picoface_ui_flush_row = 16;

extern "C" void __not_in_flash_func(i2s_callback_func)()
{
    // Rendering happens in the main loop; the IRQ stays deliberately tiny.
    if (!g_audio_pool) return;
    g_pio_stall_count += audio_i2s_consume_txstall();
}

// -----------------------------------------------------------------------------
// MIDI callbacks
// The MIDIInputUSB callbacks deliver the channel number as the LAST argument,
// the picoface instrument interface expects it as the FIRST - swap them here.
// -----------------------------------------------------------------------------
static void pf_note_on(uint8_t n, uint8_t v, uint8_t c)
{
    picoface::instrument().noteOn(c, n, v);
}

static void pf_note_off(uint8_t n, uint8_t v, uint8_t c)
{
    picoface::instrument().noteOff(c, n, v);
}

static void pf_cc(uint8_t cc, uint8_t v, uint8_t c)
{
    picoface::instrument().controlChange(c, cc, v);
}

static void pf_pc(uint8_t p, uint8_t c)
{
    picoface::instrument().programChange(c, p);
}

static void pf_pb(uint16_t bend14, uint8_t c)
{
    // USB delivers 14-bit offset binary 0..16383, the interface wants -8192..8191
    picoface::instrument().pitchBend(c, (int16_t)((int32_t)bend14 - 8192));
}

static void pf_at(uint8_t v, uint8_t c)
{
    picoface::instrument().channelPressure(c, v);
}

static void pf_sysex(const uint8_t* d, uint16_t len)
{
    picoface::instrument().sysEx(d, len);
}

static void pf_realtime(uint8_t status)
{
    picoface::instrument().realtime(status);
}

static void pf_activity(void)
{
    picoface::instrument().midiActivity();
}

// -----------------------------------------------------------------------------
// veeprom lock hooks
// nothing to park - the write runs on core0 between two audio blocks.
// veeprom disables the interrupts itself and restores the QMI flash timing
// afterwards.
// -----------------------------------------------------------------------------
static bool pf_flash_lock(void) { return true; }
static void pf_flash_unlock(void) {}

// -----------------------------------------------------------------------------
// long-press detection and input capture
// -----------------------------------------------------------------------------
static constexpr uint32_t PF_LONG_PRESS_MS = 600;
static uint32_t g_btDownSince[picoface::ui::kButtonCount] = {0, 0, 0};
static bool     g_btLongFired[picoface::ui::kButtonCount] = {false, false, false};

static void pf_build_input(picoface::ui::InputState& in, uint32_t nowMs)
{
    in.nowMs = nowMs;
    // pot is not wired on every board; instruments that use it read it themselves for now
    in.pot = 0;

    PushButton* buttons[3]  = {&g_btSel, &g_btA, &g_btB};
    Encoder*    encoders[3] = {&g_encSel, &g_encA, &g_encB};

    for (int i = 0; i < 3; ++i) {
        in.encoderDelta[i] = (int8_t)encoders[i]->delta();

        bool toggled = buttons[i]->Toggled();
        bool down    = (buttons[i]->ReadButton() == PushButton::PRESSED);

        in.buttonDown[i] = down;
        // Toggled() fires on both edges, so the level is checked too -
        // otherwise releasing would trigger a second event
        in.buttonPressed[i]   = (toggled && down);
        in.buttonLongPress[i] = false;

        if (down && g_btDownSince[i] == 0) {
            g_btDownSince[i] = nowMs;
            g_btLongFired[i] = false;
        }
        if (down && !g_btLongFired[i] && (nowMs - g_btDownSince[i]) >= PF_LONG_PRESS_MS) {
            in.buttonLongPress[i] = true;
            g_btLongFired[i]      = true;
        }
        if (!down) {
            g_btDownSince[i] = 0;
            g_btLongFired[i] = false;
        }
    }
}

// -----------------------------------------------------------------------------
// firmware update: into the UF2 bootloader from the panel
// -----------------------------------------------------------------------------
// Ten firmware images share one board, so choosing a different instrument is
// something a user does, not something a factory does once. That makes the
// route into the bootloader part of the interface, and it belongs here rather
// than in a menu: a menu entry would have to be added to all ten instruments,
// while the core already owns the buttons and the display.
//
// The gesture is all three encoder buttons together. No instrument uses that
// combination -- they read single presses and long presses -- and it cannot be
// reached by resting a hand on one knob. After half a second the core takes the
// screen and counts down, so nothing happens invisibly and letting go cancels.
//
// Two details worth knowing:
//
//   The audio pipeline is drained before the jump. All-sound-off is sent first
//   and the main loop keeps rendering for a further 150 ms, so the five buffers
//   already queued in the DMA play out silent. Jumping straight away would
//   leave whatever was queued mid-note in the DAC as the clock stops.
//
//   reset_usb_boot's first argument is a GPIO mask for a USB activity LED, and
//   passing PIN_LED here would be the obvious thing to do. It is deliberately 0:
//   on RP2350 A2 silicon the SDK works around an activity-LED bug by rebooting
//   into RISC-V mode when a LED is named (see PICO_BOOTROM_WORKAROUND_RP2350_
//   A2_ACTIVITY_LED_BUG), which is not what anyone wants from a firmware
//   update. No LED, no workaround, Arm bootloader.
//
// Settings are NOT written on the way out. A flash write stalls execution and
// this is not a normal shutdown; the debounced save in the main loop has had
// two seconds of idle to run long before anyone holds three buttons down.
static constexpr uint32_t PF_FW_ARM_MS   = 500;    // core takes the screen
static constexpr uint32_t PF_FW_HOLD_MS  = 2500;   // committed
static constexpr uint32_t PF_FW_DRAIN_MS = 150;    // silence reaches the DAC
static uint32_t g_fwHoldSince = 0;
static uint32_t g_fwCommitted = 0;
static int32_t  g_fwShown     = -1;                // last second drawn

// Returns true while the core owns the display, so the caller skips uiTick.
static bool pf_firmware_update(picoface::Instrument& inst,
                               const picoface::ui::InputState& in,
                               uint32_t nowMs)
{
    if (g_fwCommitted) {
        if ((nowMs - g_fwCommitted) >= PF_FW_DRAIN_MS) reset_usb_boot(0, 0);
        return true;                                  // never returns from that
    }

    const bool all = in.buttonDown[0] && in.buttonDown[1] && in.buttonDown[2];
    if (!all) { g_fwHoldSince = 0; g_fwShown = -1; return false; }
    if (g_fwHoldSince == 0) { g_fwHoldSince = nowMs; return false; }

    const uint32_t held = nowMs - g_fwHoldSince;
    if (held < PF_FW_ARM_MS) return false;

    if (held < PF_FW_HOLD_MS) {
        // Redraw only when the digit changes -- three times, not once every
        // 20 ms. The countdown goes out through the incremental flush like
        // everything else, because a blocking full-buffer transfer costs more
        // than the audio lead has (see section 2 of the main loop) and two and
        // a half seconds of stutter is a poor thing to hand someone who is
        // about to let go and cancel.
        const int32_t left = (int32_t)((PF_FW_HOLD_MS - held + 999) / 1000);
        if (left != g_fwShown) {
            g_fwShown = left;
            char line[16];
            snprintf(line, sizeof line, "%ld", (long)left);
            g_display.clear();
            g_display.setFont(u8g2_font_7x13B_tf);
            g_display.drawTextCentered(20, "FIRMWARE");
            g_display.drawTextCentered(42, line);
            g_display.setFont(u8g2_font_6x10_tf);
            g_display.drawTextCentered(58, "let go to cancel");
            g_display.flush();
        }
        return true;
    }

    // Committed: silence every channel, say so, and let the loop drain the
    // queued buffers. Here the blocking transfer is right -- the audio is
    // deliberately over, and the message has to be on the glass before the
    // chip resets.
    for (uint8_t ch = 0; ch < 16; ++ch) inst.controlChange(ch, 120, 0);
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_7x13B_tf);
    u8g2_DrawStr(&g_u8g2, (128 - u8g2_GetStrWidth(&g_u8g2, "BOOTLOADER")) / 2, 28, "BOOTLOADER");
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&g_u8g2, (128 - u8g2_GetStrWidth(&g_u8g2, "drop a .uf2 on it")) / 2, 46,
                 "drop a .uf2 on it");
    u8g2_SendBuffer(&g_u8g2);
    g_fwCommitted = nowMs;
    return true;
}

int main(void)
{
    // bring up the chip basics before anything else
    pico_init();

    // grab the singleton instrument
    picoface::Instrument& inst = picoface::instrument();

    // Installed before init(), so an instrument may already use the veeprom
    // while starting up.
    veeprom_set_lock_hooks(pf_flash_lock, pf_flash_unlock);
    // Before the scan: it decides which records this instrument may see at all.
    veeprom_set_instrument(inst.name());
    veeprom_init();

    inst.init();   // engine first - it determines its own sample rate

    // board, serial and USB stack
    board_init();
    usb_serial_init();
    tusb_init();

    // display setup (SH1106, 128x64, I2C)
    u8g2_Setup_sh1106_i2c_128x64_noname_f(&g_u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);
    u8g2_ClearBuffer(&g_u8g2);

    // splash screen: instrument name and firmware version, both centered
    const char* name = inst.name();
    u8g2_SetFont(&g_u8g2, u8g2_font_7x13B_tf);
    u8g2_DrawStr(&g_u8g2, (128 - u8g2_GetStrWidth(&g_u8g2, name)) / 2, 28, name);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&g_u8g2, (128 - u8g2_GetStrWidth(&g_u8g2, PICOFACE_VERSION)) / 2, 44, PICOFACE_VERSION);

    // Bottom line: which silicon this is, and the flash clock actually in force.
    // Both come up in bug reports (issue #107 turned on exactly this pair) and
    // neither is otherwise readable: picotool does not report the stepping, and
    // the flash timing is a compile-time choice with a runtime fallback -- if
    // set_sys_clock_hz() misses its target the boot keeps the slack timing, so
    // only the register knows what is really running. Read it, do not assume it.
#if PICO_RP2350
    {
        // CLKDIV encodes the SCK period in system clock cycles; 0 means 256.
        const uint32_t clkdivRaw = qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS;
        const uint32_t clkdiv = clkdivRaw ? clkdivRaw : 256u;
        const uint32_t sysHz    = clock_get_hz(clk_sys);
        const uint32_t coreMHz  = (sysHz + 500000u) / 1000000u;
        const uint32_t flashMHz = (sysHz / clkdiv + 500000u) / 1000000u;
        char hw[32];
        // Q or D/S: whether the bootrom got the flash into quad mode. On a
        // board where it did not, the whole fast-timing path is skipped and
        // the flash keeps the bootrom's own (much slower) clock -- so this
        // letter and that number together say why an instrument feels slow.
        // RXDELAY too: it is the other half of the flash timing, it is the one
        // parameter two builds can differ in while showing the same SCK, and
        // without it an A/B of two timings is unreadable at the device.
        const uint32_t rxdelay =
            (qmi_hw->m[0].timing & QMI_M0_TIMING_RXDELAY_BITS) >> QMI_M0_TIMING_RXDELAY_LSB;
        // The core clock leads: it is what set_sys_clock_hz() may silently have
        // failed to reach (the flash figure is derived from it and would then
        // be misread as a flash problem), and it is the parameter the #107
        // board is suspected on. Reading "core/flash" also makes a set of
        // images that differ only in clock target tellable apart at the device.
        snprintf(hw, sizeof hw, "A%u %c %u/%u r%u", (unsigned)rp2350_chip_version(),
                 picoface_flash_is_quad ? 'Q' : 'D', (unsigned)coreMHz,
                 (unsigned)flashMHz, (unsigned)rxdelay);
        u8g2_SetFont(&g_u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(&g_u8g2, (128 - u8g2_GetStrWidth(&g_u8g2, hw)) / 2, 60, hw);
    }
#endif
    u8g2_SendBuffer(&g_u8g2);   // blocking is fine here, audio is not running yet

    // hold the splash for 2000 ms, keeping USB alive
    const absolute_time_t splash_end = make_timeout_time_ms(2000);
    while (!time_reached(splash_end)) {
        tud_task();
        sleep_ms(1);
    }

    // buttons and encoders
    g_btSel.Init();
    g_btA.Init();
    g_btB.Init();
    g_encSel.init();
    g_encA.init();
    g_encB.init();

    // wire up all MIDI callbacks
    g_usbmidi.setNoteOnCallback(pf_note_on);
    g_usbmidi.setNoteOffCallback(pf_note_off);
    g_usbmidi.setCCCallback(pf_cc);
    g_usbmidi.setProgramChangeCallback(pf_pc);
    g_usbmidi.setPitchBendCallback(pf_pb);
    g_usbmidi.setChannelPressureCallback(pf_at);
    g_usbmidi.setSysExCallback(pf_sysex);
    g_usbmidi.setRealtimeCallback(pf_realtime);
    g_usbmidi.setActivityCallback(pf_activity);

    // DIN MIDI onto the very same dispatch functions - an instrument does
    // not care which wire an event arrived on.
    midiSerial().init();
    midiSerial().setNoteOnCallback(pf_note_on);
    midiSerial().setNoteOffCallback(pf_note_off);
    midiSerial().setCCCallback(pf_cc);
    midiSerial().setProgramChangeCallback(pf_pc);
    midiSerial().setPitchBendCallback(pf_pb);
    midiSerial().setChannelPressureCallback(pf_at);
    midiSerial().setSysExCallback(pf_sysex);
    midiSerial().setRealtimeCallback(pf_realtime);
    midiSerial().setActivityCallback(pf_activity);

    // audio output at the engine's own rate
    g_audio_pool = init_audio(inst.sampleRate(), 6);

    irq_set_priority(DMA_IRQ_0 + PICO_AUDIO_I2S_DMA_IRQ, 0x00);
    irq_set_priority(USBCTRL_IRQ, 0xC0);   // audio DMA must outrank USB

    // Restore persisted settings. An instrument that writes its own veeprom
    // record reports size 0 and restores its state itself.
    {
        const size_t need = inst.settingsSize();
        if (need > 0 && need <= VEEPROM_MAX_PAYLOAD) {
            // static buffer - VEEPROM_MAX_PAYLOAD on the stack would be wasteful here
            static uint8_t buf[VEEPROM_MAX_PAYLOAD];
            uint16_t len = 0, ver = 0;
            if (veeprom_load(buf, sizeof(buf), &len, &ver) && ver == inst.settingsVersion() && len >= need)
                inst.settingsLoad(buf, len);
        }
    }

    // hand the display over to the instrument UI
    inst.uiInit(g_display);

    picoface::ui::InputState input{};
    uint32_t last_ui_ms = 0;
    uint32_t last_edit_ms = 0;
    bool settings_dirty = false;

    while (true) {
        // 1. Audio producer in thread context - interruptible by every IRQ
        // -1 = idle, otherwise the number of buffers still to drain
        static int rateDrain = -1;
        audio_buffer_t* buffer;
        while ((buffer = take_audio_buffer(g_audio_pool, false)) != nullptr) {
            inst.render((int32_t*) buffer->buffer->bytes, buffer->max_sample_count);
            buffer->sample_count = buffer->max_sample_count;
            give_audio_buffer(g_audio_pool, buffer);

            // The core must not switch the hardware rate immediately: the roughly
            // five buffers already queued in the DMA pipeline were rendered at the
            // old rate and would otherwise play back 10-15 ms too fast or too slow.
            // So we count down buffer_count-1 further buffers before actually
            // applying the new rate.
            if (rateDrain > 0) {
                if (--rateDrain == 0) {
                    audio_set_sample_freq(inst.sampleRate());
                    rateDrain = -1;
                }
            }
            // A new signal while a countdown is still running simply re-arms it.
            if (inst.consumeSampleRateChange()) rateDrain = 5;
        }

        // 2. Incremental display flush
        // Half tile rows, roughly 1.5 ms of I2C each, so the audio lead does not collapse.
        //
        // Stands aside while MIDI still has bytes queued. The transmit queue is
        // topped up once per pass, so a 1.5 ms I2C block between two passes
        // paces a SysEx dump at roughly a TinyUSB FIFO per 1.5 ms: a 241-byte
        // voice takes about six of those, some 9 ms, where USB itself would be
        // done in four 1 ms frames. An editor reading a voice sees the gaps; the
        // display catches up a few milliseconds later and nobody sees that.
        if (picoface_ui_flush_row < 16 && usbMidiOut().empty()) {
            u8g2_UpdateDisplayArea(&g_u8g2, (picoface_ui_flush_row & 1) ? 8 : 0, (uint8_t)(picoface_ui_flush_row >> 1), 8, 1);
            picoface_ui_flush_row++;
        }

        // 3. USB and DIN MIDI
        // Drain the transmit queue right after tud_task(): that is the point at
        // which the completion callback has freed the endpoint, so the TinyUSB
        // FIFO has room again. A reface DX voice dump is several USB frames
        // long and leaves over as many iterations as it needs.
        tud_task();
        usbMidiOut().process();
        g_usbmidi.process();
        midiSerial().process();

        // Report I2S underruns to the instrument so it can shed load.
        // (g_i2s_underrun_count comes from audio_i2s.h via audio_subsystem.h)
        static uint32_t last_underrun = 0;
        if (g_i2s_underrun_count > last_underrun) {
            last_underrun = g_i2s_underrun_count;
            inst.onAudioUnderrun();
        }

        const uint32_t now = to_ms_since_boot(get_absolute_time());

        // 4. UI tick, at most every 20 ms and only while the display is not being pushed out
        // uiTick draws into the u8g2 buffer only; Display::flush() just arms the incremental push.
        if (picoface_ui_flush_row >= 16 && (now - last_ui_ms) >= 20) {
            pf_build_input(input, now);
            const bool edited = input.encoderDelta[0] || input.encoderDelta[1] || input.encoderDelta[2] || input.buttonPressed[0] || input.buttonPressed[1] || input.buttonPressed[2];
            // The three-button gesture owns the screen while it is held, so the
            // instrument does not draw over the countdown.
            if (!pf_firmware_update(inst, input, now)) inst.uiTick(g_display, input);
            last_ui_ms = now;
            if (edited) { settings_dirty = true; last_edit_ms = now; }
        }

        // 5. Debounced settings save while idle, 2000 ms after the last operation
        // Only encoder and button operation marks the state dirty - changes arriving over MIDI stay deliberately transient.
        // An instrument can postpone the write, e.g. while notes are still sounding.
        static uint8_t save_retries = 0;
        if (settings_dirty && (now - last_edit_ms) > 2000u && inst.settingsSaveAllowed()) {
            const size_t need = inst.settingsSize();
            if (need > 0 && need <= VEEPROM_MAX_PAYLOAD) {
                static uint8_t buf[VEEPROM_MAX_PAYLOAD];
                inst.settingsSave(buf, need);
                if (veeprom_save(buf, (uint16_t) need, inst.settingsVersion())) {
                    settings_dirty = false;
                    save_retries = 0;
                } else {
                    // A failed write must not drop the edit; the failed slot is
                    // non-blank, so the veeprom sector-jump rule routes the retry
                    // to the other sector. Giving up after three attempts avoids
                    // hammering bad flash every two seconds.
                    if (++save_retries >= 3u) {
                        settings_dirty = false;
                        save_retries = 0;
                    } else {
                        last_edit_ms = now; // retry after another 2000 ms
                    }
                }
            } else {
                settings_dirty = false;
            }
            // Swallow the save-induced underrun delta so the instrument's
            // underrun hook does not fire on the next pass.
            last_underrun = g_i2s_underrun_count;
        }
    }

    return 0;
}

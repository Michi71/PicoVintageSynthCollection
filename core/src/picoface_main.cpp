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

int main(void)
{
    // bring up the chip basics before anything else
    pico_init();

    // grab the singleton instrument
    picoface::Instrument& inst = picoface::instrument();

    // Installed before init(), so an instrument may already use the veeprom
    // while starting up.
    veeprom_set_lock_hooks(pf_flash_lock, pf_flash_unlock);
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
            inst.uiTick(g_display, input);
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

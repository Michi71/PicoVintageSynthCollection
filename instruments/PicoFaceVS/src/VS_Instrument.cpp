// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// VS_Instrument.cpp - adapter binding the provs engine (Sequential Prophet VS)
// to picoface::Instrument. Standard runtime model: core0 does audio, USB, MIDI
// and GUI, core1 is unused.
//
// MIDI dispatch, uiTick() and render() are all core0 main-loop steps and never
// overlap, so MIDI and the panel call the engine directly; there is no ring.
// The panel, the edit buffer and the dumps are VS_Controller's; this file is
// what needs the Pico SDK.

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/timer.h"

#include "picoface/instrument.h"
#include "picoface/ui_kit.h"

#include "project_config.h"    // PIN_LED
#include "audio_subsystem.h"   // SAMPLES_PER_BUFFER
#include "midi_output_usb.h"
#include "midi_serial.h"

#include "VS_Controller.h"
#include "provs/engine.h"
#include "vs_banks.h"          // generated: the user's .syx files, or empty
#include "vs_store.h"

// MIDI output for VS_Controller and VS_Dump: both wires, paced by whichever ring is
// fuller. A host that does not read the USB endpoint (see
// MIDIOutputUSB::stalled) must not hold DIN back.
size_t vs_sysex_room() {
    size_t room = midiSerial().txFree();
    if (!usbMidiOut().stalled()) {
        const size_t u = usbMidiOut().freeSpace();
        if (u < room) room = u;
    }
    return room;
}

void vs_sysex_write(const uint8_t* data, size_t len) {
    usbMidiOut().write(data, (uint16_t)len);
    midiSerial().write(data, (uint16_t)len);
}

namespace {

// The engine is rate independent. On the hardware it peaked at 53 % load at
// 32 kHz, so 44.1 kHz should peak near 73 %; the DIAG page shows the load.
constexpr uint32_t kSampleRate = 44100;

// Longest SysEx this instrument takes: a Prophet VS bank dump
// (F0 01 0A 64, 100 x 164 nibbles, F7). The dump of all user waves is shorter
// (F0 01 0A 7F, 32 x 384 nibbles, F7: 12293 bytes).
constexpr size_t kSysExMax = 16405;

class VSInstrument final : public picoface::Instrument {
public:
    VSInstrument() : controller_(engine_, store_) {}

    const char* name() const override { return "PicoFaceVS"; }

    void init() override {
        engine_.init((double) kSampleRate);
        controller_.init(kVsEmbeddedBanks, kVsEmbeddedBankSize);
    }

    uint32_t sampleRate() const override { return kSampleRate; }

    void render(int32_t* out, uint32_t frames) override {
        const uint32_t t0 = time_us_32();

        static float buf[2 * SAMPLES_PER_BUFFER];
        uint32_t done = 0;
        while (done < frames) {
            uint32_t n = frames - done;
            if (n > SAMPLES_PER_BUFFER) n = SAMPLES_PER_BUFFER;
            engine_.render(buf, (int) n);
            for (uint32_t i = 0; i < 2 * n; ++i) {
                float s = buf[i];
                if (s < -1.f) s = -1.f; else if (s > 1.f) s = 1.f;
                out[done * 2 + i] = ((int32_t)(s * 32767.f)) << 16;
            }
            done += n;
        }

        const uint32_t us = time_us_32() - t0;
        controller_.setLoad((float) us * 100.f * (float) kSampleRate / ((float) frames * 1000000.f));
    }

    // A flash write stalls the CPU for milliseconds, which is audible.
    bool settingsSaveAllowed() const override { return engine_.activeVoices() == 0; }

    // ----------------------------------------------------------------
    // MIDI
    // ----------------------------------------------------------------

    // What a channel means is the controller's business (MIDI mode, right
    // and left channel); the LED shows a note that was for this instrument.
    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override {
        if (vel == 0) { noteOff(ch, note, 0); return; }
        if (controller_.noteOn(ch, note, vel)) gpio_put(PIN_LED, 1);
    }

    void noteOff(uint8_t ch, uint8_t note, uint8_t) override {
        if (controller_.noteOff(ch, note)) gpio_put(PIN_LED, 0);
    }

    void controlChange(uint8_t ch, uint8_t cc, uint8_t value) override {
        controller_.controlChange(ch, cc, value);
        dirty_ = true;
    }

    void pitchBend(uint8_t ch, int16_t bend) override { controller_.pitchBend(ch, bend); }

    void channelPressure(uint8_t ch, uint8_t value) override { controller_.pressure(ch, value); }

    void programChange(uint8_t ch, uint8_t program) override {
        controller_.programChange(ch, program);
        dirty_ = true;
    }

    // MIDI clock steps the arpeggiator when its clock is set to MIDI.
    void realtime(uint8_t status) override {
        if (status == 0xF8) controller_.midiClock();
    }

    // Bank dumps do not fit the core's 512-byte SysEx buffer, so take the
    // stream and hand over each complete message.
    bool wantsSysExStream() const override { return true; }

    void sysExStream(const uint8_t* data, size_t length) override {
        for (size_t i = 0; i < length; ++i) {
            const uint8_t b = data[i];
            if (b == 0xF0) { sysexLen_ = 0; sysexOpen_ = true; }
            if (!sysexOpen_) continue;
            if (sysexLen_ < sizeof(sysex_)) sysex_[sysexLen_++] = b;
            else { sysexOpen_ = false; continue; }             // too long: drop it
            if (b == 0xF7) {
                sysexOpen_ = false;
                controller_.sysExMessage(sysex_, sysexLen_);
                dirty_ = true;
            }
        }
    }

    // ----------------------------------------------------------------
    // GUI
    // ----------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override { marquee_ = controller_.draw(d, 0); }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        if (controller_.poll(in.nowMs)) dirty_ = true;
        if (controller_.input(in)) dirty_ = true;

        // Redraw after input, and twice a second for the figures that move.
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u) {
            marquee_ = controller_.draw(d, in.nowMs);
            dirty_ = false;
            lastDrawMs_ = lastMarqueeMs_ = in.nowMs;
        } else if (marquee_ && (in.nowMs - lastMarqueeMs_) >= 40u) {
            marquee_ = picoface::ui::kit::marqueeTick(d, in.nowMs);
            lastMarqueeMs_ = in.nowMs;
        }
    }

    // ----------------------------------------------------------------
    // Persistence: the panel state. The programs have their own store.
    // ----------------------------------------------------------------

    uint16_t settingsVersion() const override { return kVsSettingsVersion; }
    size_t settingsSize() const override { return sizeof(VsSettingsV4); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(VsSettingsV4)) return;
        VsSettingsV4 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(VsSettingsV4)) return;
        VsSettingsV4 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
        dirty_ = true;
    }

private:
    provs::Engine engine_;
    vs::Store store_;
    VS_Controller controller_;
    bool     dirty_    = true;
    bool     marquee_  = false;
    uint32_t lastDrawMs_ = 0, lastMarqueeMs_ = 0;

    uint8_t  sysex_[kSysExMax];
    size_t   sysexLen_  = 0;
    bool     sysexOpen_ = false;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(VSInstrument)

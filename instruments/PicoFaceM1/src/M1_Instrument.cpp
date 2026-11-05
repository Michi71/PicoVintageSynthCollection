// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// M1_Instrument.cpp -- adapter binding the M1 classes to picoface::Instrument.
// Everything runs on core0, as on the JV: sixteen voices at 31.25 kHz plus
// the two effects stay well inside one core, and each voice reads its sample
// sequentially, so neither the core1 worker nor a raised clock applies.

#include <cstdio>
#include <cstring>

#include "picoface/instrument.h"

#include "M1_Bridge.h"
#include "M1_Controller.h"
#include "M1_Midi.h"
#include "M1_SysEx.h"
#include "audio_i2s.h"   // g_i2s_underrun_count
#include "m1_params.h"
#include "m1_rom_tables.h"
#include "m1_settings.h"
#include "midi_output_usb.h"
#include "midi_serial.h"
#include "picoface/ui_kit.h"

// SysEx output for M1_SysEx: both wires, paced by whichever ring is fuller.
// A host that does not read the USB endpoint (see MIDIOutputUSB::stalled)
// must not hold DIN back.
size_t m1_sysex_room() {
    size_t room = midiSerial().txFree();
    if (!usbMidiOut().stalled()) {
        const size_t u = usbMidiOut().freeSpace();
        if (u < room) room = u;
    }
    return room;
}

void m1_sysex_write(const uint8_t* data, size_t len) {
    usbMidiOut().write(data, (uint16_t)len);
    midiSerial().write(data, (uint16_t)len);
}

namespace {

class M1Instrument final : public picoface::Instrument {
public:
    M1Instrument() : controller_(bridge_), midi_(bridge_, controller_), sysex_(controller_) {}

    const char* name() const override { return "PicoFaceM1"; }

    void init() override {
        bridge_.init();
        m1::setNameSources(m1rom::kMultisoundNames, m1rom::kDrumNames,
                           bridge_.memory().image().programs);
        controller_.setSysEx(&sysex_);
        M1SettingsV1 defaults{};
        controller_.exportSettings(defaults);
        controller_.importSettings(defaults);   // pushes them into the bridge
    }

    uint32_t sampleRate() const override { return bridge_.sampleRate(); }

    void render(int32_t* out, uint32_t frames) override {
        bridge_.fillBufferI32(out, (int)frames);
    }

    // An underrun sheds voices for a while; recovery in uiTick().
    void onAudioUnderrun() override {
        const int n = bridge_.voiceLimit();
        if (n > kVoiceFloor) bridge_.setVoiceLimit(n - 2);
    }

    // A flash write stalls the CPU for milliseconds, which is audible.
    bool settingsSaveAllowed() const override { return bridge_.activeVoices() == 0; }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOn(ch, note, vel); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t) override { midi_.onNoteOff(ch, note); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { midi_.onControlChange(ch, cc, v); }
    void pitchBend(uint8_t ch, int16_t bend) override { midi_.onPitchBend(ch, bend); }
    void channelPressure(uint8_t ch, uint8_t v) override { midi_.onChannelPressure(ch, v); }
    void programChange(uint8_t ch, uint8_t p) override { midi_.onProgramChange(ch, p); dirty_ = true; }

    // Bank dumps are 14-50 KB: take SysEx as a stream.
    bool wantsSysExStream() const override { return true; }
    void sysExStream(const uint8_t* d, size_t n) override {
        sysex_.feed(d, n);
        dirty_ = true;
    }

    // ---------------------------------------------------------------- GUI
    void uiInit(picoface::ui::Display& d) override { marquee_ = controller_.draw(d, 0); }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        sysex_.poll();
        // Voice governor recovery, one voice per quiet second.
        if (g_i2s_underrun_count != lastUnderrun_) {
            lastUnderrun_ = g_i2s_underrun_count;
            lastUnderrunMs_ = in.nowMs;
        } else if (bridge_.voiceLimit() < m1::kVoices && (in.nowMs - lastUnderrunMs_) > 1000u) {
            bridge_.setVoiceLimit(bridge_.voiceLimit() + 1);
            lastUnderrunMs_ = in.nowMs;
        }
        // The memory mirror: written once things have been quiet for a while
        // and nothing sounds, since the erase stops the CPU.
        uint32_t changed = 0;
        if (controller_.memoryDirtySince(changed) && (in.nowMs - changed) > 1500u &&
            bridge_.activeVoices() == 0 && !sysex_.sending()) {
            bridge_.memory().store();
            dirty_ = true;
        }

        if (controller_.input(in)) dirty_ = true;
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u) {
            marquee_ = controller_.draw(d, in.nowMs);
            dirty_ = false;
            lastDrawMs_ = in.nowMs;
        } else if (marquee_ && (in.nowMs - lastMarqueeMs_) >= 40u) {
            marquee_ = picoface::ui::kit::marqueeTick(d, in.nowMs);
            lastMarqueeMs_ = in.nowMs;
        }
    }

    // -------------------------------------------------------- Persistence
    uint16_t settingsVersion() const override { return M1_SETTINGS_VERSION; }
    size_t settingsSize() const override { return sizeof(M1SettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(M1SettingsV1)) return;
        M1SettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(M1SettingsV1)) return;
        M1SettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
        dirty_ = true;
    }

private:
    static constexpr int kVoiceFloor = 4;

    M1_Bridge bridge_;
    M1_Controller controller_;
    M1_Midi midi_;
    M1_SysEx sysex_;
    bool dirty_ = true;
    uint32_t lastDrawMs_ = 0;
    bool marquee_ = false;
    uint32_t lastMarqueeMs_ = 0;
    uint32_t lastUnderrun_ = 0;
    uint32_t lastUnderrunMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(M1Instrument)

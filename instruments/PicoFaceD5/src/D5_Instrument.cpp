// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// D5_Instrument.cpp -- adapter binding the D-50 classes to
// picoface::Instrument. Everything runs on core0: the engine renders sixteen
// voices of two partials each without a core1 worker, so the optional hooks
// stay at their defaults except for the underrun governor.

#include <cstdio>
#include <cstring>

#include "picoface/instrument.h"

#include "D5_Bridge.h"
#include "D5_Controller.h"
#include "D5_Display.h"
#include "D5_Midi.h"
#include "d5_settings.h"
#include "audio_i2s.h"   // g_i2s_underrun_count, for the diagnostics footer

namespace {

class D5Instrument final : public picoface::Instrument {
public:
    D5Instrument() : controller_(bridge_), midi_(bridge_, controller_) {}

    const char* name() const override { return "PicoFaceD5"; }

    void init() override {
        bridge_.init();
        benchPct_ = bridge_.bootBenchPercent();
        D5SettingsV1 defaults{};
        controller_.exportSettings(defaults);
        controller_.importSettings(defaults);   // pushes them into the bridge
    }

    uint32_t sampleRate() const override { return bridge_.sampleRate(); }

    void render(int32_t* out, uint32_t frames) override {
        bridge_.fillBufferI32(out, (int)frames);
    }

    // Shedding voices on an underrun has to be temporary: a version that only
    // ever lowered the cap would ratchet down to the floor after one overload
    // and stay there. Recovery happens in uiTick().
    void onAudioUnderrun() override {
        const int n = bridge_.voiceLimit();
        if (n > kVoiceFloor) bridge_.setVoiceLimit(n - 1);
    }

    // A flash write stalls the CPU for milliseconds, which is audible.
    bool settingsSaveAllowed() const override { return bridge_.activeVoices() == 0; }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOn(ch, note, vel); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t) override { midi_.onNoteOff(ch, note); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { midi_.onControlChange(ch, cc, v); }
    void pitchBend(uint8_t ch, int16_t bend) override { midi_.onPitchBend(ch, bend); }
    void programChange(uint8_t, uint8_t p) override {
        D5SettingsV1 s{};
        controller_.exportSettings(s);
        s.patch = (uint8_t)(p % bridge_.patchCount());
        controller_.importSettings(s);
        dirty_ = true;
    }

    // ---------------------------------------------------------------- GUI
    void uiInit(picoface::ui::Display& d) override { draw(d); }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        using picoface::ui::Encoder;
        const int8_t ds = in.delta(Encoder::Sel);
        const int8_t da = in.delta(Encoder::ParamA);
        const int8_t db = in.delta(Encoder::ParamB);

        // Voice governor recovery: one voice back per second of quiet, so a
        // passing overload does not cost polyphony for the rest of the
        // session. Deliberately slower than the cut, to avoid pumping.
        if (g_i2s_underrun_count != lastUnderrun_) {
            lastUnderrun_ = g_i2s_underrun_count;
            lastUnderrunMs_ = in.nowMs;
        } else if (bridge_.voiceLimit() < d5::kMaxVoicesPerTone &&
                   (in.nowMs - lastUnderrunMs_) > 1000u) {
            bridge_.setVoiceLimit(bridge_.voiceLimit() + 1);
            lastUnderrunMs_ = in.nowMs;
            dirty_ = true;
        }

        if (ds) { controller_.onEncoderSel(ds); dirty_ = true; }
        if (da) { controller_.onEncoderA(da); dirty_ = true; }
        if (db) { controller_.onEncoderB(db); dirty_ = true; }
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u) {
            draw(d);
            dirty_ = false;
            lastDrawMs_ = in.nowMs;
        }
    }

    // -------------------------------------------------------- Persistence
    uint16_t settingsVersion() const override { return D5_SETTINGS_VERSION; }
    size_t settingsSize() const override { return sizeof(D5SettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(D5SettingsV1)) return;
        D5SettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(D5SettingsV1)) return;   // short record: keep defaults
        D5SettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
        dirty_ = true;
    }

private:
    void draw(picoface::ui::Display& d) {
        D5UiModel m{};
        snprintf(m.title, sizeof m.title, "%s", controller_.title());
        snprintf(m.page, sizeof m.page, "%s", controller_.pageName());
        controller_.lineA(m.lineA, sizeof m.lineA);
        controller_.lineB(m.lineB, sizeof m.lineB);
        snprintf(m.footer, sizeof m.footer, "P%d B%d U%lu A%d/%d N%lu",
                 bridge_.cpuLoadPeakPercent(), benchPct_,
                 (unsigned long)(g_i2s_underrun_count > 999 ? 999 : g_i2s_underrun_count),
                 bridge_.activeVoices(), bridge_.voiceLimit(),
                 (unsigned long)(bridge_.noteOnTotal() % 1000u));
        d5_display_page(d, m);
        // Arms the incremental push: the main loop only streams the buffer
        // while picoface_ui_flush_row < 16, and flush() resets that counter.
        d.flush();
    }

    static constexpr int kVoiceFloor = 2;
    int benchPct_ = 0;

    D5_Bridge bridge_;
    D5_Controller controller_;
    D5_Midi midi_;
    bool dirty_ = true;
    uint32_t lastDrawMs_ = 0;
    uint32_t lastUnderrun_ = 0;
    uint32_t lastUnderrunMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(D5Instrument)

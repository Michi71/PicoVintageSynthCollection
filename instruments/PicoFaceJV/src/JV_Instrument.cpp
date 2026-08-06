// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// JV_Instrument.cpp -- adapter binding the JV-880 classes to
// picoface::Instrument. Everything runs on core0: the engine costs roughly
// 55 M cycles/s at full polyphony and decodes each voice sequentially, so
// neither the core1 worker nor the raised clock that PicoFaceRD needs applies
// here. The optional interface hooks are therefore left at their defaults,
// except for the underrun handler.

#include <cstdio>
#include <cstring>

#include "picoface/instrument.h"

#include "JV_Bridge.h"
#include "JV_Controller.h"
#include "JV_Display.h"
#include "JV_Midi.h"
#include "jv_settings.h"
#include "audio_i2s.h"   // g_i2s_underrun_count, for the diagnostics footer

namespace {

class JVInstrument final : public picoface::Instrument {
public:
    JVInstrument() : controller_(bridge_), midi_(bridge_, controller_) {}

    const char* name() const override { return "PicoFaceJV"; }

    void init() override {
        bridge_.init();
        JvSettingsV1 defaults{};
        controller_.exportSettings(defaults);
        controller_.importSettings(defaults);   // pushes them into the bridge
    }

    uint32_t sampleRate() const override { return bridge_.sampleRate(); }

    void render(int32_t* out, uint32_t frames) override {
        bridge_.fillBufferI32(out, (int)frames);
    }

    // An underrun means polyphony outran the CPU. Shed two voices; the user can
    // raise the cap again on the VOICES page.
    void onAudioUnderrun() override {
        const int n = bridge_.voiceLimit();
        if (n > 4) bridge_.setVoiceLimit(n - 2);
    }

    // A flash write stalls the CPU for milliseconds, which is audible.
    bool settingsSaveAllowed() const override { return bridge_.activeVoices() == 0; }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOn(ch, note, vel); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t) override { midi_.onNoteOff(ch, note); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { midi_.onControlChange(ch, cc, v); }
    void pitchBend(uint8_t ch, int16_t bend) override { midi_.onPitchBend(ch, bend); }
    void programChange(uint8_t ch, uint8_t p) override {
        if (p < 64) { JvSettingsV1 s{}; controller_.exportSettings(s); s.patch = p;
                      controller_.importSettings(s); dirty_ = true; }
    }

    // ---------------------------------------------------------------- GUI
    void uiInit(picoface::ui::Display& d) override { draw(d); }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        using picoface::ui::Encoder;
        const int8_t ds = in.delta(Encoder::Sel);
        const int8_t da = in.delta(Encoder::ParamA);
        const int8_t db = in.delta(Encoder::ParamB);
        if (ds) { controller_.onEncoderSel(ds); dirty_ = true; }
        if (da) { controller_.onEncoderA(da); dirty_ = true; }
        if (db) { controller_.onEncoderB(db); dirty_ = true; }
        // Redraw soon after input, and at least twice a second as a keep-alive
        // so the footer's live counters stay current.
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u) {
            draw(d);
            dirty_ = false;
            lastDrawMs_ = in.nowMs;
        }
    }

    // -------------------------------------------------------- Persistence
    uint16_t settingsVersion() const override { return JV_SETTINGS_VERSION; }
    size_t settingsSize() const override { return sizeof(JvSettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(JvSettingsV1)) return;
        JvSettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(JvSettingsV1)) return;   // short record: keep defaults
        JvSettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
        dirty_ = true;
    }

private:
    void draw(picoface::ui::Display& d) {
        JvUiModel m{};
        snprintf(m.title, sizeof m.title, "%s", controller_.title());
        snprintf(m.page, sizeof m.page, "%s", controller_.pageName());
        controller_.lineA(m.lineA, sizeof m.lineA);
        controller_.lineB(m.lineB, sizeof m.lineB);
        snprintf(m.footer, sizeof m.footer, "U%lu A%d/%d",
                 (unsigned long)g_i2s_underrun_count,
                 bridge_.activeVoices(), bridge_.voiceLimit());
        jv_display_page(d, m);
        // Arms the incremental push. Painting the buffer is not enough: the main
        // loop only streams it out while picoface_ui_flush_row < 16, and flush()
        // is what resets that counter. Without this the display keeps showing
        // whatever was pushed last -- the boot splash -- while everything else
        // runs normally.
        d.flush();
    }

    JV_Bridge bridge_;
    JV_Controller controller_;
    JV_Midi midi_;
    bool dirty_ = true;
    uint32_t lastDrawMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(JVInstrument)

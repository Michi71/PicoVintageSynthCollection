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
#include "picoface/ui_kit.h"
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

    // An underrun means polyphony outran the CPU, so shed voices -- but the cut
    // has to be temporary. An earlier version only ever lowered the cap, so a
    // brief overload ratcheted it down to the floor and left it there: every
    // note after that stole a voice, which chops the sound in a way that is
    // easy to mistake for bad loop points. Recovery happens in uiTick().
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
    // Program change reaches all 192 patches, since the latched bank select
    // decides which memory it lands in -- see JV_Midi::resolveProgram.
    void programChange(uint8_t ch, uint8_t p) override {
        int bank = 0, index = 0;
        if (!midi_.resolveProgram(p, bank, index)) return;
        JvSettingsV1 s{};
        controller_.exportSettings(s);
        s.bank = (uint8_t)bank;
        s.patch = (uint8_t)index;
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
        // passing overload does not cost polyphony for the rest of the session.
        // Deliberately slower than the cut, to avoid pumping around the limit.
        if (g_i2s_underrun_count != lastUnderrun_) {
            lastUnderrun_ = g_i2s_underrun_count;
            lastUnderrunMs_ = in.nowMs;
        } else if (bridge_.voiceLimit() < jv::kMaxVoices &&
                   (in.nowMs - lastUnderrunMs_) > 1000u) {
            bridge_.setVoiceLimit(bridge_.voiceLimit() + 1);
            lastUnderrunMs_ = in.nowMs;
            dirty_ = true;
        }

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
        namespace kit = picoface::ui::kit;

        // The developer's numbers went from a strip at the bottom of every page
        // to a page of their own (#161: that strip cost the body nine rows on
        // a screen with none to spare). Formatted here because only this
        // instrument knows what its numbers are; 6x12 holds 21 characters.
        if (controller_.isDiagPage()) {
            // Peak load first: it is the number that says how much headroom
            // is left, and the one that moves before an underrun happens.
            char r0[22], r1[22], r2[22];
            snprintf(r0, sizeof r0, "CPU peak %d%%", (int)bridge_.cpuLoadPeakPercent());
            snprintf(r1, sizeof r1, "Underruns %lu", (unsigned long)g_i2s_underrun_count);
            snprintf(r2, sizeof r2, "Voices %d/%d", bridge_.activeVoices(), bridge_.voiceLimit());
            const char* rows[3] = { r0, r1, r2 };
            kit::diagnostics(d, controller_.pageName(), rows, 3);
            d.flush();
            return;
        }


        const JV_Controller::Value a = controller_.valueA();
        const JV_Controller::Value b = controller_.valueB();

        d.clear();
        kit::header(d, controller_.pageName(), controller_.pageIndex(),
                    controller_.pageTotal());
        if (controller_.isPatchPage()) {
            // A patch name does not fit in half the width at a size worth
            // reading, so it gets all of it, with the bank under it.
            kit::panelName(d, a.text, b.text, {});
        } else {
            kit::panelDuo(d, { a.name, a.text, a.norm },
                             { b.name, b.text, b.norm });
        }

        // Arms the incremental push. Painting the buffer is not enough: the main
        // loop only streams it out while picoface_ui_flush_row < 16, and flush()
        // is what resets that counter. Without this the display keeps showing
        // whatever was pushed last -- the boot splash -- while everything else
        // runs normally.
        d.flush();
    }

    static constexpr int kVoiceFloor = 4;

    JV_Bridge bridge_;
    JV_Controller controller_;
    JV_Midi midi_;
    bool dirty_ = true;
    uint32_t lastDrawMs_ = 0;
    uint32_t lastUnderrun_ = 0;
    uint32_t lastUnderrunMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(JVInstrument)

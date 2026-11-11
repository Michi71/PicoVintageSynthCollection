// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// VS_Instrument.cpp - adapter binding the provs engine (Sequential Prophet VS)
// to picoface::Instrument. Standard runtime model: core0 does audio, USB, MIDI
// and GUI, core1 is unused.
//
// MIDI dispatch, uiTick() and render() are all core0 main-loop steps and never
// overlap, so MIDI and the panel call the engine directly; there is no ring.

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/timer.h"

#include "picoface/instrument.h"
#include "picoface/ui_kit.h"

#include "project_config.h"    // PIN_LED
#include "audio_subsystem.h"   // SAMPLES_PER_BUFFER

#include "provs/engine.h"
#include "vs_banks.h"          // generated: the user's .syx files, or empty

namespace {

// The engine is rate independent. On the hardware it peaked at 53 % load at
// 32 kHz, so 44.1 kHz should peak near 73 %; the panel shows the load.
constexpr uint32_t kSampleRate = 44100;

// Longest SysEx this instrument takes: a Prophet VS bank dump
// (F0 01 0A 64, 100 x 164 nibbles, F7).
constexpr size_t kSysExMax = 16405;

enum Bank : uint8_t { kBankVs = 0, kBankMini = 1 };

const char* const kFxName[5] = {"Prog", "Off", "Chorus", "Ensem", "VS Cho"};

struct VsSettingsV1 {
    uint8_t bank;
    uint8_t program;
    uint8_t fx;        // 0 = as programmed, 1..4 = forced effect type + 1
    uint8_t volume;    // 0..100
};

class VSInstrument final : public picoface::Instrument {
public:
    const char* name() const override { return "PicoFaceVS"; }

    void init() override {
        engine_.init((double) kSampleRate);
        if (kVsEmbeddedBankSize) {
            provs::Engine::loadVsSysex(kVsEmbeddedBanks, kVsEmbeddedBankSize);
            provs::Engine::loadPvmSysex(kVsEmbeddedBanks, kVsEmbeddedBankSize);
        }
        applyProgram();
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
        const float load = (float) us * 100.f * (float) kSampleRate / ((float) frames * 1000000.f);
        load_ = load;
        if (load > loadPeak_) loadPeak_ = load;
    }

    // A flash write stalls the CPU for milliseconds, which is audible.
    bool settingsSaveAllowed() const override { return engine_.activeVoices() == 0; }

    // ----------------------------------------------------------------
    // MIDI
    // ----------------------------------------------------------------

    void noteOn(uint8_t, uint8_t note, uint8_t vel) override {
        if (vel == 0) { noteOff(0, note, 0); return; }
        gpio_put(PIN_LED, 1);
        engine_.noteOn(note, vel);
    }

    void noteOff(uint8_t, uint8_t note, uint8_t) override {
        gpio_put(PIN_LED, 0);
        engine_.noteOff(note);
    }

    void controlChange(uint8_t, uint8_t cc, uint8_t value) override {
        switch (cc) {
        case 0:   return;                                    // bank select MSB, ignored
        case 32:  midiBank_ = value; return;                 // bank select LSB
        case 1:   engine_.setModWheel((float) value * (1.f / 127.f)); return;
        case 7:   volume_ = (uint8_t)((value * 100 + 63) / 127); applyOverrides(); dirty_ = true; return;
        case 64:  engine_.setHold(value >= 64); return;
        case 66:  engine_.setAltRelease(value >= 64); return;   // the VS's alternate-release footswitch
        case 120:
        case 123: engine_.allNotesOff(); return;
        default:  return;
        }
    }

    void pitchBend(uint8_t, int16_t bend) override {
        engine_.pitchBend((float) bend * (1.f / 8192.f));
    }

    void channelPressure(uint8_t, uint8_t value) override {
        engine_.setPressure((float) value * (1.f / 127.f));
    }

    // Bank select LSB 0 = Prophet VS programs, 1 = Pro VS Mini patches. An
    // index past the end is ignored rather than wrapped.
    void programChange(uint8_t, uint8_t program) override {
        const uint8_t bank = midiBank_ == 1 ? kBankMini : kBankVs;
        if (program >= count(bank)) return;
        bank_ = bank;
        program_ = program;
        applyProgram();
        dirty_ = true;
    }

    // Bank dumps do not fit the core's 512-byte SysEx buffer, so take the
    // stream. A complete message is handed to both loaders; each ignores what
    // it does not know. The bank lives in RAM: a dump received here is gone
    // after a power cycle (the embedded banks come back).
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
                const int n = provs::Engine::loadVsSysex(sysex_, sysexLen_)
                            + provs::Engine::loadPvmSysex(sysex_, sysexLen_);
                if (n > 0) {
                    if (program_ >= count(bank_)) program_ = 0;
                    engine_.allNotesOff();
                    applyProgram();
                    dirty_ = true;
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // GUI: Sel steps through the programs (press: switch bank), A picks the
    // effect, B the volume.
    // ----------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override { draw(d, 0); }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        using picoface::ui::Button;
        using picoface::ui::Encoder;
        const int ds = in.delta(Encoder::Sel);
        const int da = in.delta(Encoder::ParamA);
        const int db = in.delta(Encoder::ParamB);

        if (ds) {
            const int n = count(bank_);
            int p = ((int) program_ + ds) % n;
            if (p < 0) p += n;
            program_ = (uint8_t) p;
            applyProgram();
            dirty_ = true;
        }
        if (in.pressed(Button::Sel) && count(kBankMini) > 0) {
            bank_ = bank_ == kBankVs ? kBankMini : kBankVs;
            program_ = 0;
            applyProgram();
            dirty_ = true;
        }
        if (da) {
            int f = ((int) fx_ + da) % 5;
            if (f < 0) f += 5;
            fx_ = (uint8_t) f;
            applyProgram();          // "Prog" needs the programmed effect back
            dirty_ = true;
        }
        if (db) {
            int v = (int) volume_ + db * 2;
            volume_ = (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v));
            applyOverrides();
            dirty_ = true;
        }
        if (in.pressed(Button::ParamB)) { loadPeak_ = 0.f; dirty_ = true; }

        // Redraw after input, and twice a second for the load figures.
        if (dirty_ || in.nowMs - lastDrawMs_ > 500u) {
            draw(d, in.nowMs);
        } else if (scrolling_ && in.nowMs - lastMarqueeMs_ > 40u) {
            lastMarqueeMs_ = in.nowMs;
            scrolling_ = picoface::ui::kit::marqueeTick(d, in.nowMs);
        }
    }

    // ----------------------------------------------------------------
    // Persistence: which program, the effect choice and the volume
    // ----------------------------------------------------------------

    uint16_t settingsVersion() const override { return 1; }
    size_t settingsSize() const override { return sizeof(VsSettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(VsSettingsV1)) return;
        const VsSettingsV1 s{bank_, program_, fx_, volume_};
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(VsSettingsV1)) return;
        VsSettingsV1 s;
        memcpy(&s, buffer, sizeof(s));
        bank_ = s.bank == kBankMini && count(kBankMini) > 0 ? kBankMini : kBankVs;
        program_ = s.program < count(bank_) ? s.program : 0;
        fx_ = s.fx < 5 ? s.fx : 0;
        volume_ = s.volume <= 100 ? s.volume : 80;
        applyProgram();
        dirty_ = true;
    }

private:
    static int count(uint8_t bank) {
        return bank == kBankMini ? provs::Engine::pvmPatchCount() : provs::Engine::presetCount();
    }

    void applyProgram() {
        if (bank_ == kBankMini) engine_.setPvmPatch(program_);
        else engine_.setPreset(program_);
        applyOverrides();
    }

    // Volume and a forced effect sit on top of whatever the program says.
    void applyOverrides() {
        provs::Params p = engine_.params();
        p.gain = 0.7f * (float) volume_ * (1.f / 80.f);     // 80 = the engine's own level
        if (fx_ > 0) p.fxType = (provs::Fx::Type)(fx_ - 1);
        engine_.setParams(p);
    }

    void draw(picoface::ui::Display& d, uint32_t nowMs) {
        namespace kit = picoface::ui::kit;
        char number[8], name[12], sub[28], vol[8];
        if (bank_ == kBankMini) {
            snprintf(number, sizeof number, "M%02d", (int) program_ + 1);
            snprintf(name, sizeof name, "%s", provs::Engine::pvmPatchName(program_));
        } else {
            snprintf(number, sizeof number, "%02d", (int) program_);
            provs::Engine::presetName(program_, name);
        }
        snprintf(sub, sizeof sub, "FX %s  %dv %d/%d%%", kFxName[fx_], engine_.activeVoices(),
                 (int) load_, (int) loadPeak_);
        snprintf(vol, sizeof vol, "%d", (int) volume_);

        d.clear();
        kit::header(d, "PicoFaceVS");
        kit::Param b;
        b.name = "Volume";
        b.text = vol;
        b.norm = (float) volume_ * 0.01f;
        scrolling_ = kit::panelName(d, number, name, sub, b, nowMs);
        d.flush();
        dirty_ = false;
        lastDrawMs_ = lastMarqueeMs_ = nowMs;
    }

    provs::Engine engine_;
    uint8_t  bank_     = kBankVs;
    uint8_t  program_  = 0;
    uint8_t  fx_       = 0;
    uint8_t  volume_   = 80;
    uint8_t  midiBank_ = 0;      // CC 32, held until the next program change
    float    load_     = 0.f;
    float    loadPeak_ = 0.f;
    bool     dirty_    = true;
    bool     scrolling_ = false;
    uint32_t lastDrawMs_ = 0, lastMarqueeMs_ = 0;

    uint8_t  sysex_[kSysExMax];
    size_t   sysexLen_  = 0;
    bool     sysexOpen_ = false;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(VSInstrument)

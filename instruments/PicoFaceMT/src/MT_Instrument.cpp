// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// MT_Instrument.cpp - adapter binding the mello tape replay engine to
// picoface::Instrument: MIDI in, the three-encoder panel, the settings record.
//
// MIDI and the panel run in the main loop, render() in the audio producer;
// both are on core0. Everything that touches the engine therefore goes through
// a single-producer single-consumer ring and is applied at the start of a
// block. The panel state (State below) lives on the main-loop side only.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/timer.h"

#include "picoface/instrument.h"
#include "picoface/ui_kit.h"
#include "audio_i2s.h"          // g_i2s_underrun_count for the diagnostics page
#include "mello/engine.h"

#ifndef RAM_HOT
#define RAM_HOT(f) __not_in_flash_func(f)
#endif

namespace {

constexpr uint32_t kSampleRate = 44100;
constexpr int      kScale      = 100;     // panel and settings resolution of a parameter

// ---------------------------------------------------------------------------
// Main loop -> audio ring
// ---------------------------------------------------------------------------
enum : uint8_t { kEvNoteOn, kEvNoteOff, kEvParam, kEvInstrument, kEvBendRange,
                 kEvPitchBend, kEvSustain, kEvAllOff };

inline uint32_t pack(uint8_t type, uint8_t d1, uint16_t d2) {
    return (uint32_t(type) << 24) | (uint32_t(d1) << 16) | d2;
}

// 256 entries: a full ring dropping a note-off is a stuck note.
volatile uint32_t ringBuf[256];
volatile uint16_t ringHead = 0, ringTail = 0;
volatile uint32_t ringDropped = 0;

void push(uint32_t pkt) {
    uint16_t next = (ringHead + 1) & 255;
    if (next == ringTail) { ringDropped = ringDropped + 1; return; }
    ringBuf[ringHead] = pkt;
    ringHead = next;
}

inline bool pop(uint32_t* pkt) {
    if (ringTail == ringHead) return false;
    *pkt = ringBuf[ringTail];
    ringTail = (ringTail + 1) & 255;
    return true;
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------
// What one encoder edits: an engine parameter, or one of the few things that
// are not a 0..1 value.
enum Item : uint8_t {
    kItemParam0 = 0,                                  // .. mello::kNumParams - 1
    kItemInstA = mello::kNumParams, kItemInstB, kItemInstC,
    kItemBend,
    kItemNone
};

struct Page { const char* title; uint8_t a, b; };

const Page kPages[] = {
    {"Mellotron", mello::kTrack,    mello::kVolume},
    {"Tapes A B", kItemInstA,       kItemInstB},
    {"Tape C",    kItemInstC,       kItemNone},
    {"Tape",      mello::kTapeLimit, mello::kRewind},
    {"Tone",      mello::kTone,     mello::kDrive},
    {"Motor",     mello::kWow,      mello::kFlutter},
    {"Keys",      mello::kAttack,   mello::kRelease},
    {"Feel",      mello::kKeyDrag,  mello::kVelocity},
    {"Tune",      mello::kPitch,    kItemBend},
    {"Diagnostics", kItemNone,      kItemNone},
};
constexpr int kPageCount = int(sizeof kPages / sizeof kPages[0]);
constexpr int kDiagPage  = kPageCount - 1;

const char* const kParamNames[mello::kNumParams] = {
    "Volume", "Tone", "Pitch", "Track", "Attack", "Release",
    "Velocity", "Wow", "Flutter", "Drive", "Drag", "Limit", "Rewind"};

// The settings record. Bump kSettingsVersion on any change.
constexpr uint16_t kSettingsVersion = 2;
struct State {
    uint8_t value[mello::kNumParams];   // 0..kScale
    uint8_t inst[mello::kNumTracks];    // bank index per track
    uint8_t bendRange;                  // semitones, 1..12
    uint8_t page;
};

const State kDefaults = {
    // volume tone pitch track attack release velocity wow flutter drive drag limit rewind
    {80, 75, 50, 0, 0, 12, 0, 25, 25, 20, 30, 0, 30},
    {0, 0, 0},                          // filled from the engine in init()
    2, 0};

class MTInstrument final : public picoface::Instrument {
public:
    const char* name() const override { return "PicoFaceMT"; }

    void init() override {
        engine_.init(float(kSampleRate));
        state_ = kDefaults;
        for (int t = 0; t < mello::kNumTracks; ++t) state_.inst[t] = uint8_t(engine_.trackInstrument(t));
        // No audio is running yet, so the engine may be set directly.
        for (int p = 0; p < mello::kNumParams; ++p)
            engine_.setParam(mello::Param(p), float(state_.value[p]) / float(kScale));
        engine_.setBendRange(float(state_.bendRange));
    }

    uint32_t sampleRate() const override { return kSampleRate; }

    void RAM_HOT(render)(int32_t* out, uint32_t frames) override {
        const uint32_t t0 = time_us_32();
        uint32_t pkt;
        while (pop(&pkt)) apply(pkt);

        float buf[64];
        uint32_t done = 0;
        while (done < frames) {
            uint32_t n = frames - done;
            if (n > 64) n = 64;
            engine_.process(buf, int(n));
            for (uint32_t i = 0; i < n; ++i) {
                // S32 words, sample left-aligned: 24 bits shifted up by 8.
                // The engine is mono; both channels carry the same signal.
                int32_t s = int32_t(buf[i] * 8388607.0f);
                if (s >  8388607) s =  8388607;
                if (s < -8388608) s = -8388608;
                out[(done + i) * 2 + 0] = s << 8;
                out[(done + i) * 2 + 1] = s << 8;
            }
            done += n;
        }
        voices_ = uint8_t(engine_.activeVoices());

        const uint32_t used  = time_us_32() - t0;
        const uint32_t avail = uint32_t((1000000ull * frames) / kSampleRate);
        if (avail) {
            const int pct = int((used * 100u) / avail);
            if (pct > cpuPeak_) cpuPeak_ = pct;
        }
    }

    // A flash write stalls the CPU for milliseconds; wait until nothing sounds.
    bool settingsSaveAllowed() const override { return voices_ == 0; }

    // ------------------------------------------------------------------
    // MIDI (omni: the channel is ignored)
    // ------------------------------------------------------------------
    void noteOn(uint8_t, uint8_t note, uint8_t vel) override {
        if (vel == 0) { push(pack(kEvNoteOff, note, 0)); return; }
        push(pack(kEvNoteOn, note, vel));
        ++notes_;
    }
    void noteOff(uint8_t, uint8_t note, uint8_t) override { push(pack(kEvNoteOff, note, 0)); }

    void controlChange(uint8_t, uint8_t cc, uint8_t v) override {
        switch (cc) {
            case 3:   setFromMidi(mello::kTrack, v);   break;
            case 7:   setFromMidi(mello::kVolume, v);  break;
            case 12:  setFromMidi(mello::kWow, v);     break;
            case 13:  setFromMidi(mello::kFlutter, v); break;
            case 71:  setFromMidi(mello::kDrive, v);   break;
            case 72:  setFromMidi(mello::kRelease, v); break;
            case 73:  setFromMidi(mello::kAttack, v);  break;
            case 74:  setFromMidi(mello::kTone, v);    break;
            case 64:  push(pack(kEvSustain, 0, v >= 64)); break;
            case 120:
            case 123: push(pack(kEvAllOff, 0, 0)); break;
            default:  break;
        }
    }

    // Program change picks the tape on track A.
    void programChange(uint8_t, uint8_t program) override {
        if (program < mello::kBankSize) setItem(kItemInstA, program);
    }

    void pitchBend(uint8_t, int16_t bend) override {
        push(pack(kEvPitchBend, 0, uint16_t(int32_t(bend) + 8192)));
    }

    // ------------------------------------------------------------------
    // GUI
    // ------------------------------------------------------------------
    void uiInit(picoface::ui::Display& d) override { draw(d); }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        using picoface::ui::Button;
        using picoface::ui::Encoder;

        if (in.pressed(Button::Sel) && state_.page != 0) { state_.page = 0; dirty_ = true; }

        if (const int8_t dp = in.delta(Encoder::Sel)) {
            int p = int(state_.page) + dp;
            state_.page = uint8_t(p < 0 ? 0 : (p >= kPageCount ? kPageCount - 1 : p));
            dirty_ = true;
        }
        const Page& pg = kPages[state_.page];
        if (const int8_t da = in.delta(Encoder::ParamA)) turn(pg.a, da);
        if (const int8_t db = in.delta(Encoder::ParamB)) turn(pg.b, db);

        // Promptly after a change; otherwise twice a second, which keeps the
        // diagnostics page alive.
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u) {
            draw(d);
            dirty_ = false;
            lastDrawMs_ = in.nowMs;
        }
    }

    // ------------------------------------------------------------------
    // Persistence
    // ------------------------------------------------------------------
    uint16_t settingsVersion() const override { return kSettingsVersion; }
    size_t   settingsSize() const override { return sizeof(State); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(State)) return;
        memcpy(buffer, &state_, sizeof(State));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(State)) return;
        State s;
        memcpy(&s, buffer, sizeof(State));
        // A record from a build with another bank or range must not index
        // outside the tables.
        for (int p = 0; p < mello::kNumParams; ++p) setItem(uint8_t(p), s.value[p] > kScale ? kScale : s.value[p]);
        for (int t = 0; t < mello::kNumTracks; ++t)
            if (s.inst[t] < mello::kBankSize) setItem(uint8_t(kItemInstA + t), s.inst[t]);
        setItem(kItemBend, s.bendRange);
        state_.page = s.page < kPageCount ? s.page : 0;
        dirty_ = true;
    }

private:
    // Audio side: one ring packet into the engine.
    void apply(uint32_t pkt) {
        const uint8_t  d1 = uint8_t(pkt >> 16);
        const uint16_t d2 = uint16_t(pkt);
        switch (uint8_t(pkt >> 24)) {
            case kEvNoteOn:     engine_.noteOn(d1, uint8_t(d2)); break;
            case kEvNoteOff:    engine_.noteOff(d1); break;
            case kEvParam:      engine_.setParam(mello::Param(d1), float(d2) / float(kScale)); break;
            case kEvInstrument: engine_.setTrackInstrument(d1, d2); break;
            case kEvBendRange:  engine_.setBendRange(float(d2)); break;
            case kEvPitchBend:  engine_.pitchBend((float(d2) - 8192.0f) / 8192.0f); break;
            case kEvSustain:    engine_.sustainPedal(d2 != 0); break;
            case kEvAllOff:     engine_.allNotesOff(); break;
            default: break;
        }
    }

    // Main-loop side: store a value in the panel state and send it on.
    void setItem(uint8_t item, int v) {
        if (item < mello::kNumParams) {
            v = v < 0 ? 0 : (v > kScale ? kScale : v);
            state_.value[item] = uint8_t(v);
            push(pack(kEvParam, item, uint16_t(v)));
        } else if (item <= kItemInstC) {
            const int t = item - kItemInstA;
            v = v < 0 ? 0 : (v >= mello::kBankSize ? mello::kBankSize - 1 : v);
            state_.inst[t] = uint8_t(v);
            push(pack(kEvInstrument, uint8_t(t), uint16_t(v)));
        } else if (item == kItemBend) {
            v = v < 1 ? 1 : (v > 12 ? 12 : v);
            state_.bendRange = uint8_t(v);
            push(pack(kEvBendRange, 0, uint16_t(v)));
        }
        dirty_ = true;
    }

    void setFromMidi(mello::Param p, uint8_t v) { setItem(p, (int(v) * kScale + 63) / 127); }

    void turn(uint8_t item, int delta) {
        if (item < mello::kNumParams) {
            if (item == mello::kTapeLimit) setItem(item, delta > 0 ? kScale : 0);
            else if (item == mello::kTrack) setItem(item, state_.value[item] + delta * 5);
            else setItem(item, state_.value[item] + delta * 2);
        } else if (item <= kItemInstC) {
            setItem(item, state_.inst[item - kItemInstA] + delta);
        } else if (item == kItemBend) {
            setItem(item, state_.bendRange + delta);
        }
    }

    // Name, formatted value and knob position of one encoder's item.
    // Beside a knob the value has 36 pixels, five characters of the small face;
    // hence "500ms" without a space and cents without a unit.
    picoface::ui::kit::Param describe(uint8_t item, char* text, size_t len) const {
        picoface::ui::kit::Param p;
        p.text = text;
        text[0] = 0;
        if (item < mello::kNumParams) {
            const int v = state_.value[item];
            const float n = float(v) / float(kScale);
            p.name = kParamNames[item];
            p.norm = n;
            switch (item) {
                case mello::kTrack:
                    snprintf(text, len, "%s", v == 0 ? "A" : v < 50 ? "A+B" : v == 50 ? "B" : v < 100 ? "B+C" : "C");
                    break;
                case mello::kPitch:
                    snprintf(text, len, "%+d", (v - 50) * 4);          // cents
                    break;
                case mello::kAttack:
                    snprintf(text, len, "%dms", int(powf(500.0f, n) + 0.5f));
                    break;
                case mello::kRelease: {
                    const int ms = int(5.0f * powf(600.0f, n) + 0.5f);
                    if (ms < 1000) snprintf(text, len, "%dms", ms);
                    else snprintf(text, len, "%d.%d s", ms / 1000, (ms % 1000) / 100);
                    break;
                }
                case mello::kRewind:
                    // time a fully played 8 s tape takes to come back
                    if (v == 0) snprintf(text, len, "Off");
                    else if (v < 50) snprintf(text, len, "%dms", v * 20);
                    else snprintf(text, len, "%d.%d s", v / 50, (v % 50) / 5);
                    break;
                case mello::kTapeLimit:
                    snprintf(text, len, "%s", v >= 50 ? "8 s" : "Off");
                    p.norm = -1.0f;
                    break;
                default:
                    snprintf(text, len, "%d", v);
                    break;
            }
        } else if (item <= kItemInstC) {
            static const char* const kNames[] = {"Tape A", "Tape B", "Tape C"};
            p.name = kNames[item - kItemInstA];
            snprintf(text, len, "%s", mello::kBank[state_.inst[item - kItemInstA]].label);
        } else if (item == kItemBend) {
            p.name = "Bend";
            p.norm = float(state_.bendRange - 1) / 11.0f;
            snprintf(text, len, "%d st", int(state_.bendRange));
        }
        return p;
    }

    void draw(picoface::ui::Display& d) {
        namespace kit = picoface::ui::kit;
        const Page& pg = kPages[state_.page];

        if (state_.page == kDiagPage) {
            char r0[22], r1[22], r2[22], r3[22];
            snprintf(r0, sizeof r0, "CPU peak %d%%", int(cpuPeak_));
            snprintf(r1, sizeof r1, "Underruns %lu", (unsigned long) g_i2s_underrun_count);
            snprintf(r2, sizeof r2, "Dropped %lu", (unsigned long) ringDropped);
            snprintf(r3, sizeof r3, "Voices %d Notes %lu", int(voices_), (unsigned long) notes_);
            cpuPeak_ = 0;
            const char* rows[4] = {r0, r1, r2, r3};
            kit::diagnostics(d, pg.title, rows, 4);
            d.flush();
            return;
        }

        char ta[24], tb[24];
        const kit::Param a = describe(pg.a, ta, sizeof ta);
        const kit::Param b = describe(pg.b, tb, sizeof tb);
        d.clear();
        kit::header(d, pg.title, state_.page, kPageCount);
        kit::panelDuo(d, a, b);
        d.flush();
    }

    mello::Engine engine_;
    State    state_      = kDefaults;
    bool     dirty_      = false;
    uint32_t lastDrawMs_ = 0;
    uint32_t notes_      = 0;
    volatile int     cpuPeak_ = 0;
    volatile uint8_t voices_  = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(MTInstrument)

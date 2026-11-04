// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_engine.cpp -- voices, allocation, programs and combinations.
//
// Section numbers refer to the analysis summarised in doc/ENGINE.md. The
// oscillator, VDF, VDA and EG core follow the Python reference m1_engine.py
// line by line so a host render can be compared sample for sample
// (setReferenceMode); the program-parameter terms on top of it (velocity,
// keyboard tracking, MGs, controllers) are the firmware's where the driver
// code was read, and marked ASSUMED where it was not.
#include "m1_engine/m1_engine.h"

#include <math.h>
#include <string.h>

namespace m1 {
namespace {

constexpr int kLevelUnit = 0x14A;           // EG level step: param x 330
constexpr float kLevelFull = 99.0f * kLevelUnit;
constexpr int kCutMax = 0x1FD2;             // VDF1 clamp, 99.6 x 0x52

inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline int s8(uint8_t v) { return (int8_t)v; }

// Program byte map (owner's manual TABLE 1, matched against the OS edit
// records, analysis 8.21). OSC blocks at 63 (OSC 1) and 103 (OSC 2).
enum : int {
    P_OSC_MODE = 10, P_FLAGS = 11, P_MS1 = 12, P_OCT1 = 13, P_MS2 = 14, P_OCT2 = 15,
    P_INTERVAL = 16, P_DETUNE = 17, P_DELAY = 18,
    P_PMG_BITS = 19, P_PMG_FREQ = 20, P_PMG_DELAY = 21, P_PMG_INT = 22,
    P_CMG_BITS = 23, P_CMG_FREQ = 24, P_CMG_DELAY = 25, P_CMG_INT = 26,
    P_AT_PITCH = 27, P_AT_PMG = 28, P_AT_CUT = 29, P_AT_CMG = 30, P_AT_AMP = 31,
    P_JS_BEND = 32, P_JS_SWEEP = 33, P_JS_PMG = 34, P_JS_PMG_F = 35, P_JS_CMG = 36, P_JS_CMG_F = 37,
    P_FX = 38, P_OSC1 = 63, P_OSC2 = 103,
};
enum : int {   // relative to the OSC block
    O_PEG_SL = 0, O_PEG_AT = 1, O_PEG_AL = 2, O_PEG_DT = 3, O_PEG_RT = 4, O_PEG_RL = 5,
    O_PEG_TVEL = 6, O_PEG_LVEL = 7,
    O_CUTOFF = 8, O_VDF_KC = 9, O_VDF_KT = 10, O_VDF_EGI = 11, O_VDF_TKBD = 12, O_VDF_TVEL = 13,
    O_VDF_IVEL = 14, O_VDF_AT = 15, O_VDF_AL = 16, O_VDF_DT = 17, O_VDF_BP = 18, O_VDF_ST = 19,
    O_VDF_SL = 20, O_VDF_RT = 21, O_VDF_RL = 22,
    O_LEVEL = 23, O_VDA_KC = 24, O_VDA_KT = 25, O_VDA_VEL = 26, O_VDA_TKBD = 27, O_VDA_TVEL = 28,
    O_VDA_AT = 29, O_VDA_AL = 30, O_VDA_DT = 31, O_VDA_BP = 32, O_VDA_ST = 33, O_VDA_SL = 34,
    O_VDA_RT = 35,
    O_VDF_TKBD_SW = 36, O_VDF_TVEL_SW = 37, O_VDA_TKBD_SW = 38, O_VDA_TVEL_SW = 39,
};

}  // namespace

// ---------------------------------------------------------------------------
// Envelopes (analysis 6.2)
// ---------------------------------------------------------------------------
float Engine::segUpdates(int timeParam, bool rising, float scale) const {
    int t = (int)((float)timeParam * 165.0f * scale);
    if (t > 0x3FFF) t = 0x3FFF;
    if (t < 0) t = 0;
    const int idx = t >> 6;
    const int rate = (rising ? rom_.egRise : rom_.egFall)[idx];
    return 65536.0f / (float)(rate < 1 ? 1 : rate);
}

void Engine::egStart(Eg& e, int startLevel, const int16_t* times, const int16_t* levels, int n,
                     int relTime, int relLevel, bool useFall, float timeScale) {
    e.level = startLevel * kLevelUnit;
    e.nseg = n;
    for (int i = 0; i < n; ++i) { e.segTime[i] = times[i]; e.segLevel[i] = levels[i]; }
    e.relTime = (int16_t)relTime;
    e.relLevel = (int16_t)relLevel;
    e.useFall = useFall;
    e.timeScale = timeScale;
    e.seg = 0;
    e.active = false;
    if (n > 0) {
        e.src = e.level;
        e.target = levels[0] * kLevelUnit;
        const bool rising = e.target >= e.src || !useFall;
        e.n = segUpdates(times[0], rising, timeScale);
        e.k = 0;
        e.seg = 1;
        e.active = true;
    }
}

void Engine::egRelease(Eg& e) {
    e.nseg = 0;
    e.seg = 0;
    e.src = e.level;
    e.target = e.relLevel * kLevelUnit;
    const bool rising = e.target >= e.src || !e.useFall;
    e.n = segUpdates(e.relTime, rising, e.timeScale);
    e.k = 0;
    e.active = true;
}

// One 20.8 ms update. Returns the level normalised to -1..1.
float Engine::egStep(Eg& e) {
    if (e.active) {
        e.k += 1;
        if ((float)e.k >= e.n) {
            e.level = e.target;
            if (e.seg < e.nseg) {
                e.src = e.level;
                e.target = e.segLevel[e.seg] * kLevelUnit;
                const bool rising = e.target >= e.src || !e.useFall;
                e.n = segUpdates(e.segTime[e.seg], rising, e.timeScale);
                e.k = 0;
                e.seg += 1;
            } else {
                e.active = false;
            }
        } else {
            e.level = e.src + (int32_t)((float)(e.target - e.src) * (float)e.k / e.n);
        }
    }
    return (float)e.level / kLevelFull;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
bool Engine::init(const RomTables& rom, int16_t* fxMem) {
    rom_ = rom;
    fx_.init(rom.fx, rom.pan101, fxMem);
    for (int i = 0; i < kVoices; ++i) { queue_[i] = (uint8_t)i; voice_[i] = Voice(); }
    return rom.pcm != nullptr;
}

void Engine::configureTimbre(int t, const uint8_t* prog) {
    Timbre& tb = tim_[t];
    tb.prog = prog;
    tb.on = prog != nullptr;
    tb.monoNotes[0] = tb.monoNotes[1] = tb.monoNotes[2] = 0xFF;
}

void Engine::selectProgram(int index) {
    allSoundOff();
    index = clampi(index, 0, kPrograms - 1);
    const uint8_t* p = rom_.programs + kProgLen * index;
    for (int t = 0; t < kTimbres; ++t) configureTimbre(t, nullptr);
    configureTimbre(0, p);
    Timbre& tb = tim_[0];
    tb.level = 99; tb.transpose = 0; tb.detune = 0;
    tb.pan = 5;                          // programs feed A/B at 5:5 (manual)
    tb.keyLo = 0; tb.keyHi = 127; tb.velLo = 1; tb.velHi = 127; tb.midiCh = 0;
    combiMode_ = false;
    memcpy(name_, p, 10);
    name_[10] = 0;
    fx_.configure(p + P_FX);
}

void Engine::selectCombi(int index) {
    allSoundOff();
    index = clampi(index, 0, kCombis - 1);
    const uint8_t* c = rom_.combis + kCombiLen * index;
    for (int t = 0; t < kTimbres; ++t) {
        const uint8_t* r = c + 36 + 11 * t;
        const bool off = (r[10] & 0x10) != 0;
        const int prog = r[0];
        configureTimbre(t, (off || prog >= kPrograms) ? nullptr : rom_.programs + kProgLen * prog);
        Timbre& tb = tim_[t];
        tb.level = clampi(r[1], 0, 99);
        tb.transpose = s8(r[2]);         // factory combis go to +-24
        tb.detune = s8(r[3]);
        tb.pan = r[4];
        tb.keyHi = r[5]; tb.keyLo = r[6];
        tb.velHi = r[7]; tb.velLo = r[8];
        tb.midiCh = r[10] & 0x0F;
    }
    combiMode_ = true;
    memcpy(name_, c, 10);
    name_[10] = 0;
    fx_.configure(c + 11);
}

// ---------------------------------------------------------------------------
// Allocation (analysis 7): the queue is kept as
//   [ free | releasing (oldest first) | held (oldest first) ]
// and a new note always takes the head.
// ---------------------------------------------------------------------------
int Engine::takeChannel() {
    // Respect the voice limit: only channels below it are ever handed out.
    for (int i = 0; i < kVoices; ++i) {
        const int v = queue_[i];
        if (v < voiceLimit_) {
            memmove(queue_ + i, queue_ + i + 1, (size_t)(kVoices - 1 - i));
            queue_[kVoices - 1] = (uint8_t)v;
            return v;
        }
    }
    return 0;
}

static void moveTo(uint8_t* q, int v, int pos) {
    int i = 0;
    while (i < kVoices && q[i] != v) ++i;
    if (i == kVoices) return;
    memmove(q + i, q + i + 1, (size_t)(kVoices - 1 - i));
    if (pos > kVoices - 1) pos = kVoices - 1;
    memmove(q + pos + 1, q + pos, (size_t)(kVoices - 1 - pos));
    q[pos] = (uint8_t)v;
}

void Engine::queueToFront(int v) { moveTo(queue_, v, 0); }

void Engine::queueBeforeHeld(int v) {
    // first held channel after removing v
    int pos = 0;
    for (int i = 0; i < kVoices; ++i) {
        const int c = queue_[i];
        if (c == v) continue;
        if (voice_[c].active && (voice_[c].status & 0x80)) break;
        ++pos;
    }
    moveTo(queue_, v, pos);
}

void Engine::queueToBack(int v) { moveTo(queue_, v, kVoices - 1); }

int Engine::activeVoices() const {
    int n = 0;
    for (const Voice& v : voice_) n += v.active ? 1 : 0;
    return n;
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------
void Engine::noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    if (vel == 0) { noteOff(ch, note); return; }
    for (int t = 0; t < kTimbres; ++t) {
        const Timbre& tb = tim_[t];
        if (!tb.on || tb.midiCh != ch) continue;
        if (note < tb.keyLo || note > tb.keyHi) continue;
        if (vel < tb.velLo || vel > tb.velHi) continue;
        timbreNoteOn(t, note, vel);
    }
}

void Engine::timbreNoteOn(int t, uint8_t note, uint8_t vel) {
    Timbre& tb = tim_[t];
    const uint8_t* p = tb.prog;
    const int mode = p[P_OSC_MODE];
    const bool mono = (p[P_FLAGS] & 1) != 0;
    if (mono) {
        // Retrigger, not legato: every channel of the timbre stops hard.
        for (int v = 0; v < kVoices; ++v)
            if (voice_[v].active && voice_[v].timbre == t) killVoice(v);
        tb.monoNotes[2] = tb.monoNotes[1]; tb.monoVel[2] = tb.monoVel[1];
        tb.monoNotes[1] = tb.monoNotes[0]; tb.monoVel[1] = tb.monoVel[0];
        tb.monoNotes[0] = note; tb.monoVel[0] = vel;
    }
    // The zone note is folded back into 0..127 by octaves; the pitch note is not.
    int pitchNote = note + tb.transpose;
    const int oscs = (mode == 1) ? 2 : 1;
    for (int o = 0; o < oscs; ++o) {
        const int v = takeChannel();
        if (voice_[v].active) killVoice(v);
        startVoice(v, t, o, note, vel, pitchNote);
    }
}

void Engine::noteOff(uint8_t ch, uint8_t note) {
    for (int t = 0; t < kTimbres; ++t) {
        Timbre& tb = tim_[t];
        if (!tb.on || tb.midiCh != ch) continue;
        timbreNoteOff(t, note);
    }
}

void Engine::timbreNoteOff(int t, uint8_t note) {
    Timbre& tb = tim_[t];
    const bool mono = tb.prog && (tb.prog[P_FLAGS] & 1);
    if (mono && tb.monoNotes[0] == note) {
        // Last-note priority: a remembered note still held is struck again.
        tb.monoNotes[0] = 0xFF;
        for (int k = 1; k < 3; ++k) {
            if (tb.monoNotes[k] != 0xFF) {
                const uint8_t n = tb.monoNotes[k], vl = tb.monoVel[k];
                tb.monoNotes[k] = 0xFF;
                for (int v = 0; v < kVoices; ++v)
                    if (voice_[v].active && voice_[v].timbre == t) killVoice(v);
                tb.monoNotes[0] = n; tb.monoVel[0] = vl;
                const int oscs = (tb.prog[P_OSC_MODE] == 1) ? 2 : 1;
                for (int o = 0; o < oscs; ++o) {
                    const int v = takeChannel();
                    if (voice_[v].active) killVoice(v);
                    startVoice(v, t, o, n, vl, n + tb.transpose);
                }
                return;
            }
        }
    } else if (mono) {
        for (int k = 1; k < 3; ++k) if (tb.monoNotes[k] == note) tb.monoNotes[k] = 0xFF;
        return;
    }
    for (int v = 0; v < kVoices; ++v) {
        Voice& vc = voice_[v];
        if (!vc.active || vc.timbre != t || vc.note != note || !(vc.status & 0x80)) continue;
        if (tb.damper) {
            vc.status = 3;
            queueBeforeHeld(v);
        } else {
            releaseVoice(v);
        }
    }
}

void Engine::damper(uint8_t ch, bool down) {
    for (int t = 0; t < kTimbres; ++t) {
        Timbre& tb = tim_[t];
        if (tb.midiCh != ch) continue;
        tb.damper = down;
        if (!down)
            for (int v = 0; v < kVoices; ++v)
                if (voice_[v].active && voice_[v].timbre == t && voice_[v].status == 3) releaseVoice(v);
    }
}

void Engine::releaseVoice(int v) {
    Voice& vc = voice_[v];
    vc.status = 1;
    queueBeforeHeld(v);
    // Hold (program byte 11 bit 1): no release; the sound runs to the end of
    // its envelope.
    const Timbre& tb = tim_[vc.timbre];
    if (tb.prog && (tb.prog[P_FLAGS] & 2)) return;
    egRelease(vc.pitchEg);
    egRelease(vc.vdfEg);
    egRelease(vc.vdaEg);
    vc.releasing = true;
}

void Engine::killVoice(int v) {
    voice_[v].active = false;
    voice_[v].status = 0;
    queueToFront(v);
}

void Engine::allNotesOff() {
    for (int t = 0; t < kTimbres; ++t) tim_[t].damper = false;
    for (int v = 0; v < kVoices; ++v)
        if (voice_[v].active && voice_[v].status != 1) releaseVoice(v);
}

void Engine::allSoundOff() {
    for (int v = 0; v < kVoices; ++v) if (voice_[v].active) killVoice(v);
    for (int t = 0; t < kTimbres; ++t) tim_[t].damper = false;
}

void Engine::pitchBend(uint8_t ch, int16_t b) { bend_[ch & 15] = b; }
void Engine::modWheel(uint8_t ch, uint8_t v) { mod_[ch & 15] = v; }
void Engine::modWheelNeg(uint8_t ch, uint8_t v) { modNeg_[ch & 15] = v; }
void Engine::aftertouch(uint8_t ch, uint8_t v) { aft_[ch & 15] = v; }

// ---------------------------------------------------------------------------
// Voice start (analysis 3.2, 7.2)
// ---------------------------------------------------------------------------
void Engine::startVoice(int vi, int t, int osc, uint8_t note, uint8_t vel, int pitchNote) {
    Voice& v = voice_[vi];
    const Timbre& tb = tim_[t];
    const uint8_t* p = tb.prog;
    const uint8_t* o = p + (osc ? P_OSC2 : P_OSC1);
    const int mode = p[P_OSC_MODE];

    int zoneNote = pitchNote;
    while (zoneNote > 127) zoneNote -= 12;
    while (zoneNote < 0) zoneNote += 12;

    // Source: multisound split, or a drum kit instrument.
    uint32_t start, loopA, end;
    bool loop;
    int root, tuneCents, level, cutTrim, panCode = tb.pan & 0x0F;
    bool nt = false;
    float drumLevel = 1.0f;
    if (mode == 2) {
        // Drums: the program's OSC 1 multisound byte selects one of the four
        // global kits; the kit maps a key to a drum sound with its own pan,
        // tune, level and decay (analysis 2.5, owner's manual TABLE 3).
        const uint8_t* kit = rom_.kits + kKitLen * clampi(p[P_MS1], 0, 3);
        const uint8_t* ins = nullptr;
        for (int i = 0; i < 30; ++i)
            if (kit[7 * i] && kit[7 * i + 1] == zoneNote) { ins = kit + 7 * i; break; }
        if (!ins || ins[0] > rom_.nDrums) return;
        const DrumSound& d = rom_.drums[ins[0]];
        start = d.start; loopA = d.loop; end = d.end; loop = d.flags & 1;
        root = d.root;
        // ASSUMED: kit Tune in cents on top of the sound's own, Level as a
        // trim in parameter units like split byte 6.
        tuneCents = d.tune + s8(ins[3]);
        level = d.level + s8(ins[4]) / 4;
        cutTrim = d.cutoff;
        nt = true;                            // drum sounds play at their own pitch
        if (!combiMode_ || (tb.pan & 0x80)) panCode = ins[2] & 0x0F;
        (void)drumLevel;
    } else {
        const int msIdx = clampi(o == p + P_OSC1 ? p[P_MS1] : p[P_MS2], 0, 99);
        const Multisound& ms = rom_.ms[msIdx];
        const Split* sp = nullptr;
        for (int i = 0; i < ms.count; ++i) {
            const Split& s = rom_.splits[ms.first + i];
            if (zoneNote <= s.top) { sp = &s; break; }
        }
        if (!sp) return;                      // keymap FF: no voice
        start = sp->start; loopA = sp->loop; end = sp->end;
        loop = sp->flags & 1;
        nt = (sp->flags & 2) != 0;
        root = sp->root;
        tuneCents = sp->tune;
        level = sp->level;
        cutTrim = sp->cutoff;
        if (tb.pan & 0x80) panCode = sp->pan;
    }

    v = Voice();
    v.active = true;
    v.status = 0x80;
    v.timbre = (uint8_t)t;
    v.osc = (uint8_t)osc;
    v.note = note;
    v.vel = vel;
    v.pitchNote = pitchNote;
    v.base = rom_.pcm + start;
    v.endRel = end - start;
    v.loopLen = end - loopA;
    v.loop = loop && v.loopLen > 0;
    v.pos = 0;
    // root128 = rootKey x 128 - cents x 1.285 (EF83:0223, x 0x149 >> 8), floor
    const int rk = nt ? pitchNote : root;
    int c = tuneCents * 0x149;
    c = (c >= 0) ? (c >> 8) : -((-c + 255) >> 8);
    v.root128 = rk * 128 - c;
    v.splitCutoff = cutTrim;
    v.splitLevel = level;
    v.age = ++ageCounter_;

    // Voice pan to the buses (analysis 8.18): register word from DS:0854,
    // A/B in 6 bits each, C and D on/off.
    if (panCode >= 14) panCode = 5;
    const uint16_t pr = rom_.panReg[panCode];
    v.panA = (pr & 63) / 63.0f;
    v.panB = ((pr >> 6) & 63) / 63.0f;
    v.panC = (pr >> 12) & 1;
    v.panD = (pr >> 13) & 1;

    // Envelopes. Pitch EG: start level, attack, decay to 0, release; rise
    // table only. VDF EG: from 0 through AL, BP, SL; release to RL. VDA EG:
    // from 0 through AL, BP, SL; release to 0.
    float tsF = 1.0f, tsA = 1.0f;
    if (!refMode_) {
        // ASSUMED (doc/ENGINE.md): EG time KBD/VEL scale all non-release
        // segment times; positive values shorten towards the top key / hard
        // playing. Factor range 1/4 .. 4.
        auto factor = [&](int kbd, int velp) {
            float f = 1.0f;
            f *= powf(2.0f, -(float)kbd / 99.0f * (float)(note - 60) / 24.0f);
            f *= powf(2.0f, -(float)velp / 99.0f * (float)(vel - 64) / 32.0f);
            return clampf(f, 0.25f, 4.0f);
        };
        tsF = factor(o[O_VDF_TKBD], o[O_VDF_TVEL]);
        tsA = factor(o[O_VDA_TKBD], o[O_VDA_TVEL]);
    }
    {
        const int16_t tm[2] = {o[O_PEG_AT], o[O_PEG_DT]};
        const int16_t lv[2] = {(int16_t)s8(o[O_PEG_AL]), 0};
        egStart(v.pitchEg, s8(o[O_PEG_SL]), tm, lv, 2, o[O_PEG_RT], s8(o[O_PEG_RL]), false, 1.0f);
    }
    {
        const int16_t tm[3] = {o[O_VDF_AT], o[O_VDF_DT], o[O_VDF_ST]};
        const int16_t lv[3] = {(int16_t)s8(o[O_VDF_AL]), (int16_t)s8(o[O_VDF_BP]), (int16_t)s8(o[O_VDF_SL])};
        egStart(v.vdfEg, 0, tm, lv, 3, o[O_VDF_RT], s8(o[O_VDF_RL]), true, tsF);
    }
    {
        const int16_t tm[3] = {o[O_VDA_AT], o[O_VDA_DT], o[O_VDA_ST]};
        const int16_t lv[3] = {(int16_t)o[O_VDA_AL], (int16_t)o[O_VDA_BP], (int16_t)o[O_VDA_SL]};
        egStart(v.vdaEg, 0, tm, lv, 3, o[O_VDA_RT], 0, true, tsA);
    }

    // OSC 2 delay start, in 1.3 ms ticks (DS:776E) -> samples.
    if (osc == 1 && mode == 1 && !refMode_)
        v.delayTicks = (int)((float)rom_.delayStart[clampi(p[P_DELAY], 0, 99)] * 40.625f);

    // MGs: without key sync a new voice takes over the timbre's running phase.
    for (int m = 0; m < 2; ++m) {
        const uint8_t bits = p[m ? P_CMG_BITS : P_PMG_BITS];
        v.mgPhase[m] = (bits & 0x80) ? 0 : tim_[t].mgPhase[m];
        v.mgDelay[m] = p[m ? P_CMG_DELAY : P_PMG_DELAY];
    }
    v.first = true;
    v.untilUpdate = 0;
}

// ---------------------------------------------------------------------------
// Control update, every 20.8 ms per voice (analysis 6.1, 6.3)
// ---------------------------------------------------------------------------
void Engine::update(Voice& v) {
    const Timbre& tb = tim_[v.timbre];
    const uint8_t* p = tb.prog;
    const uint8_t* o = p + (v.osc ? P_OSC2 : P_OSC1);

    const float pEg = egStep(v.pitchEg);
    const float fEg = egStep(v.vdfEg);
    const float aEg = egStep(v.vdaEg);

    float cutReg, amp;
    int delta128;
    if (refMode_) {
        // Exactly the Python reference's render_voice().
        cutReg = (float)(o[O_CUTOFF] * 0x52 + v.splitCutoff * 0x52) + fEg * o[O_VDF_EGI] / 99.0f * 0x1FFF;
        amp = (aEg > 0 ? aEg : 0) * o[O_LEVEL] / 99.0f * (float)(0x8000 + v.splitLevel * kLevelUnit) / 32768.0f;
        delta128 = v.pitchNote * 128 + 3 - v.root128;
    } else {
        const int ch = tb.midiCh & 15;
        // --- MGs (ASSUMED scaling: the MG table value is the phase step per
        // update in a 16-bit cycle; triangle/saw/square as named).
        float mg[2] = {0, 0};
        for (int m = 0; m < 2; ++m) {
            const uint8_t bits = p[m ? P_CMG_BITS : P_PMG_BITS];
            const bool en = v.osc ? (bits & 0x40) : (bits & 0x20);
            const int freq = clampi(p[m ? P_CMG_FREQ : P_PMG_FREQ], 0, 99);
            v.mgPhase[m] = (v.mgPhase[m] + rom_.mgFreq[freq]) & 0xFFFF;
            if (!en) continue;
            if (v.mgDelay[m] > 0) { --v.mgDelay[m]; continue; }
            const float ph = v.mgPhase[m] / 65536.0f;
            float w;
            switch (bits & 3) {
                case 0: w = ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph; break;   // triangle
                case 1: w = 2 * ph - 1; break;                            // saw up
                case 2: w = 1 - 2 * ph; break;                            // saw down
                default: w = ph < 0.5f ? 1.0f : -1.0f; break;             // square
            }
            mg[m] = w;
        }
        tim_[v.timbre].mgPhase[0] = v.mgPhase[0];
        tim_[v.timbre].mgPhase[1] = v.mgPhase[1];
        const float modUp = mod_[ch] / 127.0f, modDn = modNeg_[ch] / 127.0f, at = aft_[ch] / 127.0f;

        // --- pitch, in 1/128 semitone
        const int oct = s8(o == p + P_OSC1 ? p[P_OCT1] : p[P_OCT2]);
        float d = (float)(v.pitchNote * 128 + oct * 1536 + 3 - v.root128);
        if (v.osc == 1) d += s8(p[P_INTERVAL]) * 128.0f;
        // Detune spreads the two oscillators by +-half the value (cents).
        const float det = s8(p[P_DETUNE]) * (v.osc ? 0.5f : -0.5f) + tb.detune + tuneCents_;
        d += det * 1.285f;
        // Pitch EG: level 99 = one octave (ASSUMED).
        const float pegVel = 1.0f + s8(o[O_PEG_LVEL]) / 99.0f * ((float)v.vel - 64.0f) / 63.0f;
        d += pEg * pegVel * 1536.0f;
        // Pitch MG: intensity 99 = one octave (ASSUMED); joystick +Y and
        // aftertouch add intensity.
        const float pmgInt = (p[P_PMG_INT] + p[P_JS_PMG] * modUp + p[P_AT_PMG] * at) / 99.0f;
        d += mg[0] * clampf(pmgInt, 0.0f, 1.0f) * 1536.0f;
        // Joystick X, range in semitones per program; aftertouch pitch likewise.
        const int b = bend_[ch];
        d += (float)b / 8192.0f * s8(p[P_JS_BEND]) * 128.0f;
        d += at * s8(p[P_AT_PITCH]) * 128.0f;
        delta128 = (int)lrintf(d);

        // --- cutoff register (EF83:1F58): additive in the log domain
        float cr = (float)(o[O_CUTOFF] * 0x52 + v.splitCutoff * 0x52);
        cr += (float)(v.pitchNote - o[O_VDF_KC]) * s8(o[O_VDF_KT]) / 99.0f * 0x47;   // ASSUMED
        const float egInt = o[O_VDF_EGI] / 99.0f * (1.0f + s8(o[O_VDF_IVEL]) / 99.0f * ((float)v.vel - 64.0f) / 63.0f);
        cr += fEg * clampf(egInt, 0.0f, 2.0f) * 0x1FFF;
        cr += mg[1] * (p[P_CMG_INT] + p[P_JS_CMG] * modDn + p[P_AT_CMG] * at) / 99.0f * 0x1000;   // ASSUMED
        cr += s8(p[P_JS_SWEEP]) / 99.0f * modDn * 0x1000;                                          // ASSUMED
        cr += s8(p[P_AT_CUT]) / 99.0f * at * 0x1000;                                               // ASSUMED
        cutReg = cr;

        // --- amplitude (EF83:1EE9): multiplicative
        float a = (aEg > 0 ? aEg : 0) * o[O_LEVEL] / 99.0f * (float)(0x8000 + v.splitLevel * kLevelUnit) / 32768.0f;
        const int vs = s8(o[O_VDA_VEL]);
        // Velocity through the firmware's exponential curve (CS:1BC3); the
        // sensitivity blends between flat and that curve (ASSUMED blend).
        const float vc = rom_.velCurve[v.vel & 127] / (float)rom_.velCurve[127];
        const float velGain = vs >= 0 ? 1.0f + vs / 99.0f * (vc - 1.0f)
                                      : 1.0f + (-vs) / 99.0f * (rom_.velCurve[127 - (v.vel & 127)] /
                                                                (float)rom_.velCurve[127] - 1.0f);
        a *= velGain;
        // Keyboard tracking of level: +-99 = +-6 dB per octave (ASSUMED).
        a *= powf(2.0f, s8(o[O_VDA_KT]) / 99.0f * ((float)v.pitchNote - o[O_VDA_KC]) / 12.0f * 1.0f);
        a *= 1.0f + s8(p[P_AT_AMP]) / 99.0f * at;
        a *= tb.level / 99.0f;
        amp = clampf(a, 0.0f, 4.0f);
    }

    // Pitch step 4.12, clamp 0x2FFF (EF83:001F)
    float ratio = exp2f((float)delta128 / 1536.0f);
    int32_t step = (int32_t)lrintf(ratio * 4096.0f);
    if (step > 0x2FFF) step = 0x2FFF;
    v.step = step;

    float cut = clampf(cutReg, 0.0f, (float)kCutMax);
    cut = floorf(cut);
    if (v.first) { v.cut = cut; v.amp = amp; v.first = false; }
    // Hardware ramp, modelled as linear across the update interval.
    v.cutInc = (cut - v.cut) / (float)kUpdate;
    v.ampInc = (amp - v.amp) / (float)kUpdate;

    // Voice end: VDA envelope finished at zero.
    // Voice end: the VDA envelope has finished at zero (a sustain of 0, or the
    // end of the release) and the ramp towards it has run out.
    if (!v.vdaEg.active && v.vdaEg.level <= 0 && amp <= 0.0f && v.amp <= 1e-6f) v.active = false;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void Engine::renderVoice(Voice& v, int frames) {
    const int16_t* base = v.base;
    const uint8_t* K = rom_.kernel;
    const uint16_t* k1t = rom_.vdf1k;
    const uint16_t* k2t = rom_.vdf2k;
    const int32_t endRel = (int32_t)v.endRel;
    const int32_t L = (int32_t)v.loopLen;
    const bool loop = v.loop;
    const int32_t below = -(int32_t)(base - rom_.pcm);   // first readable index
    float y1 = v.y1, y2 = v.y2;
    int i = 0;
    while (i < frames && v.active) {
        if (v.delayTicks > 0) {
            const int n = frames - i < v.delayTicks ? frames - i : v.delayTicks;
            v.delayTicks -= n;
            i += n;
            continue;
        }
        if (v.untilUpdate <= 0) {
            // The TG reports a finished one-shot and the driver frees the
            // channel at its next visit (analysis 7.3).
            if (v.oscDone) { v.active = false; break; }
            update(v);
            v.untilUpdate = kUpdate;
            if (!v.active) break;
        }
        int n = frames - i;
        if (n > v.untilUpdate) n = v.untilUpdate;
        for (int j = 0; j < n; ++j, ++i) {
            if (v.oscDone) {
                y1 += (rom_.vdf1k[(int)v.cut >> 1] * (1.0f / 8192.0f)) * (0.0f - y1);
                y2 += (rom_.vdf2k[(int)v.cut >> 1] * (1.0f / 8192.0f)) * (y1 - y2);
                const float out = y2 * v.amp;
                busA_[i] += out * v.panA; busB_[i] += out * v.panB;
                busC_[i] += out * v.panC; busD_[i] += out * v.panD;
                v.cut += v.cutInc; v.amp += v.ampInc;
                continue;
            }
            const int32_t ip = v.pos >> 12;
            const uint8_t* w = K + ((v.pos & 0xFFF) << 3);
            int32_t acc;
            if (ip - 3 >= below && ip + 4 <= endRel) {
                const int16_t* x = base + ip;
                acc = (int8_t)w[0] * x[4] + (int8_t)w[1] * x[3] + 2 * (int8_t)w[2] * x[2]
                    + 8 * (w[3] * x[1] + w[4] * x[0])
                    + 2 * (int8_t)w[5] * x[-1] + (int8_t)w[6] * x[-2] + (int8_t)w[7] * x[-3];
            } else {
                static const int mul[8] = {1, 1, 2, 8, 8, 2, 1, 1};
                acc = 0;
                for (int l = 0; l < 8; ++l) {
                    int32_t idx = ip + 4 - l;
                    if (loop && idx > endRel) idx -= L;       // taps past the end read the loop
                    if (idx < below) idx = below;
                    const int wl = (l == 3 || l == 4) ? (int)w[l] : (int)(int8_t)w[l];
                    acc += mul[l] * wl * base[idx];
                }
            }
            const float x = (float)acc * (1.0f / (1226.0f * 32768.0f));

            const int ci = (int)v.cut >> 1;
            const float k1 = k1t[ci] * (1.0f / 8192.0f), k2 = k2t[ci] * (1.0f / 8192.0f);
            y1 += k1 * (x - y1);
            y2 += k2 * (y1 - y2);
            const float out = y2 * v.amp;
            busA_[i] += out * v.panA;
            busB_[i] += out * v.panB;
            busC_[i] += out * v.panC;
            busD_[i] += out * v.panD;
            v.cut += v.cutInc;
            v.amp += v.ampInc;

            v.pos += v.step;
            if ((v.pos >> 12) >= endRel) {
                if (loop) {
                    do { v.pos -= L << 12; } while ((v.pos >> 12) >= endRel);
                } else {
                    v.oscDone = true;                         // one-shot ran out
                }
            }
        }
        v.untilUpdate -= n;
    }
    v.y1 = y1; v.y2 = y2;
    if (!v.active) {
        // The firmware puts a finished channel at the head of the queue.
        for (int k = 0; k < kVoices; ++k)
            if (&voice_[queue_[k]] == &v) { queueToFront(queue_[k]); break; }
        v.status = 0;
    }
}

void Engine::render(float* left, float* right, int frames) {
    while (frames > 0) {
        const int n = frames > kRenderBlock ? kRenderBlock : frames;
        memset(busA_, 0, sizeof(float) * n);
        memset(busB_, 0, sizeof(float) * n);
        memset(busC_, 0, sizeof(float) * n);
        memset(busD_, 0, sizeof(float) * n);
        for (Voice& v : voice_) if (v.active) renderVoice(v, n);
        if (bypassFx_) {
            for (int i = 0; i < n; ++i) {
                left[i] = busA_[i] + busC_[i];
                right[i] = busB_[i] + busD_[i];
            }
        } else {
            fx_.process(busA_, busB_, busC_, busD_, left, right, n);
        }
        left += n; right += n; frames -= n;
    }
}

}  // namespace m1

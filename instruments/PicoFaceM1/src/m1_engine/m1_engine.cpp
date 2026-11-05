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
// Envelopes (driver EF83:1642, 1687, 1904..1A0D, 1B9C..1EB5; doc/ENGINE.md)
// ---------------------------------------------------------------------------
namespace {
inline int32_t hi16(int64_t x) { return (int32_t)(x >> 16); }
inline int32_t sc(int p) { const int32_t x = (int32_t)(int8_t)p * 0x149; return x >> 8; }   // -99..99 -> -127..127
}  // namespace

void Engine::egSegment(Eg& e, int time, int target, int segIndex) {
    e.start = e.level;
    e.target = target;
    if (e.ref) {
        // Python reference: linear, 65536/rate updates, fall table for falls.
        const bool rising = e.target >= e.start || e.kind == Eg::Pitch;
        int t = time * 165;
        if (t > 0x3FFF) t = 0x3FFF;
        const int rate = (rising ? rom_.egRise : rom_.egFall)[t >> 6];
        e.n = 65536.0f / (float)(rate < 1 ? 1 : rate);
        e.k = 0;
        e.active = true;
        return;
    }
    const uint32_t f = e.factor[segIndex];
    int idx;
    if (e.kind == Eg::Pitch) {
        int32_t x = hi16((int64_t)time * 330 * f);
        if (x > 0x3FFF) x = 0x3FFF;
        idx = x >> 6;
    } else {
        idx = hi16((int64_t)time * 330 * f) >> 6;
        if (idx > 255) idx = 255;
    }
    if (idx < 0) idx = 0;
    e.falling = e.kind != Eg::Pitch && target < e.level;
    e.rate = (e.falling ? rom_.egFall : rom_.egRise)[idx];
    e.phase = e.falling ? 0xFFFF : 0;
    e.active = true;
}

void Engine::egStart(Eg& e, Eg::Kind kind, int startLevel, const int16_t* times, const int16_t* levels,
                     int n, int relTime, int relLevel) {
    e.kind = kind;
    e.ref = refMode_;
    e.level = startLevel;
    e.nseg = n;
    for (int i = 0; i < n; ++i) { e.segTime[i] = times[i]; e.segLevel[i] = levels[i]; }
    e.relTime = (int16_t)relTime;
    e.relLevel = (int16_t)relLevel;
    e.seg = 1;
    egSegment(e, times[0], levels[0], 0);
}

void Engine::egRelease(Eg& e) {
    e.nseg = 0;
    e.seg = 0;
    egSegment(e, e.relTime, e.relLevel, 3);
}

// One 20.8 ms update; returns the level in firmware units.
int32_t Engine::egStep(Eg& e) {
    if (!e.active) return e.level;
    bool done;
    if (e.ref) {
        e.k += 1;
        done = (float)e.k >= e.n;
        if (!done) e.level = e.start + (int32_t)((float)(e.target - e.start) * (float)e.k / e.n);
    } else if (!e.falling) {
        e.phase += e.rate;
        done = e.phase >= 0xFFDC;
        if (!done) e.level = e.start + hi16((int64_t)(e.target - e.start) * e.phase);
    } else {
        int32_t dec = hi16((int64_t)e.rate * e.phase);
        if (dec < 1) dec = 1;
        e.phase -= dec;
        // VDF ends at -48 dB of the segment, VDA at -48 dB, or -60 dB when it
        // falls to zero (EF83:1950, 1DE8).
        const int32_t thr = e.kind == Eg::Vdf ? 0x100 : (e.target == 0 ? 0x40 : 0xFF);
        done = e.phase < thr;
        if (!done) e.level = e.target + hi16((int64_t)(e.start - e.target) * e.phase);
    }
    if (done) {
        e.level = e.target;
        e.active = false;
        if (e.seg > 0 && e.seg < e.nseg) {
            const int s = e.seg++;
            egSegment(e, e.segTime[s], e.segLevel[s], s);
        }
    }
    return e.level;
}
// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
bool Engine::init(const RomTables& rom, int16_t* fxMem) {
    rom_ = rom;
    fx_.init(rom.fx, rom.pan101, fxMem);
    for (int i = 0; i < kVoices; ++i) { queue_[i] = (uint8_t)i; voice_[i] = Voice(); }
    // Drum keymaps (E066:2E48): each instrument fills its key and every key
    // below it that still holds what its key held.
    for (int k = 0; k < 4; ++k) {
        uint8_t* map = kitMap_[k];
        memset(map, 0xFF, 128);
        const uint8_t* kit = rom.kits + kKitLen * k;
        for (int i = 0; i < 30; ++i) {
            const uint8_t* e = kit + 7 * i;
            if (!e[0] || e[1] > 127) continue;
            const uint8_t old = map[e[1]];
            for (int key = e[1]; key >= 0 && map[key] == old; --key) map[key] = (uint8_t)i;
        }
    }
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
    for (int i = 0; i < 10; ++i) if (name_[i] == 0) name_[i] = ' ';   // the preload pads with NUL
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
    for (int i = 0; i < 10; ++i) if (name_[i] == 0) name_[i] = ' ';
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
    const int oscs = (mode == 1) ? 2 : 1;
    for (int o = 0; o < oscs; ++o) {
        const int v = takeChannel();
        if (voice_[v].active) killVoice(v);
        startVoice(v, t, o, note, vel);
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
                    startVoice(v, t, o, n, vl);
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
    if (vc.delayTicks > 0) { killVoice(v); return; }   // delayed OSC 2 not started yet
    vc.status = 1;
    queueBeforeHeld(v);
    // Hold (program byte 11 bit 1): no release; the sound runs to the end of
    // its envelope.
    const Timbre& tb = tim_[vc.timbre];
    if (tb.prog && (tb.prog[P_FLAGS] & 2)) return;
    egRelease(vc.pitchEg);
    egRelease(vc.vdfEg);
    egRelease(vc.vdaEg);
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
// Voice start (driver EF83:01C4, 0C6F, 0D45; doc/ENGINE.md "Voice")
// ---------------------------------------------------------------------------
namespace {
// EG time factor for each segment from the switch/polarity bytes (EF83:17C6,
// 1A54): bits 0..3 switch attack/decay/slope/release on, bits 4..7 flip the
// polarity; "+" makes higher keys / harder playing shorter.
void timeFactors(uint16_t* out, uint8_t kbdSw, uint8_t velSw, int timeKbd, int timeVel,
                 int note, int center, int V) {
    int32_t kk = 2 * sc(timeKbd) * (int8_t)(note - center);
    kk = -(kk < -0x4000 ? -0x4000 : (kk > 0x3FFF ? 0x3FFF : kk));
    int32_t kv = 4 * sc(timeVel) * V;
    kv = -(kv < -0x4000 ? -0x4000 : (kv > 0x3FFF ? 0x3FFF : kv));
    for (int k = 0; k < 4; ++k) {
        int32_t s = 0;
        if (kbdSw & (1 << k)) s = (kbdSw & (0x10 << k)) ? -kk : kk;
        if (velSw & (1 << k)) s += (velSw & (0x10 << k)) ? -kv : kv;
        s *= 2;
        if (s > 0x7FFF) s = 0x7FFF;
        if (s < -0x8000) s = -0x8000;
        out[k] = (uint16_t)(s + 0x8000);
    }
}
}  // namespace

void Engine::startVoice(int vi, int t, int osc, uint8_t note, uint8_t vel) {
    Voice& v = voice_[vi];
    const Timbre& tb = tim_[t];
    const uint8_t* p = tb.prog;
    const uint8_t* o = p + (osc ? P_OSC2 : P_OSC1);
    const int mode = p[P_OSC_MODE];

    // Zone and pitch note: octave, combi transpose and (OSC 2) interval,
    // folded back into 0..127 by octaves (E066:887E).
    int nE = note + tb.transpose;
    if (!refMode_)                        // the Python reference plays the note as is
        nE += 12 * s8(osc ? p[P_OCT2] : p[P_OCT1]) + (osc ? s8(p[P_INTERVAL]) : 0);
    while (nE > 127) nE -= 12;
    while (nE < 0) nE += 12;

    uint32_t start, loopA, end;
    bool loop;
    int rootKey, cents, tune128, level, cutTrim, decay;
    int panCode = (tb.pan & 0x80) ? -1 : (tb.pan & 0x0F);
    if (mode == 2) {
        // Drums: program byte 12 picks one of the four global kits. A kit
        // instrument covers its key and the free keys below it (E066:2E48).
        const int kit = p[P_MS1] & 3;
        const int i = kitMap_[kit][nE];
        if (i == 0xFF) return;
        const uint8_t* ins = rom_.kits + kKitLen * kit + 7 * i;
        if (ins[0] == 0 || ins[0] > rom_.nDrums) return;
        const DrumSound& d = rom_.drums[ins[0]];
        start = d.start; loopA = d.loop; end = d.end; loop = d.flags & 1;
        tune128 = d.tune128;
        // Tune in 10-cent steps, split into a root key and a fine part.
        const int T = s8(ins[3]);
        const int f = (T % 10) * 10, s = T / 10;
        if (f > 50)       { cents = f - 100; rootKey = ins[1] - s - 1; }
        else if (f < -50) { cents = f + 100; rootKey = ins[1] - s + 1; }
        else              { cents = f;       rootKey = ins[1] - s; }
        level = s8(ins[4]);
        decay = s8(ins[5]);
        cutTrim = 0;
        panCode = ins[2] & 0x0F;          // drum programs always use the instrument pan
    } else {
        const Multisound& ms = rom_.ms[clampi(osc ? p[P_MS2] : p[P_MS1], 0, 99)];
        const Split* sp = nullptr;
        for (int i = 0; i < ms.count; ++i) {
            const Split& s = rom_.splits[ms.first + i];
            if (nE <= s.top) { sp = &s; break; }
        }
        if (!sp) return;                  // keymap FF: no voice
        start = sp->start; loopA = sp->loop; end = sp->end;
        loop = sp->flags & 1;
        rootKey = (sp->flags & 2) ? note : sp->root;   // NT: root := played key
        cents = sp->tune;
        tune128 = sp->tune128;
        level = sp->level;
        cutTrim = sp->cutoff;
        decay = sp->decay;
        if (panCode < 0) panCode = sp->pan;
    }

    v = Voice();
    v.active = true;
    v.status = 0x80;
    v.timbre = (uint8_t)t;
    v.osc = (uint8_t)osc;
    v.note = note;
    v.vel = vel;
    v.nE = nE;
    v.base = rom_.pcm + start;
    v.endRel = (int32_t)(end - start);
    v.loopLen = (int32_t)(end - loopA);
    v.loop = loop && v.loopLen > 0;
    v.pos = 0;
    v.root128 = rootKey * 128 - sc(cents) + tune128;

    // Voice pan (EF83:0337): register word from DS:0854, A/B in 6 bits each,
    // C and D on/off.
    if (panCode < 0 || panCode >= 14) panCode = 5;
    const uint16_t pr = rom_.panReg[panCode];
    v.panA = (pr & 63) / 63.0f;
    v.panB = ((pr >> 6) & 63) / 63.0f;
    v.panC = (float)((pr >> 12) & 1);
    v.panD = (float)((pr >> 13) & 1);

    const int V = vel - 64;
    if (refMode_) {
        v.refCutoff = o[O_CUTOFF] * 0x52 + cutTrim * 0x52;
        v.refLevel = level;
    } else {
        // Pitch EG intensity and time factor (EF83:1687)
        v.pegI = 0x1FC2 + sc(o[O_PEG_LVEL]) * V;
        const int32_t ft = 0x8000 - ((s8(o[O_PEG_TVEL]) * V * 1316) >> 8);
        for (int k = 0; k < 4; ++k) v.pitchEg.factor[k] = (uint16_t)clampi(ft, 0, 0xFFFF);
        // Cutoff key terms (EF83:1846): a built-in tracking above note 32,
        // the program's tracking around its centre, the split's trim.
        int32_t K = nE * 0x47 - 0x900;
        if (K < 0) K = 0;
        K += sc(o[O_VDF_KT]) * (int8_t)(note - o[O_VDF_KC]) + cutTrim * 0x52;
        v.cutK = clampi(K, -0x2000, 0x1FFF);
        const int iv = s8(o[O_VDF_IVEL]);
        const int m = iv >= 0 ? vel - 127 : vel;
        v.vdfI = hi16((int64_t)o[O_VDF_EGI] * 330 * 2 * (0x3FFF + sc(iv) * m));
        // Amplitude (EF83:1AF3, 1EE9)
        int32_t A = hi16((int64_t)o[O_LEVEL] * 0xA5 * (0x8000 + level * 330));
        if (A > 0x1FFF) A = 0x1FFF;
        int32_t KT = sc(o[O_VDA_KT]) * (int8_t)(note - o[O_VDA_KC]);
        KT = clampi(KT, -0x800, 0x7FF) * 4;
        v.vdaA = A + KT;
        const int vs = s8(o[O_VDA_VEL]);
        const int mv = vs >= 0 ? vel - 128 : vel - 1;
        const int idx = (((sc(vs) * mv + 0x3F01) << 2) & 0xFFFF) >> 8;
        v.velG = rom_.velCurve[idx];
        timeFactors(v.vdfEg.factor, o[O_VDF_TKBD_SW], o[O_VDF_TVEL_SW], o[O_VDF_TKBD], o[O_VDF_TVEL],
                    note, o[O_VDF_KC], V);
        timeFactors(v.vdaEg.factor, o[O_VDA_TKBD_SW], o[O_VDA_TVEL_SW], o[O_VDA_TKBD], o[O_VDA_TVEL],
                    note, o[O_VDA_KC], V);
        // A drum's Decay scales the VDA decay segment.
        v.vdaEg.factor[1] = (uint16_t)clampi((int32_t)(((int64_t)v.vdaEg.factor[1] * (0x8000 + decay * 330)) >> 15),
                                             0, 0xFFFF);
    }

    // Envelopes. Pitch: start level, attack, decay to 0, release (linear,
    // rise table only). VDF: from 0 through AL, BP, SL, release to RL.
    // VDA: from 0 through AL, BP, SL, release to 0.
    {
        auto L = [](int prm) { return (s8((uint8_t)prm) * 4 * 0x52BF) >> 8; };
        const int16_t tm[2] = {o[O_PEG_AT], o[O_PEG_DT]};
        const int16_t lv[2] = {(int16_t)L(o[O_PEG_AL]), 0};
        egStart(v.pitchEg, Eg::Pitch, L(o[O_PEG_SL]), tm, lv, 2, o[O_PEG_RT], L(o[O_PEG_RL]));
    }
    {
        const int16_t tm[3] = {o[O_VDF_AT], o[O_VDF_DT], o[O_VDF_ST]};
        const int16_t lv[3] = {(int16_t)(s8(o[O_VDF_AL]) * 330), (int16_t)(s8(o[O_VDF_BP]) * 330),
                               (int16_t)(s8(o[O_VDF_SL]) * 330)};
        egStart(v.vdfEg, Eg::Vdf, 0, tm, lv, 3, o[O_VDF_RT], s8(o[O_VDF_RL]) * 330);
    }
    {
        const int16_t tm[3] = {o[O_VDA_AT], o[O_VDA_DT], o[O_VDA_ST]};
        const int16_t lv[3] = {(int16_t)(o[O_VDA_AL] * 330), (int16_t)(o[O_VDA_BP] * 330),
                               (int16_t)(o[O_VDA_SL] * 330)};
        egStart(v.vdaEg, Eg::Vda, 0, tm, lv, 3, o[O_VDA_RT], 0);
    }

    // OSC 2 delay start, in 1.3 ms ticks (DS:776E) -> samples.
    if (osc == 1 && !refMode_)
        v.delayTicks = (int)((float)rom_.delayStart[clampi(p[P_DELAY], 0, 99)] * 40.625f);

    // MGs: without key sync a new voice takes over the timbre's running phase.
    for (int m = 0; m < 2; ++m) {
        const uint8_t bits = p[m ? P_CMG_BITS : P_PMG_BITS];
        v.mgPhase[m] = (bits & 0x80) ? 0 : tim_[t].mgPhase[m];
        v.mgAcc[m] = 0;
    }
    v.first = true;
    v.untilUpdate = 0;
}

// Pitch register from 1/128 semitone (EF83:001F, tables DS:0616 / DS:0748).
uint16_t Engine::pitchStep(int32_t d) const {
    int32_t hi = (d >> 7) + 0x80;
    const int lo = d & 0x7F;
    if (hi >= 0x99) return 0x2FFF;
    if (hi < 0) hi = 0;
    const uint32_t S = rom_.pitchS[hi];
    uint32_t step = S + ((S * rom_.pitchF[lo]) >> 16);
    if (step >= 0x3000) step = 0x2FFF;
    return (uint16_t)step;
}

// ---------------------------------------------------------------------------
// Control update, every 20.8 ms per voice (EF83:1785, 1F58, 1EE9, 1533)
// ---------------------------------------------------------------------------
namespace {
int32_t mgWave(int shape, uint16_t ph) {
    switch (shape & 3) {
        case 0: {                                        // triangle, from 0 rising
            uint32_t x = (ph + 0x4000u) & 0xFFFFu;
            if (x & 0x8000u) x = ~x & 0xFFFFu;
            return (int32_t)(int16_t)(uint16_t)((x - 0x4000u) << 1);
        }
        case 1: return (int32_t)(int16_t)(uint16_t)(ph ^ 0x8000u);                  // saw up
        case 2: return (int32_t)(int16_t)(uint16_t)(((0x10000u - ph) & 0xFFFFu) ^ 0x8000u);  // saw down
        default: return ph < 0x8000 ? 0 : 0x7FFF;          // square, unipolar
    }
}
}  // namespace

void Engine::update(Voice& v) {
    Timbre& tb = tim_[v.timbre];
    const uint8_t* p = tb.prog;
    const uint8_t* o = p + (v.osc ? P_OSC2 : P_OSC1);

    const int32_t pL = egStep(v.pitchEg);
    const int32_t fL = egStep(v.vdfEg);
    const int32_t aL = egStep(v.vdaEg);

    float cut, amp;
    int32_t step;
    if (refMode_) {
        // Exactly the Python reference's render_voice().
        const float fEg = (float)fL / (99.0f * 330.0f), aEg = (float)aL / (99.0f * 330.0f);
        float cr = (float)v.refCutoff + fEg * o[O_VDF_EGI] / 99.0f * 0x1FFF;
        cut = floorf(clampf(cr, 0.0f, (float)kCutMax));
        amp = (aEg > 0 ? aEg : 0) * o[O_LEVEL] / 99.0f * (float)(0x8000 + v.refLevel * 330) / 32768.0f;
        step = (int32_t)lrintf(exp2f((float)(v.nE * 128 + 3 - v.root128) / 1536.0f) * 4096.0f);
        if (step > 0x2FFF) step = 0x2FFF;
    } else {
        const int ch = tb.midiCh & 15;
        const int AT = aft_[ch], JYp = mod_[ch], JYn = modNeg_[ch], BEND = bend_[ch];
        // --- MGs: pitch (bytes 19..22) and VDF (23..26)
        int32_t mgOut[2] = {0, 0};
        for (int m = 0; m < 2; ++m) {
            const uint8_t bits = p[m ? P_CMG_BITS : P_PMG_BITS];
            const int jy = m ? JYn : JYp;
            int fi = p[m ? P_CMG_FREQ : P_PMG_FREQ] + ((p[m ? P_JS_CMG_F : P_JS_PMG_F] * jy) >> 3);
            if (fi > 99) fi = 99;
            const uint16_t inc = rom_.mgFreq[clampi(fi, 0, 99)];
            int32_t wave = 0;
            if (inc) {
                v.mgPhase[m] = (uint16_t)(v.mgPhase[m] + inc);
                const bool en = v.osc ? (bits & 0x40) : (bits & 0x20);
                if (en) wave = mgWave(bits, v.mgPhase[m]);
            }
            tb.mgPhase[m] = v.mgPhase[m];
            uint32_t acc = v.mgAcc[m] + rom_.mgDelay[clampi(p[m ? P_CMG_DELAY : P_PMG_DELAY], 0, 99)];
            if (acc > 0xFFFF) acc = 0xFFFF;
            v.mgAcc[m] = (uint16_t)acc;
            const uint32_t fade = acc >= 0x8000 ? (acc * 2) & 0xFFFF : 0;
            const int32_t extra = hi16((int64_t)(p[m ? P_AT_CMG : P_AT_PMG] * AT
                                                 + p[m ? P_JS_CMG : P_JS_PMG] * jy) * 0xA480);
            const uint16_t* intT = m ? rom_.mgIntF : rom_.mgIntP;
            int32_t depth = hi16((int64_t)fade * intT[clampi(p[m ? P_CMG_INT : P_PMG_INT], 0, 99)]) + extra;
            if (depth > 0x1FFF) depth = 0x1FFF;
            mgOut[m] = hi16((int64_t)depth * wave) * 2;
        }
        // --- pitch, 1/128 semitone (EF83:178B)
        const int32_t t1e = hi16((int64_t)(s8(p[P_JS_BEND]) * 256) * (BEND * 4))
                          + (AT == 127 ? s8(p[P_AT_PITCH]) * 128 : s8(p[P_AT_PITCH]) * AT)
                          + (((tb.detune + tuneCents_) * 0x29) >> 5);
        const int32_t egP = hi16((int64_t)hi16((int64_t)pL * v.pegI) * 0x6000);
        const int32_t a = (v.osc ? (0x29 * s8(p[P_DETUNE])) >> 4 : 0) + (v.nE << 8);
        const int32_t delta = (a >> 1) + hi16((int64_t)0x3000 * mgOut[0]) + t1e + egP;
        step = pitchStep(delta + 3 - v.root128);
        // --- cutoff register (EF83:1F58, E066:896C)
        int32_t c1 = ((o[O_CUTOFF] * 0x52BF) >> 8) + v.cutK;
        c1 = clampi(c1, 0, 0x1FFF);
        const int32_t t1c = hi16((int64_t)(hi16((int64_t)(s8(p[P_JS_SWEEP]) * 256) * (BEND * 4))
                                           + s8(p[P_AT_CUT]) * AT) * 0x5240);
        const int32_t c = c1 + mgOut[1] + t1c + hi16((int64_t)fL * v.vdfI);
        cut = (float)clampi(c, 0, 0x1FFF);
        // --- amplitude (EF83:1EE9): every factor multiplies
        const int32_t atA = hi16((int64_t)s8(p[P_AT_AMP]) * AT * 2 * 0x5240);
        const int32_t A2 = clampi(v.vdaA + atA, 0, 0x1FFF);
        const int32_t t18 = (tb.level * 127 * 0x524) >> 8;
        const int32_t out = hi16((int64_t)hi16((int64_t)hi16((int64_t)(A2 << 3) * v.velG) * t18) * aL);
        amp = (float)(out > 0 ? out : 0) * (1.0f / 8192.0f);
    }
    v.step = step;

    if (v.first) { v.cut = cut; v.amp = amp; v.first = false; }
    // Hardware ramp, modelled as linear across the update interval.
    v.cutInc = (cut - v.cut) / (float)kUpdate;
    v.ampInc = (amp - v.amp) / (float)kUpdate;

    // Voice end: the VDA envelope has finished at zero (a sustain of 0, or the
    // end of the release) and the ramp towards it has run out.
    if (!v.vdaEg.active && v.vdaEg.level <= 0 && amp <= 0.0f && v.amp <= 1e-6f) v.active = false;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
// The VDF: two MB87404 chips in series, both fed the same cutoff register
// (VDF 1 clamped to 0x1FD2). Each chip is taken as two one-pole stages: a
// plain one with its k table (T1: 0.707 k in VDF 1, k in VDF 2) and a
// bilinear one whose pole is the chip's second table T0 = 2k - 1 (gain
// (1 + T0)/2, so DC gain 1, zero at Nyquist). Four poles, 24 dB/oct
// (doc/ENGINE.md, "VDF"). Reference mode: the earlier two one-pole stages.
inline float Engine::vdf(Voice& v, float x) {
    const int ci = (int)v.cut;
    const int i1 = (ci > kCutMax ? kCutMax : ci) >> 1, i2 = ci >> 1;
    const float k1 = rom_.vdf1k[i1] * (1.0f / 8192.0f), k2 = rom_.vdf2k[i2] * (1.0f / 8192.0f);
    v.y1 += k1 * (x - v.y1);
    if (refMode_) {
        v.y2 += k2 * (v.y1 - v.y2);
        return v.y2;
    }
    const float a1 = -rom_.vdf1t0[i1] * (1.0f / 8192.0f);
    v.y2 = a1 * v.y2 + (1.0f - a1) * 0.5f * (v.y1 + v.x1);
    v.x1 = v.y1;
    v.y3 += k2 * (v.y2 - v.y3);
    const float a2 = -rom_.vdf2t0[i2] * (1.0f / 8192.0f);
    v.y4 = a2 * v.y4 + (1.0f - a2) * 0.5f * (v.y3 + v.x3);
    v.x3 = v.y3;
    return v.y4;
}

void Engine::renderVoice(Voice& v, int frames) {
    const int16_t* base = v.base;
    const uint8_t* K = rom_.kernel;
    const int32_t endRel = (int32_t)v.endRel;
    const int32_t L = (int32_t)v.loopLen;
    const bool loop = v.loop;
    const int32_t below = -(int32_t)(base - rom_.pcm);   // first readable index
    // A loop is [loop, end): its period is end - loop, and the word at `end`
    // is not part of it -- the ROM repeats with exactly that period before the
    // end, and the word after it does not continue the loop. So a tap at the
    // end address already reads from the loop start. (The analysis' Python
    // reference wraps one word later; the reference mode keeps that.)
    const int32_t wrapAt = refMode_ ? endRel + 1 : endRel;
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
                const float out = vdf(v, 0.0f) * v.amp;
                busA_[i] += out * v.panA; busB_[i] += out * v.panB;
                busC_[i] += out * v.panC; busD_[i] += out * v.panD;
                v.cut += v.cutInc; v.amp += v.ampInc;
                continue;
            }
            const int32_t ip = v.pos >> 12;
            const uint8_t* w = K + ((v.pos & 0xFFF) << 3);
            int32_t acc;
            if (ip - 3 >= below && ip + 4 < wrapAt) {
                const int16_t* x = base + ip;
                acc = (int8_t)w[0] * x[4] + (int8_t)w[1] * x[3] + 2 * (int8_t)w[2] * x[2]
                    + 8 * (w[3] * x[1] + w[4] * x[0])
                    + 2 * (int8_t)w[5] * x[-1] + (int8_t)w[6] * x[-2] + (int8_t)w[7] * x[-3];
            } else {
                static const int mul[8] = {1, 1, 2, 8, 8, 2, 1, 1};
                acc = 0;
                for (int l = 0; l < 8; ++l) {
                    int32_t idx = ip + 4 - l;
                    if (loop && idx >= wrapAt) idx -= L;      // taps past the end read the loop
                    if (idx < below) idx = below;
                    const int wl = (l == 3 || l == 4) ? (int)w[l] : (int)(int8_t)w[l];
                    acc += mul[l] * wl * base[idx];
                }
            }
            const float x = (float)acc * (1.0f / (1226.0f * 32768.0f));

            const float out = vdf(v, x) * v.amp;
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

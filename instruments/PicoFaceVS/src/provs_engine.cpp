// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "provs/engine.h"

#include <cmath>

#include "provs/fw_tables.h"
#include "provs/preset_map.h"
#if PROVS_HAS_PVM
#include "provs/pvm_map.h"
#endif

namespace provs {

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void Engine::init(double sampleRate) {
    sr_ = float(sampleRate);
    fx_.init(sampleRate);
    allNotesOff();
    for (auto& layer : lfo_) layer[0] = layer[1] = Lfo{};
    ctl_ = 0;
    applyParams();
}

void Engine::setParams(const Params& p) { pNext_ = p; paramsDirty_ = true; }
void Engine::setLayer2(const Params& p) { p2Next_ = p; paramsDirty_ = true; }

void Engine::applyParams() {
    p_ = pNext_;
    p2_ = p2Next_;
    paramsDirty_ = false;
}

// --- Programme ---------------------------------------------------------------

// Die Bank liegt im RAM: 100 Programme zu 116 Feldern (ein Byte je Feld).
namespace {
constexpr int kBankSize = 100;
constexpr int kFields = 116;
constexpr int kProgramBytes = 82;        // bitgepackt
uint8_t gBank[kBankSize][kFields];
int gBankCount = 0;                      // 0 = noch nicht angelegt

void bankInit() {
    for (auto& prog : gBank)
        for (int i = 0; i < kFields; ++i) prog[i] = kInitProgram[i];
    gBankCount = 1;
}
inline void bankNeed() { if (gBankCount == 0) bankInit(); }

// 164 Nibbles (High-Nibble zuerst) -> 82 Bytes -> 116 Felder, MSB-first
// nach der Breitentabelle der Firmware
bool unpackProgram(const uint8_t* nib, uint8_t* fields) {
    uint8_t rec[kProgramBytes];
    for (int i = 0; i < kProgramBytes; ++i) {
        if ((nib[2 * i] | nib[2 * i + 1]) & 0xF0) return false;
        rec[i] = uint8_t((nib[2 * i] << 4) | nib[2 * i + 1]);
    }
    int bit = 0;
    for (int i = 0; i < kFields; ++i) {
        int v = 0;
        for (int b = 0; b < kProgramFieldBits[i]; ++b, ++bit)
            v = (v << 1) | ((rec[bit >> 3] >> (7 - (bit & 7))) & 1);
        fields[i] = uint8_t(v);
    }
    return true;
}
}  // namespace

void Engine::resetVsBank() { bankInit(); }

int Engine::loadVsSysex(const uint8_t* d, size_t n) {
    bankNeed();
    constexpr size_t kNib = 2 * kProgramBytes;
    int loaded = 0;
    size_t i = 0;
    while (i + 4 <= n) {
        if (d[i] != 0xF0) { ++i; continue; }
        if (d[i + 1] != 0x01 || d[i + 2] != 0x0A || d[i + 3] > kBankSize) { ++i; continue; }
        const int slot = d[i + 3];
        const int count = slot == kBankSize ? kBankSize : 1;
        const size_t end = i + 4 + size_t(count) * kNib;      // Position des F7
        if (end >= n || d[end] != 0xF7) { ++i; continue; }
        // erst vollstaendig pruefen (nur Nibbles erlaubt), dann uebernehmen
        bool ok = true;
        for (size_t k = i + 4; k < end && ok; ++k) ok = (d[k] & 0xF0) == 0;
        if (ok) {
            const int first = count == kBankSize ? 0 : slot;
            for (int k = 0; k < count; ++k) unpackProgram(d + i + 4 + size_t(k) * kNib, gBank[first + k]);
            if (count == kBankSize) gBankCount = kBankSize;
            else if (slot + 1 > gBankCount) gBankCount = slot + 1;
            loaded += count;
        }
        i = end + 1;
    }
    return loaded;
}

int Engine::presetCount() { bankNeed(); return gBankCount; }

void Engine::setPreset(int index) {
    bankNeed();
    if (index < 0 || index >= gBankCount) return;
    Params p = pNext_;
    const uint8_t* pr = gBank[index];
    mapVsPreset(pr, p);
    // Split/Double: Feld 61 nennt das verknuepfte Programm fuer die 2. Lage
    if (p.keyMode != kKeyNormal) {
        const int link = pr[61] < gBankCount ? pr[61] : 0;
        Params q;
        mapVsPreset(gBank[link], q);
        setLayer2(q);
    }
    setParams(p);
}

void Engine::presetName(int index, char* buf) {
    bankNeed();
    for (int i = 0; i < 8; ++i) buf[i] = ' ';
    buf[8] = 0;
    if (index < 0 || index >= gBankCount) return;
    for (int i = 0; i < 8; ++i) buf[i] = kNameChars[gBank[index][78 + i] & 31];
}

#if PROVS_HAS_PVM
namespace {
constexpr int kPvmBankSize = 32;
constexpr int kPvmPatchBytes = 64;
constexpr int kPvmHeader = 8;
uint8_t gPvmBank[kPvmBankSize][kPvmPatchBytes];
char gPvmNames[kPvmBankSize][9];
int gPvmCount = 0;
}  // namespace

void Engine::resetPvmBank() { gPvmCount = 0; }

int Engine::loadPvmSysex(const uint8_t* d, size_t n) {
    if (!kPvmHasEnvTable) return 0;
    constexpr size_t kLen = kPvmHeader + kPvmBankSize * kPvmPatchBytes + 2;   // Pruefbyte, F7
    for (size_t i = 0; i + kLen <= n; ++i) {
        if (d[i] != 0xF0 || d[i + 1] != 0x00 || d[i + 2] != 0x20 || d[i + 3] != 0x32) continue;
        if (d[i + kLen - 1] != 0xF7) continue;
        const uint8_t* src = d + i + kPvmHeader;
        for (int k = 0; k < kPvmBankSize; ++k) {
            for (int b = 0; b < kPvmPatchBytes; ++b) gPvmBank[k][b] = src[k * kPvmPatchBytes + b];
            for (int c = 0; c < 8; ++c) {
                const uint8_t ch = gPvmBank[k][55 + c];
                gPvmNames[k][c] = (ch >= 32 && ch < 127) ? char(ch) : ' ';
            }
            gPvmNames[k][8] = 0;
        }
        gPvmCount = kPvmBankSize;
        return kPvmBankSize;
    }
    return 0;
}

int Engine::pvmPatchCount() { return gPvmCount; }

const char* Engine::pvmPatchName(int index) {
    if (index < 0 || index >= gPvmCount) return "";
    return gPvmNames[index];
}

void Engine::setPvmPatch(int index) {
    if (index < 0 || index >= gPvmCount) return;
    Params p = pNext_;
    mapPvmPatch(gPvmBank[index], p);
    setParams(p);
}
#else
void Engine::resetPvmBank() {}
int Engine::loadPvmSysex(const uint8_t*, size_t) { return 0; }
int Engine::pvmPatchCount() { return 0; }
const char* Engine::pvmPatchName(int) { return ""; }
void Engine::setPvmPatch(int) {}
#endif

// --- Envelopes -----------------------------------------------------------------

// Pegel- und Zeitfolge wie die Arbeitstabellen der Firmware (@0x479C, @0x47EE):
// Segmente 0..2 vorwaerts (L0->L1->L2->L3), 3..5 rueckwaerts (L3->L2->L1->L0).
static inline float envPoint(const Env5& p, int i) {
    static const int idx[7] = {0, 1, 2, 3, 2, 1, 0};
    return p.level[idx[i]];
}
static inline float envSegTime(const Env5& p, int seg) {
    static const int idx[6] = {0, 1, 2, 2, 1, 0};
    return p.time[idx[seg]];
}

void Engine::envSegment(EnvState& e, const Env5& p, int stage) const {
    e.stage = stage;
    if (stage == kEnvRelease) {
        // Release: exponentiell auf L4 (Firmware: v = L4 + (v-L4)*k je Tick)
        e.target = p.level[4];
        const float tau = (altRelease_ && p.releaseTauAlt >= 0.0f) ? p.releaseTauAlt : p.releaseTau;
        e.coef = tau > 0.0f ? 1.0f - std::exp(-1.0f / (tau * sr_)) : 1.0f;
        e.left = 1;
        return;
    }
    // Jedes Segment beginnt exakt auf seinem Startpunkt (Firmware @0x0A48);
    // beim Loop 1..3 ist das der Sprung zurueck.
    e.v = envPoint(p, stage);
    e.target = envPoint(p, stage + 1);
    int n = int(envSegTime(p, stage) * sr_);
    if (n < 1) n = 1;
    e.left = n;
    e.inc = (e.target - e.v) / float(n);
    e.coef = p.curve > 0.0f ? 1.0f - std::exp(-p.curve / float(n)) : 0.0f;
}

void Engine::envStart(EnvState& e, const Env5& p) const {
    // Wiederholzaehler wie @0x4756: ohne Loop 1, mit Loop repeat+1, 0 = endlos
    e.repeat = p.loop == 0 ? 1 : (p.repeat ? p.repeat + 1 : 0);
    envSegment(e, p, 0);
}

void Engine::envRelease(EnvState& e, const Env5& p) const {
    if (e.stage < kEnvRelease) envSegment(e, p, kEnvRelease);
}

void Engine::envTick(EnvState& e, const Env5& p) const {
    if (e.left <= 0) return;                 // haelt oder fertig
    if (e.stage == kEnvRelease) {
        e.v += (e.target - e.v) * e.coef;
        // Ende wie in der Firmware: Rest unter 1 LSB der 16-Bit-Huellkurve
        if (std::fabs(e.target - e.v) < 1.0f / 32768.0f) {
            e.v = e.target; e.left = 0; e.stage = kEnvDone;
        }
        return;
    }
    if (p.curve > 0.0f) e.v += (e.target - e.v) * e.coef;
    else e.v += e.inc;
    if (--e.left > 0) return;
    e.v = e.target;
    const int loop = p.loop > 6 ? 3 : p.loop;
    const int end = loop >= 4 ? 10 - loop : 3;          // erstes nicht gespieltes Segment
    const int next = e.stage + 1;
    if (next < end) { envSegment(e, p, next); return; }
    if (--e.repeat == 0) { e.stage = kEnvHold; return; }
    envSegment(e, p, loop >= 4 ? loop - 4 : loop - 1);
}

// --- Tonhoehe --------------------------------------------------------------------

// Firmware @0x5D12: cent = (Note-27)*100 + Coarse*100 + Fine, ganzzahlig.
// Hier relativ zu A4 (Note 69) = 440 Hz.
float Engine::oscHz(int note, int osc) const {
    const OscParams& o = p_.osc[osc & 3];
    const float cents = float((note - 69 + p_.transpose) * 100 + o.coarse * 100 + o.fine)
                      + bend_ * p_.bendRange * 100.0f + p_.tuneCents;
    return 440.0f * std::exp2(cents / 1200.0f);
}

// --- Modulation ----------------------------------------------------------------

// Betrag 0..99 ueber die lineare Tabelle @0x6590 (99 = volle Skala)
static inline float amt99(int v) {
    if (v > 99) v = 99; else if (v < -99) v = -99;
    return float(v) / 99.0f;
}

// Zwei globale LFOs wie in der ISR @0x8F6: Phase 16 Bit, Schritt je Tick aus
// kLfoRateStep, Ausgang = Form * Betrag. Der zweite LFO sieht den neuen Wert
// des ersten, der erste den alten des zweiten (Reihenfolge der Firmware).
void Engine::lfoAdvance(int samples, int layer) {
    const Params& P = lp(layer);
    Lfo* L = lfo_[layer];
    const float ticks = float(samples) / (kFwTickSeconds * sr_);
    const float press = pressure_ * amt99(P.pressAmount);
    for (int i = 0; i < 2; ++i) {
        const LfoParams& lp = P.lfo[i];
        const LfoParams& op = P.lfo[1 - i];
        const float other = L[1 - i].out;
        // Rate: Tabellenwert + anderer LFO (Skala /128), nie negativ
        float step = lp.hz > 0.0f
            ? lp.hz * 2.0f * 65536.0f * kFwTickSeconds      // Dreieck: 2 Durchlaeufe je Periode
            : float(kLfoRateStep[lp.rate > 99 ? 99 : (lp.rate < 0 ? 0 : lp.rate)]);
        if (op.dest & kLfoOtherRate) step += other * 256.0f;
        if (P.pressDest & (i == 0 ? kPressLfo1Rate : kPressLfo2Rate)) step += press * 8192.0f;
        if (step < 0.0f) step = 0.0f;
        // Betrag: Grundwert + Mod-Wheel + anderer LFO (Skala /4), 0..1
        float amount = amt99(lp.amount);
        if (P.wheelDest & (i == 0 ? kWheelLfo1 : kWheelLfo2)) amount += wheel_;
        if (op.dest & kLfoOtherAmount) amount += other * 0.25f;
        if (P.pressDest & (i == 0 ? kPressLfo1Amount : kPressLfo2Amount)) amount += press;
        amount = clampf(amount, 0.0f, 1.0f);

        Lfo& l = L[i];
        const bool wraps = lp.shape == kLfoSaw || lp.shape == kLfoRamp;
        if (wraps) l.dir = lp.shape == kLfoSaw ? -1.0f : 1.0f;
        l.phase += l.dir * step * ticks / 32768.0f;
        if (wraps) {
            while (l.phase >= 1.0f) l.phase -= 2.0f;
            while (l.phase < -1.0f) l.phase += 2.0f;
        } else if (l.phase >= 1.0f || l.phase < -1.0f) {
            // am Rand umkehren und einen neuen Zufallswert ziehen
            l.phase = l.phase >= 1.0f ? 2.0f - l.phase : -2.0f - l.phase;
            l.phase = clampf(l.phase, -1.0f, 1.0f);
            l.dir = -l.dir;
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
            l.rnd = float(int32_t(rng_)) * (1.0f / 2147483648.0f);
        }
        float shape = l.phase;
        if (lp.shape == kLfoSquare) shape = l.dir;
        else if (lp.shape == kLfoRandom) shape = l.rnd;
        l.out = shape * amount;
    }
}

// Alles, was die Firmware je Tick pro Stimme rechnet: Tonhoehe, Cutoff,
// VCA-Pegel, Pan und die Mix-Verschiebung.
void Engine::voiceControl(Voice& v, bool snap) {
    // Die neuen Werte werden nicht gesprungen, sondern bis zum naechsten
    // Steuertakt linear angefahren (snap = sofort, beim Notenstart).
    const float before[10] = {v.fcoef, v.level, v.panL, v.panR, v.modX, v.modY,
                              v.inc[0], v.inc[1], v.inc[2], v.inc[3]};
    const Params& P = lp(v.layer);
    const Lfo* L = lfo_[v.layer];
    const float fenvMod = v.filt.v * amt99(P.fenvAmount);   // -1 .. +1
    // Pressure (@0x3B58): Wert * Betrag; volle Skala auf Pegel, Pan und
    // LFO-Betrag, /4 auf Filter, Mix und LFO-Rate, /64 auf die Tonhoehe
    const float press = pressure_ * amt99(P.pressAmount);

    // Tonhoehe: LFOs +-256 Cent (Ausgang /128), Filter-Env +-1024 Cent (/32)
    float common = bend_ * p_.bendRange * 100.0f + p_.tuneCents + v.detune + v.glide;
    if (P.fenvDest & kFenvFreq) common += fenvMod * 1024.0f;
    for (int i = 0; i < 4; ++i) {
        const OscParams& o = P.osc[i];
        const float osc = float((v.note - 69 + P.transpose) * 100 + o.coarse * 100 + o.fine);
        const float base = osc + bend_ * p_.bendRange * 100.0f + p_.tuneCents;
        float cents = osc + common;
        for (int k = 0; k < 2; ++k)
            if (P.lfo[k].dest & (kLfoFreqA << i)) cents += L[k].out * 256.0f;
        if (P.pressDest & (kPressFreqA << i)) cents += press * 512.0f;
        v.inc[i] = 440.0f * std::exp2(cents / 1200.0f) * float(kWaveFrames) / sr_;
        // Mip-Stufe nach der unmodulierten Tonhoehe: hoechster Oberton unter
        // der halben Abtastrate, kein Umschalten im Vibrato
        const float maxHarm = 0.5f * sr_ / (440.0f * std::exp2(base / 1200.0f));
        int mip = 0;
        while (mip < kWaveMips - 1 && float(kMipHarmonics[mip]) > maxHarm) ++mip;
        const int w = o.wave & 127;
        v.tabScale[i] = kWaveDataGain / 32767.0f;
        if (w >= kWaveCountVS) {
            v.tab[i] = nullptr;              // 127 = Rauschen
#if PROVS_HAS_PVM
        } else if (w < kPvmUserWaveCount) {
            // Wellen 0..31 stehen nicht im ROM; wenn vorhanden, aus den Behringer-Daten
            v.tab[i] = kPvmUserWaves + (w * kWaveMips + mip) * kWaveFrames;
            v.tabScale[i] = kPvmUserWaveGain / 32767.0f;
#endif
        } else {
            v.tab[i] = kWaveDataVS + (w * kWaveMips + mip) * kWaveFrames;
        }
    }

    // Filter (ISR @0x9B6): CV = Cutoff + Key + Env*Tiefe + LFOs/4, 0..1.
    // Kennlinie CV -> Hz: kVcf-Konstanten in engine.h (37 Hz .. 71 kHz,
    // 10,9 Oktaven).
    float depth = P.envAmount;
    if (P.velDest & kVelFilterEnv) depth = clampf(depth + v.velMod, 0.0f, 1.0f);
    float cv = P.cutoff;
    if (P.keyDest & kKeyFilter) cv = clampf(cv + v.keyMod, 0.0f, 1.0f);
    cv += depth * v.filt.v;
    for (int k = 0; k < 2; ++k)
        if (P.lfo[k].dest & kLfoFilter) cv += L[k].out * 0.25f;
    if (P.pressDest & kPressFilter) cv += press * 0.25f;
    cv = clampf(cv, 0.0f, 1.0f);
    // Glaettung am Frequenzeingang (Schaltplan: 2,2 uF gegen rund 1 kOhm = 2,1 ms)
    if (v.vcfCv < 0.0f || snap) v.vcfCv = cv;
    else v.vcfCv += (cv - v.vcfCv) * (1.0f - std::exp(-float(kControlSamples) / (0.0021f * sr_)));
    cv = v.vcfCv;
    float cut = kVcfHzAt0 * std::exp((cv * kVcfSpanMv - kVcfOffsetMv) / kVcfVtMv);
    if (cut > 0.45f * sr_) cut = 0.45f * sr_;
    // Koeffizient so, dass die vier Pole zusammen mit dem einen Sample
    // Laufzeit der Rueckkopplung bei der Cutoff-Frequenz 180 Grad drehen:
    // dort liegt dann die Resonanz, auch bei hohen Frequenzen.
    {
        const float w = 6.2831853f * cut / sr_;
        const float t = std::tan(0.78539816f - 0.25f * w);
        const float a = t / (std::sin(w) + t * std::cos(w));
        v.fcoef = clampf(1.0f - a, 0.0001f, 1.0f);
    }

    // VCA-Pegel (ISR @0xCCE): Grundpegel + Velocity, 0..1
    float level = P.ampLevel;
    if (P.velDest & kVelAmp) level += v.velMod;
    level = clampf(level, 0.0f, 1.0f);
    if (P.pressDest & kPressVolume) level = clampf(level + press, 0.0f, 1.0f);
    // [ANN] Unisono: 8 Stimmen auf einer Note, Pegel halbiert fuer den Headroom
    v.level = P.unison > 0 ? 0.5f * level : level;

    // Mix-Verschiebung (ISR @0xABA): Keyboard, Velocity, LFOs in voller Skala
    float mx = 0.0f, my = 0.0f;
    if (P.keyDest & kKeyMixAC) mx += v.keyMod;
    if (P.keyDest & kKeyMixBD) my += v.keyMod;
    if (P.velDest & kVelMixAC) mx += v.velMod;
    if (P.velDest & kVelMixBD) my += v.velMod;
    for (int k = 0; k < 2; ++k) {
        if (P.lfo[k].dest & kLfoMixAC) mx += L[k].out;
        if (P.lfo[k].dest & kLfoMixBD) my += L[k].out;
    }
    if (P.pressDest & kPressMixAC) mx += press * 0.25f;
    if (P.pressDest & kPressMixBD) my += press * 0.25f;
    v.modX = mx;
    v.modY = my;

    // Pan (ISR @0xD86): Stimmen-Pan + Keyboard*2 + Velocity + LFOs + Env*2;
    // volle Skala = halbe Panoramabreite.
    float pan = p_.pan[v.index & (kMaxVoices - 1)];
    if (P.keyDest & kKeyPan) pan += 2.0f * v.keyMod;
    if (P.velDest & kVelPan) pan += v.velMod;
    if (P.fenvDest & kFenvPan) pan += 2.0f * fenvMod;
    if (P.pressDest & kPressPan) pan += press;
    for (int k = 0; k < 2; ++k)
        if (P.lfo[k].dest & kLfoPan) pan += L[k].out;
    // CEM3379: die beiden Pan-VCAs sind komplementaer und linear, in der
    // Mitte je halbe Verstaerkung
    const float pos = clampf(0.5f + 0.5f * pan, 0.0f, 1.0f);
    v.panL = 1.0f - pos;
    v.panR = pos;

    float* cur[10] = {&v.fcoef, &v.level, &v.panL, &v.panR, &v.modX, &v.modY,
                      &v.inc[0], &v.inc[1], &v.inc[2], &v.inc[3]};
    for (int k = 0; k < 10; ++k) {
        if (snap) { v.ramp[k] = 0.0f; continue; }
        v.ramp[k] = (*cur[k] - before[k]) * (1.0f / float(kControlSamples));
        *cur[k] = before[k];
    }
}

// --- Noten ---------------------------------------------------------------------

void Engine::countActive() {
    active_ = 0;
    for (int i = 0; i < kMaxVoices; ++i) active_ += voices_[i].on ? 1 : 0;
}

int Engine::pickVoice(int lo, int hi) {
    for (int i = lo; i < hi; ++i)
        if (!voices_[i].on) return i;
    int best = -1;                           // aelteste ausklingende, sonst aelteste
    uint64_t oldest = ~0ull;
    for (int i = lo; i < hi; ++i)
        if (voices_[i].amp.stage >= kEnvRelease && voices_[i].age < oldest) {
            oldest = voices_[i].age; best = i;
        }
    if (best < 0)
        for (int i = lo; i < hi; ++i)
            if (voices_[i].age < oldest) { oldest = voices_[i].age; best = i; }
    return best;
}

void Engine::startVoice(int index, int note, int velocity, float detune, int layer) {
    const Params& P = lp(layer);
    Voice& v = voices_[index];
    // Glide (@0x5AB6): die Stimme gleitet von ihrer zuletzt gespielten
    // (momentanen) Tonhoehe zur neuen
    const float target = float(note * 100) + detune;
    const float from = lastCents_[index] + (v.on ? v.glide : 0.0f);
    const bool glides = P.glide > 0 && played_[index];
    v = Voice{};
    v.on = true;
    v.held = true;
    v.index = index;
    v.layer = layer;
    v.note = note;
    v.detune = detune;
    v.glide = glides ? from - target : 0.0f;
    lastCents_[index] = target;
    played_[index] = true;
    v.age = ++clock_;
    // Velocity (@0x5AE4): Kurve @0x6690 mal Betrag; Keyboard (@0x5B0C):
    // (Note-66) * Betrag / 64 der vollen Skala
    const int vel = velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity);
    v.velMod = float(kVelocityCurve[vel]) / 32767.0f * amt99(P.velAmount);
    v.keyMod = float(note - 66) * amt99(P.keyAmount) / 64.0f;
    envStart(v.amp, P.ampEnv);
    envStart(v.filt, P.filterEnv);
    envStart(v.mx, P.mixEnvX);
    envStart(v.my, P.mixEnvY);
    voiceControl(v, true);
}

// Stimmenbereich einer Schicht (@0x120A): normal alle 8, bei Split/Double
// Hauptschicht 1-4 und verknuepftes Programm 5-8.
void Engine::layerRange(int layer, int& lo, int& hi) const {
    if (p_.keyMode == kKeyNormal) { lo = 0; hi = kMaxVoices; }
    else if (layer == 0) { lo = 0; hi = kMaxVoices / 2; }
    else { lo = kMaxVoices / 2; hi = kMaxVoices; }
}

// Unisono (@0x1284, @0x404E): alle Stimmen der Schicht spielen die tiefste
// gehaltene Taste, Stimme k um k * Detune Cent hoeher; bei Split/Double mit
// vier Stimmen ist der Schritt doppelt so gross (@0x4078).
void Engine::unisonUpdate(int layer, int velocity) {
    int lo, hi;
    layerRange(layer, lo, hi);
    int lowest = -1;
    for (int i = 0; i < heldCount_[layer]; ++i)
        if (lowest < 0 || heldNotes_[layer][i] < lowest) lowest = heldNotes_[layer][i];
    if (lowest == unisonNote_[layer]) return;
    unisonNote_[layer] = lowest;
    if (lowest < 0) {
        for (int i = lo; i < hi; ++i) {
            Voice& v = voices_[i];
            if (!v.on || !v.held) continue;
            if (hold_) v.pending = true; else startRelease(v);
        }
        return;
    }
    const int step = lp(layer).unison * (p_.keyMode == kKeyNormal ? 1 : 2);
    const float base = (layer == 1 && p_.keyMode == kKeyDouble) ? float(p_.doubleDetune) : 0.0f;
    for (int i = lo; i < hi; ++i)
        startVoice(i, lowest, velocity, base + float((i - lo) * step), layer);
}

void Engine::layerNoteOn(int layer, int note, int velocity) {
    if (lp(layer).unison > 0) {
        if (heldCount_[layer] < 16) heldNotes_[layer][heldCount_[layer]++] = note;
        unisonUpdate(layer, velocity);
        return;
    }
    int lo, hi;
    layerRange(layer, lo, hi);
    // Double: das verknuepfte Programm klingt um doubleDetune Cent hoeher (@0x4064)
    const float detune = (layer == 1 && p_.keyMode == kKeyDouble) ? float(p_.doubleDetune) : 0.0f;
    // Mono-Voicing des Arpeggiators (@0x5FC6): eine Stimme je Schicht, die
    // bei jeder Note neu gestartet wird
    const int voice = (arpFromArp_ && !p_.arpPoly) ? lo : pickVoice(lo, hi);
    startVoice(voice, note, velocity, detune, layer);
}

void Engine::layerNoteOff(int layer, int note) {
    if (lp(layer).unison > 0) {
        int& n = heldCount_[layer];
        for (int i = 0; i < n; ++i)
            if (heldNotes_[layer][i] == note) { heldNotes_[layer][i] = heldNotes_[layer][--n]; break; }
        unisonUpdate(layer, 100);
        return;
    }
    int lo, hi;
    layerRange(layer, lo, hi);
    for (int i = lo; i < hi; ++i) {
        Voice& v = voices_[i];
        if (!v.on || !v.held || v.note != note || v.layer != layer) continue;
        if (hold_) v.pending = true; else startRelease(v);
    }
}

void Engine::queueDelayed(int note, int velocity, bool on) {
    if (delayedCount_ >= 32) return;
    delayed_[delayedCount_++] = {int(p_.doubleDelay * sr_), note, velocity, on};
}

void Engine::keyOn(int note, int velocity) {
    if (paramsDirty_) applyParams();
    if (p_.keyMode == kKeySplit) {
        // @0x126E: ab dem Splitpunkt die Hauptschicht, darunter das verknuepfte Programm
        layerNoteOn(note >= p_.splitPoint ? 0 : 1, note, velocity);
    } else if (p_.keyMode == kKeyDouble) {
        layerNoteOn(0, note, velocity);
        if (p_.doubleDelay > 0.0f) {
            // @0x5458: verzoegerte Note mit um 3 verringerter Velocity
            const int v2 = velocity > 3 ? velocity - 3 : 1;
            queueDelayed(note, v2, true);
        } else {
            layerNoteOn(1, note, velocity);
        }
    } else {
        layerNoteOn(0, note, velocity);
    }
    countActive();
}

void Engine::startRelease(Voice& v) {
    v.held = false;
    v.pending = false;
    const Params& P = lp(v.layer);
    envRelease(v.amp, P.ampEnv);
    envRelease(v.filt, P.filterEnv);
    envRelease(v.mx, P.mixEnvX);
    envRelease(v.my, P.mixEnvY);
}

void Engine::keyOff(int note) {
    if (p_.keyMode == kKeyNormal) { layerNoteOff(0, note); return; }
    layerNoteOff(0, note);
    if (p_.keyMode == kKeyDouble && p_.doubleDelay > 0.0f) queueDelayed(note, 0, false);
    else layerNoteOff(1, note);
}

// --- Arpeggiator ---------------------------------------------------------------
// Ablauf wie in der Firmware (@0x5FEC ff.): Der interne Takt hat die Periode
// (106 - Rate) Interrupts zu 1 ms (@0x5F26). Je Clocks/Step ein Schritt, die
// Note endet nach der Haelfte des Schritts (@0x6020).

void Engine::setArp(bool on) {
    if (on == arpOn_) return;
    if (!on) arpGateOff();
    arpOn_ = on;
    arpCount_ = arpHeld_ = 0;
    arpRestart();
}

void Engine::arpRestart() {
    arpPos_ = -1;
    arpOct_ = 0;
    arpDir_ = 1;
    arpRepeatLeft_ = 0;
    arpClock_ = 0;
    arpSamples_ = 0.0f;
}

void Engine::arpGateOff() {
    if (!arpSounding_) return;
    keyOff(arpNote_);
    arpSounding_ = false;
}

void Engine::arpClock() {
    if (--arpClock_ <= 0) {
        arpClock_ = arpClocksPerStep_;
        arpStep();
    } else if (arpClock_ == arpClocksPerStep_ / 2) {
        arpGateOff();
    }
}

void Engine::arpStep() {
    arpGateOff();
    const int n = arpCount_;
    if (n <= 0) return;
    const int octs = p_.arpOctaves < 0 ? 0 : (p_.arpOctaves > 3 ? 3 : p_.arpOctaves);

    if (arpRepeatLeft_ > 0 && arpPos_ >= 0 && arpPos_ < n) {
        --arpRepeatLeft_;                    // dieselbe Note noch einmal
    } else {
        arpRepeatLeft_ = p_.arpRepeats < 0 ? 0 : (p_.arpRepeats > 3 ? 3 : p_.arpRepeats);
        const bool first = arpPos_ < 0 || arpPos_ >= n;
        switch (p_.arpScan) {
            case kArpDown:
                if (first) { arpPos_ = n - 1; arpOct_ = octs; }
                else if (--arpPos_ < 0) { arpPos_ = n - 1; if (--arpOct_ < 0) arpOct_ = octs; }
                break;
            case kArpUpDown:
                // am Rand umkehren, ohne die Randnote zu wiederholen (@0x61C0)
                if (first) { arpPos_ = 0; arpOct_ = 0; arpDir_ = 1; break; }
                if (n == 1 && octs == 0) break;
                if (arpDir_ > 0) {
                    if (arpPos_ + 1 < n) ++arpPos_;
                    else if (arpOct_ < octs) { arpPos_ = 0; ++arpOct_; }
                    else { arpDir_ = -1; if (--arpPos_ < 0) { arpPos_ = n - 1; --arpOct_; } }
                } else {
                    if (arpPos_ > 0) --arpPos_;
                    else if (arpOct_ > 0) { arpPos_ = n - 1; --arpOct_; }
                    else { arpDir_ = 1; if (++arpPos_ >= n) { arpPos_ = 0; ++arpOct_; } }
                }
                break;
            case kArpRandom:
                rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                arpPos_ = int((rng_ >> 8) % uint32_t(n));
                arpOct_ = int((rng_ >> 20) % uint32_t(octs + 1));
                break;
            case kArpReverse:
                if (first) { arpPos_ = n - 1; arpOct_ = 0; }
                else if (--arpPos_ < 0) { arpPos_ = n - 1; if (++arpOct_ > octs) arpOct_ = 0; }
                break;
            default:   // Up, Assign
                if (first) { arpPos_ = 0; arpOct_ = 0; }
                else if (++arpPos_ >= n) { arpPos_ = 0; if (++arpOct_ > octs) arpOct_ = 0; }
                break;
        }
    }
    if (arpOct_ > octs) arpOct_ = octs;

    // Up/Down/UpDown laufen nach Tonhoehe, Assign/Reverse/Random in der
    // Reihenfolge des Anschlags
    int index = arpPos_;
    if (p_.arpScan <= kArpUpDown) {
        int order[kArpMaxKeys];
        for (int i = 0; i < n; ++i) order[i] = i;
        for (int i = 1; i < n; ++i) {
            const int k = order[i];
            int j = i - 1;
            while (j >= 0 && arpKeys_[order[j]].note > arpKeys_[k].note) { order[j + 1] = order[j]; --j; }
            order[j + 1] = k;
        }
        index = order[arpPos_];
    }
    // Rests: das oberste C der VS-Tastatur steht fuer eine Pause (Handbuch Kap. 5)
    if (p_.arpRests && arpKeys_[index].note == 96) return;
    // Tonhoehe wie @0x62BC: Taste + 12 * Oktave + Transpose, nie negativ
    arpNote_ = arpKeys_[index].note + 12 * arpOct_ + arpTranspose_;
    while (arpNote_ < 0) arpNote_ += 12;
    arpVel_ = p_.arpVelocity ? arpKeys_[index].velocity : 80;
    if (arpNote_ > 127) return;
    arpFromArp_ = true;
    keyOn(arpNote_, arpVel_);
    arpFromArp_ = false;
    arpSounding_ = true;
}

// Arpeggiator-Split (nur bei Split-Programmen): 1 = nur rechts vom
// Splitpunkt, 2 = nur links arpeggieren; die andere Seite spielt normal.
bool Engine::arpTakes(int note) const {
    if (!arpOn_) return false;
    if (p_.keyMode != kKeySplit) return true;
    if (p_.arpSplit == 1) return note >= p_.splitPoint;
    if (p_.arpSplit == 2) return note < p_.splitPoint;
    return true;
}

void Engine::setArpLatch(bool on) {
    arpLatch_ = on;
    if (!on && arpHeld_ == 0 && p_.arpMode == kArpNormal) {
        arpGateOff();
        arpCount_ = 0;
        arpPos_ = -1;
    }
}

// Chord-Modus des Pro VS Mini (FUN_2400a694): Taste + Tabelle[Satz][Tonklasse]
#if PROVS_HAS_PVM
static const unsigned char* chordOf(int set, int note) {
    if (set < 1 || set > kChordSetCount) return nullptr;
    return kChordSets[set - 1][((note % 12) + 12) % 12];
}
#else
static const unsigned char* chordOf(int, int) { return nullptr; }
#endif

void Engine::noteOn(int note, int velocity) {
    if (paramsDirty_) applyParams();
    const unsigned char* c = chordOf(p_.chordSet, note);
    if (!c) { inputOn(note, velocity); return; }
    for (int k = 0; k < 3; ++k)
        if (k == 0 || c[k] != c[k - 1]) inputOn(note + c[k], velocity);
}

void Engine::noteOff(int note) {
    const unsigned char* c = chordOf(p_.chordSet, note);
    if (!c) { inputOff(note); return; }
    for (int k = 0; k < 3; ++k)
        if (k == 0 || c[k] != c[k - 1]) inputOff(note + c[k]);
}

void Engine::inputOn(int note, int velocity) {
    if (note < 0 || note > 127) return;
    if (!arpTakes(note)) { keyOn(note, velocity); return; }
    if (paramsDirty_) applyParams();
    const bool wasEmpty = arpCount_ == 0;
    // Auto-Latch: eine neue Taste nach dem Loslassen aller beginnt neu
    if (p_.arpMode == kArpAutoLatch && arpHeld_ == 0) { arpCount_ = 0; arpPos_ = -1; }
    bool present = false;
    if (p_.arpMode != kArpExtend)
        for (int i = 0; i < arpCount_; ++i) present |= arpKeys_[i].note == note;
    if (!present && arpCount_ < kArpMaxKeys) arpKeys_[arpCount_++] = {note, velocity};
    ++arpHeld_;
    if (wasEmpty && arpCount_ > 0) arpRestart();     // erster Schritt sofort
}

void Engine::inputOff(int note) {
    if (note < 0 || note > 127) return;
    if (!arpTakes(note)) { keyOff(note); return; }
    if (arpHeld_ > 0) --arpHeld_;
    if (p_.arpMode != kArpNormal || arpLatch_) return;   // gemerkte Tasten bleiben
    for (int i = 0; i < arpCount_; ++i) {
        if (arpKeys_[i].note != note) continue;
        for (int k = i + 1; k < arpCount_; ++k) arpKeys_[k - 1] = arpKeys_[k];
        --arpCount_;
        if (arpPos_ >= i && arpPos_ >= 0) --arpPos_;
        break;
    }
    if (arpCount_ == 0) { arpGateOff(); arpPos_ = -1; }
}

void Engine::releaseHeld() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices_[i].on && voices_[i].pending) startRelease(voices_[i]);
}

void Engine::allNotesOff() {
    for (int i = 0; i < kMaxVoices; ++i) voices_[i] = Voice{};
    heldCount_[0] = heldCount_[1] = 0;
    unisonNote_[0] = unisonNote_[1] = -1;
    delayedCount_ = 0;
    arpCount_ = arpHeld_ = 0;
    arpSounding_ = false;
    arpPos_ = -1;
    active_ = 0;
}

void Engine::pitchBend(float amount) { bend_ = clampf(amount, -1.0f, 1.0f); }

void Engine::setModWheel(float amount) { wheel_ = clampf(amount, 0.0f, 1.0f); }

// 7-Bit-Wert der Firmware: (v<<8 | v) >> 1, also 127 -> 32639 von 32767
void Engine::setPressure(float amount) {
    pressure_ = clampf(amount, 0.0f, 1.0f) * (32639.0f / 32767.0f);
}

// --- Audio ---------------------------------------------------------------------

PROVS_HOT void Engine::render(float* out, int frames) {
    if (paramsDirty_) {
        applyParams();
        ctl_ = 0;                            // sofort neu rechnen
    }
    // Chorus (ISR @0xDD2): Depth = Grundwert + Pressure + Mod-Wheel,
    // Rate = Grundwert + Pressure/4 (in Phasenschritten der LFO-Tabelle)
    {
        const float press = pressure_ * amt99(p_.pressAmount);
        float mod = 0.0f;
        if (p_.wheelDest & kWheelChorus) mod += wheel_;
        if (p_.pressDest & kPressChorusDepth) mod += press;
        const float rateMod = (p_.pressDest & kPressChorusRate)
            ? press * 8192.0f / (2.0f * 65536.0f * kFwTickSeconds) : 0.0f;
        const float hz = p_.fxRateHz > 0.0f ? p_.fxRateHz + rateMod : 0.0f;
        fx_.setType(p_.fxType);
        fx_.setRate(p_.fxRate);
        fx_.setRateHz(hz > 0.0f ? hz : 0.0f);
        fx_.setDepth(clampf(p_.fxDepth + mod, 0.0f, 1.0f));
        // Original-Chorus: rechts das gewaehlte Programm, links bei
        // Split/Double das verknuepfte, sonst ebenfalls das gewaehlte
        // (Handbuch Kap. 12)
        const Params& pl = p_.keyMode != kKeyNormal ? p2_ : p_;
        const float hzR = p_.fxRateHz > 0.0f ? p_.fxRateHz + rateMod : 0.05f + p_.fxRate * 1.15f;
        const float hzL = pl.fxRateHz > 0.0f ? pl.fxRateHz + rateMod : 0.05f + pl.fxRate * 1.15f;
        // Latch U320: D8 = rechter, D9 = linker Chorus (Schaltplan SD2400-3)
        fx_.setVsChorus(0, (pl.chorusOn & 2) != 0, hzL, clampf(pl.fxDepth + mod, 0.0f, 1.0f));
        fx_.setVsChorus(1, (p_.chorusOn & 1) != 0, hzR, clampf(p_.fxDepth + mod, 0.0f, 1.0f));
    }

    const float vcaFall = 1.0f - std::exp(-1.0f / (0.0024f * sr_));
    const float fwrap = float(kWaveFrames);

    for (int n = 0; n < frames; ++n) {
        if (ctl_ <= 0) {
            ctl_ = kControlSamples;
            // Arpeggiator-Takt
            if (arpOn_ && !arpExternal_ && arpCount_ > 0 && p_.arpRate > 0) {
                arpSamples_ -= float(kControlSamples);
                while (arpSamples_ <= 0.0f) {
                    const int rate = p_.arpRate > 99 ? 99 : p_.arpRate;
                    arpSamples_ += float(106 - rate) * kFwInterruptSeconds * sr_;
                    arpClock();
                }
            }
            lfoAdvance(kControlSamples, 0);
            if (p_.keyMode != kKeyNormal) lfoAdvance(kControlSamples, 1);
            // verzoegerte Noten der zweiten Schicht (Double mit Delay)
            for (int d = 0; d < delayedCount_;) {
                Delayed& e = delayed_[d];
                e.samples -= kControlSamples;
                if (e.samples > 0) { ++d; continue; }
                const Delayed ev = e;
                for (int k = d + 1; k < delayedCount_; ++k) delayed_[k - 1] = delayed_[k];
                --delayedCount_;
                if (ev.on) layerNoteOn(1, ev.note, ev.velocity);
                else layerNoteOff(1, ev.note);
            }
            for (int vi = 0; vi < kMaxVoices; ++vi) {
                Voice& v = voices_[vi];
                if (!v.on) continue;
                // Glide: mit festem Tempo (Cent je Tick) auf die Zieltonhoehe zu
                const int g = lp(v.layer).glide;
                const float gstep = g > 0
                    ? float(kGlideRate[g > 99 ? 99 : g]) / 64.0f
                      * float(kControlSamples) / (kFwTickSeconds * sr_)
                    : 0.0f;
                if (v.glide > gstep) v.glide -= gstep;
                else if (v.glide < -gstep) v.glide += gstep;
                else v.glide = 0.0f;
                voiceControl(v);
            }
        }
        --ctl_;

        float mixL = 0.0f, mixR = 0.0f;
        for (int vi = 0; vi < kMaxVoices; ++vi) {
            Voice& v = voices_[vi];
            if (!v.on) continue;

            const Params& P = lp(v.layer);
            envTick(v.amp, P.ampEnv);
            envTick(v.filt, P.filterEnv);
            envTick(v.mx, P.mixEnvX);
            envTick(v.my, P.mixEnvY);
            if (v.amp.stage == kEnvDone) { v.on = false; continue; }

            // Vektor-Mix wie die Firmware (@0xB4E): u = (x+y+1)/2, v = (y-x+1)/2,
            // A = (1-u)v, B = (1-u)(1-v), C = u(1-v), D = uv. In der Mitte je 25 %.
            const float x = clampf(v.mx.v * P.mixAmount + p_.mixX + v.modX, -1.0f, 1.0f);
            const float y = clampf(v.my.v * P.mixAmount + p_.mixY + v.modY, -1.0f, 1.0f);
            const float u = clampf(0.5f * (x + y) + 0.5f, 0.0f, 1.0f);
            const float w = clampf(0.5f * (y - x) + 0.5f, 0.0f, 1.0f);
            const float gains[4] = {
                (1.0f - u) * w,             // A
                (1.0f - u) * (1.0f - w),    // B
                u * (1.0f - w),             // C
                u * w};                     // D

            float sum = 0.0f;
            for (int i = 0; i < 4; ++i) {
                float s;
                if (v.tab[i]) {
                    const int i0 = int(v.phase[i]) & (kWaveFrames - 1);
                    const int i1 = (i0 + 1) & (kWaveFrames - 1);
                    const float fr = v.phase[i] - float(int(v.phase[i]));
                    const float a = float(v.tab[i][i0]);
                    s = (a + (float(v.tab[i][i1]) - a) * fr) * v.tabScale[i];
                } else {
                    s = v.noise[i];
                }
                sum += s * gains[i] * P.osc[i].level;
                const int before = int(v.phase[i]);
                v.phase[i] += v.inc[i];
                if (!v.tab[i] && int(v.phase[i]) != before) {
                    // Rauschen: neuer Zufallswert je Wellen-Sample
                    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                    v.noise[i] = float(int32_t(rng_)) * (1.0f / 2147483648.0f);
                }
                while (v.phase[i] >= fwrap) v.phase[i] -= fwrap;
            }

            // 4-Pol-Tiefpass, Gegenkopplung ueber alle Pole (Resonanz), weich begrenzt
            const float f = v.fcoef;
            // Handbuch Kap. 7: Selbstoszillation ab etwa Resonanz 70. Die
            // Kaskade schwingt bei Schleifenverstaerkung 4.
            const float reso = clampf(P.resonance * (4.0f * 99.0f / 70.0f), 0.0f, 5.0f);
            float fb = clampf(reso * v.f3, -1.2f, 1.2f);
            fb = fb * (1.0f - fb * fb * 0.15f);
            // Datenblatt: bei voller Resonanz faellt der Durchlasspegel nur um 6 dB
            float in = sum * (1.0f + 0.375f * reso) - fb;
            in = clampf(in, -1.5f, 1.5f);
            in = in * (1.0f - in * in * 0.05f);
            v.f0 += f * (in - v.f0);
            v.f1 += f * (v.f0 - v.f1);
            v.f2 += f * (v.f1 - v.f2);
            v.f3 += f * (v.f2 - v.f3);

            const float dc = v.f3 - v.dcX + 0.998f * v.dcY;
            v.dcX = v.f3;
            v.dcY = dc;
            // 0,45: Platz fuer mehrere Stimmen plus Chorus-Anteil unter der Summenbegrenzung
            // VCA des CEM3379: linear in der Steuerspannung, darunter (4 % der
            // Skala, -20 dB) ein weicher Auslauf gegen Null
            // Steuerspannung des VCA (Schaltplan): steigt ueber Diode und
            // 390 Ohm praktisch sofort, faellt ueber 24 kOhm an 0,1 uF (2,4 ms)
            float cvA = v.amp.v * v.level;
            if (cvA < v.vcaCv) { v.vcaCv += (cvA - v.vcaCv) * vcaFall; cvA = v.vcaCv; }
            else v.vcaCv = cvA;
            float vca;
            if (cvA >= 0.04f) vca = 0.1f + (cvA - 0.04f) * (0.9f / 0.96f);
            else { const float t = cvA * 25.0f; vca = 0.1f * t * t * t; }
            const float o = dc * vca * 0.45f * p_.gain;
            mixL += o * v.panL;
            mixR += o * v.panR;
            v.fcoef += v.ramp[0]; v.level += v.ramp[1];
            v.panL += v.ramp[2];  v.panR += v.ramp[3];
            v.modX += v.ramp[4];  v.modY += v.ramp[5];
            v.inc[0] += v.ramp[6]; v.inc[1] += v.ramp[7];
            v.inc[2] += v.ramp[8]; v.inc[3] += v.ramp[9];
        }
        float l = mixL * 2.0f, r = mixR * 2.0f;      // Pan Mitte = 0 dB
        fx_.process(l, r);
        // weiche Summenbegrenzung hinter den Effekten (kubisch, erreicht
        // +-1 bei +-1,5)
        l = clampf(l, -1.5f, 1.5f);
        r = clampf(r, -1.5f, 1.5f);
        l -= l * l * l * (1.0f / 6.75f);
        r -= r * r * r * (1.0f / 6.75f);
        out[2 * n] = l;
        out[2 * n + 1] = r;
    }
    countActive();
}

}  // namespace provs

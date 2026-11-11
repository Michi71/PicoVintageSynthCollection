// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/fx.h — Effekte der provs-Engine
//
//  CHORUS    Stereo-Chorus: zwei modulierte Verzoegerungen aus der Monosumme,
//            um 90 Grad versetzt moduliert, je eine nach links und rechts,
//            gleich laut wie das trockene Signal.
//  ENSEMBLE  String-Ensemble nach dem klassischen Prinzip: drei
//            Verzoegerungen, jede von einer langsamen und einer schnellen
//            Dreiphasen-Schwingung moduliert.
//  VS-CHORUS der Chorus des Original-Prophet-VS, nach Schaltplan (s. u.).
//
//  Chorus und Ensemble sind eigene Entwuerfe dieser Engine. Sie stehen fuer
//  die gleichnamigen Effekte des Pro VS Mini, bilden dessen Firmware aber
//  nicht nach; Verzoegerungen, Raten und Mischung sind hier nach Gehoer und
//  ueblicher Praxis gewaehlt.
//
//  Portabel (keine OS-Abhaengigkeiten), float32.
// ============================================================================
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace provs {

class Fx {
public:
    enum Type : uint8_t { kOff = 0, kChorus = 1, kEnsemble = 2, kVsChorus = 3 };

    void init(double sampleRate) {
        sr_ = float(sampleRate);
        clear();
    }

    void clear() {
        std::memset(buf_, 0, sizeof buf_);
        pos_ = 0;
        wet_ = 0.0f;
        for (Osc& o : osc_) { o.s = 0.0f; o.c = 1.0f; }

        std::memset(vsBuf_, 0, sizeof vsBuf_);
        vsPos_ = 0;
        for (int c = 0; c < 2; ++c) {
            vsPhase_[c] = 0.0f; vsDir_[c] = 1.0f; vsCv_[c] = 0.5f;
            for (int k = 0; k < 2; ++k) vsZ_[c][k][0] = vsZ_[c][k][1] = 0.0f;
        }
        // Butterworth-Tiefpass 2. Ordnung bei 7 kHz, Glaettung der Chorus-CV
        const float w = 6.2831853f * 7000.0f / sr_;
        const float alpha = std::sin(w) / 1.4142136f, cw = std::cos(w);
        const float a0 = 1.0f + alpha;
        vsB0_ = 0.5f * (1.0f - cw) / a0;
        vsB1_ = (1.0f - cw) / a0;
        vsA1_ = -2.0f * cw / a0;
        vsA2_ = (1.0f - alpha) / a0;
        vsCvCoef_ = 1.0f - std::exp(-1.0f / (0.0087f * sr_));
    }

    void setType(Type t) { type_ = t; }
    Type type() const { return type_; }

    // Rate 0..1: Chorus 0,1 .. 6 Hz (exponentiell); beim Ensemble verschiebt
    // sie beide Schwingungen leicht.
    void setRate(float r) { rate_ = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r); }
    // > 0: Chorus-Frequenz direkt in Hz, sonst gilt setRate
    void setRateHz(float hz) { rateHz_ = hz; }
    // Depth 0..1: Anteil des Effekts und Hub der Modulation
    void setDepth(float d) { depth_ = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }

    // Chorus des Original-VS je Kanal (0 = links, 1 = rechts):
    // an/aus, LFO-Frequenz in Hz, Tiefe 0..1. Ausgeschaltet bleibt der
    // Kanal trocken.
    void setVsChorus(int ch, bool on, float hz, float depth) {
        vsOn_[ch & 1] = on;
        vsHz_[ch & 1] = hz;
        vsDepth_[ch & 1] = depth < 0.0f ? 0.0f : (depth > 1.0f ? 1.0f : depth);
    }

    void process(float& l, float& r) {
        if (type_ == kVsChorus) {
            vsChorus(l, 0);
            vsChorus(r, 1);
            vsPos_ = (vsPos_ + 1) & (kVsLen - 1);
            return;
        }
        if (type_ == kOff) return;
        wet_ += (depth_ - wet_) * 0.002f;          // weiches Ein- und Ausblenden
        buf_[pos_] = 0.5f * (l + r);               // Eingang: Monosumme
        if (type_ == kChorus) chorus(l, r);
        else ensemble(l, r);
        pos_ = (pos_ + 1) & (kLen - 1);
    }

private:
    // Sinus-/Cosinus-Paar ohne Tabellen: gekoppelte Form, je Sample zwei
    // Multiplikationen. w = 2 pi f / fs.
    struct Osc {
        float s = 0.0f, c = 1.0f;
        void step(float w) { s += w * c; c -= w * s; }
    };

    // Lesen mit linearer Interpolation, delay in Samples hinter der Schreibstelle
    float read(float delay) const {
        float p = float(pos_) - delay;
        if (p < 0.0f) p += float(kLen);
        const int i0 = int(p) & (kLen - 1);
        const int i1 = (i0 + 1) & (kLen - 1);
        const float f = p - float(int(p));
        return buf_[i0] + (buf_[i1] - buf_[i0]) * f;
    }

    // Stereo-Chorus. Grundverzoegerung 7 ms, Hub 0,8 .. 4 ms je nach Depth.
    // Links und rechts lesen dieselbe Leitung mit um 90 Grad versetzter
    // Modulation; das ergibt Breite, ohne dass die Monosumme ausloescht.
    // Ab Depth 0,6 ist das verzoegerte Signal so laut wie das trockene; die
    // Summe wird so abgesenkt, dass der Klang nicht lauter wird.
    void chorus(float& l, float& r) {
        const float hz = rateHz_ > 0.0f ? rateHz_ : 0.1f * std::exp(rate_ * 4.0943f);
        osc_[0].step(6.2831853f * hz / sr_);
        const float base = 0.007f * sr_;
        const float hub = (0.0008f + 0.0032f * wet_) * sr_;
        const float wl = read(base + hub * osc_[0].s);
        const float wr = read(base + hub * osc_[0].c);
        const float g = wet_ > 0.6f ? 1.0f : wet_ * (1.0f / 0.6f);
        const float norm = 1.0f / (1.0f + 0.35f * g);
        l = (l + g * wl) * norm;
        r = (r + g * wr) * norm;
    }

    // String-Ensemble: drei Verzoegerungen um 5 ms. Jede wird von einer
    // langsamen (um 0,6 Hz, Hub 1,5 ms) und einer schnellen Schwingung
    // (um 6 Hz, Hub 0,25 ms) moduliert, die drei Leitungen liegen je 120 Grad
    // auseinander. Leitung 1 geht nach links, 3 nach rechts, 2 in die Mitte.
    // Ab Depth 0,6 voller Effektanteil.
    void ensemble(float& l, float& r) {
        osc_[1].step(6.2831853f * (0.45f + 0.5f * rate_) / sr_);
        osc_[2].step(6.2831853f * (5.0f + 2.5f * rate_) / sr_);
        const float base = 0.005f * sr_, slow = 0.0015f * sr_, fast = 0.00025f * sr_;
        float tap[3];
        for (int k = 0; k < 3; ++k) {
            // sin(x + k*120 Grad) aus dem Sinus-/Cosinus-Paar
            static const float ca[3] = {1.0f, -0.5f, -0.5f};
            static const float sa[3] = {0.0f, 0.8660254f, -0.8660254f};
            const float ls = osc_[1].s * ca[k] + osc_[1].c * sa[k];
            const float lf = osc_[2].s * ca[k] + osc_[2].c * sa[k];
            tap[k] = read(base + slow * ls + fast * lf);
        }
        const float g = wet_ > 0.6f ? 1.0f : wet_ * (1.0f / 0.6f);
        const float norm = 1.0f / (1.0f + 0.6f * g);
        l = (l + g * (tap[0] + 0.7f * tap[1])) * norm;
        r = (r + g * (tap[2] + 0.7f * tap[1])) * norm;
    }

    // Chorus des Original-VS (Schaltplan SD2400-9), je Kanal eine Schaltung:
    //   Kompressor NE571 -> 7-kHz-Filter -> Eimerkette MN3209 (256 Stufen)
    //   -> 7-kHz-Filter -> Expander NE571 -> Schalter -> zum trockenen Signal.
    // Takt: Die Steuerspannung des Chorus-LFO (DAC 0..5,2 V, Dreieck * Depth
    // um die Mitte, ISR @0xDD2) wird um 1,325 verstaerkt und versetzt
    // ("1-8 Volt") und liegt ueber 10 kOhm (mit 1 uF geglaettet) am
    // Steuereingang eines 7555 an +8 V. Dessen Kondensator (220 pF) laedt
    // ueber 44,2 kOhm von Vc/2 auf Vc:
    //   T = 9,72 us * ln((8 - Vc/2) / (8 - Vc)) + 1,4 us
    // Das ergibt die im Plan notierten 2,5 .. 25 us (400 .. 40 kHz). Ein 4013
    // halbiert den Takt, die Verzoegerung ist 256 * T = 0,64 .. 6 ms.
    // Mischung am Eingang des Ausgangs-VCA: trocken 15 Vss ueber 62 kOhm,
    // Chorus 6 Vss ueber 47 kOhm -> Chorus-Anteil 0,53.
    // Nicht nachgebildet: die Dynamik des Kompanders (im Ganzen linear).
    static float vsClockPeriod(float cv) {            // cv = DAC-Anteil 0..1
        const float vop = 1.0f + 6.89f * cv;          // Verstaerker, getrimmt auf 1..8 V
        const float vc = 0.8696f * vop + 0.695f;      // 10k gegen den internen Teiler (2 x 100k)
        return 9.724e-6f * std::log((8.0f - 0.5f * vc) / (8.0f - vc)) + 1.38e-6f;
    }

    void vsChorus(float& x, int c) {
        vsBuf_[c][vsPos_] = x;
        if (!vsOn_[c]) return;
        // Dreieck-LFO wie die Firmware: Phase -1..+1, am Rand umkehren
        vsPhase_[c] += vsDir_[c] * 4.0f * vsHz_[c] / sr_;
        if (vsPhase_[c] > 1.0f) { vsPhase_[c] = 2.0f - vsPhase_[c]; vsDir_[c] = -1.0f; }
        else if (vsPhase_[c] < -1.0f) { vsPhase_[c] = -2.0f - vsPhase_[c]; vsDir_[c] = 1.0f; }
        // Glaettung der Steuerspannung: 10k || 66,7k mit 1 uF = 8,7 ms
        const float cv = 0.5f + 0.5f * vsPhase_[c] * vsDepth_[c];
        vsCv_[c] += (cv - vsCv_[c]) * vsCvCoef_;
        const float delay = 256.0f * vsClockPeriod(vsCv_[c]) * sr_;    // Samples
        float pos = float(vsPos_) - delay;
        if (pos < 0.0f) pos += float(kVsLen);
        const int i0 = int(pos) & (kVsLen - 1);
        const int i1 = (i0 + 1) & (kVsLen - 1);
        const float f = pos - float(int(pos));
        float wet = vsBuf_[c][i0] * (1.0f - f) + vsBuf_[c][i1] * f;
        // zwei Tiefpaesse 2. Ordnung bei 7 kHz (vor und nach der Eimerkette)
        for (int k = 0; k < 2; ++k) {
            float* z = vsZ_[c][k];
            const float y = vsB0_ * wet + z[0];
            z[0] = vsB1_ * wet - vsA1_ * y + z[1];
            z[1] = vsB0_ * wet - vsA2_ * y;
            wet = y;
        }
        x += 0.53f * wet;
    }

    float sr_ = 44100.0f;
    Type type_ = kOff;
    float rate_ = 0.4f;
    float rateHz_ = 0.0f;
    float depth_ = 0.5f;
    float wet_ = 0.0f;

    // gemeinsame Leitung fuer Chorus und Ensemble: 4096 Samples reichen fuer
    // 12 ms bis 192 kHz
    static constexpr int kLen = 4096;
    float buf_[kLen];
    int pos_ = 0;
    Osc osc_[3];                              // Chorus, Ensemble langsam, schnell

    static constexpr int kVsLen = 1024;       // 6,4 ms passen bis 96 kHz
    float vsBuf_[2][kVsLen];
    int vsPos_ = 0;
    bool vsOn_[2] = {false, false};
    float vsHz_[2] = {1.0f, 1.0f}, vsDepth_[2] = {0.0f, 0.0f};
    float vsPhase_[2] = {0.0f, 0.0f}, vsDir_[2] = {1.0f, 1.0f};
    float vsCv_[2] = {0.5f, 0.5f};
    float vsZ_[2][2][2] = {};
    float vsB0_ = 0.0f, vsB1_ = 0.0f, vsA1_ = 0.0f, vsA2_ = 0.0f, vsCvCoef_ = 1.0f;
};

}  // namespace provs

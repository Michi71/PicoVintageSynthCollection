// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/fx.h — effects of the provs engine
//
//  CHORUS    Stereo chorus: two modulated delays fed from the mono sum,
//            modulated 90 degrees apart, one to the left and one to the
//            right, each as loud as the dry signal.
//  ENSEMBLE  String ensemble following the classic principle: three
//            delays, each modulated by a slow and a fast three-phase
//            oscillation.
//  VS-CHORUS the chorus of the original Prophet VS, per the schematic (see
//            below).
//
//  Chorus and ensemble are this engine's own designs. They stand in for the
//  effects of the same name in the Pro VS Mini but do not replicate its
//  firmware; delays, rates and mix are chosen here by ear and common
//  practice.
//
//  Portable (no OS dependencies), float32.
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
        // 2nd-order Butterworth low-pass at 7 kHz, smoothing of the chorus CV
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

    // Rate 0..1: chorus 0.1 .. 6 Hz (exponential); for the ensemble it
    // shifts both oscillations slightly.
    void setRate(float r) { rate_ = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r); }
    // > 0: chorus frequency directly in Hz, otherwise setRate applies
    void setRateHz(float hz) { rateHz_ = hz; }
    // Depth 0..1: effect amount and modulation excursion
    void setDepth(float d) { depth_ = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }

    // Chorus of the original VS per channel (0 = left, 1 = right):
    // on/off, LFO frequency in Hz, depth 0..1. When switched off, the
    // channel stays dry.
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
        wet_ += (depth_ - wet_) * 0.002f;          // smooth fade-in and fade-out
        buf_[pos_] = 0.5f * (l + r);               // input: mono sum
        if (type_ == kChorus) chorus(l, r);
        else ensemble(l, r);
        pos_ = (pos_ + 1) & (kLen - 1);
    }

private:
    // Sine/cosine pair without tables: coupled form, two multiplications
    // per sample. w = 2 pi f / fs.
    struct Osc {
        float s = 0.0f, c = 1.0f;
        void step(float w) { s += w * c; c -= w * s; }
    };

    // Read with linear interpolation, delay in samples behind the write position
    float read(float delay) const {
        float p = float(pos_) - delay;
        if (p < 0.0f) p += float(kLen);
        const int i0 = int(p) & (kLen - 1);
        const int i1 = (i0 + 1) & (kLen - 1);
        const float f = p - float(int(p));
        return buf_[i0] + (buf_[i1] - buf_[i0]) * f;
    }

    // Stereo chorus. Base delay 7 ms, excursion 0.8 .. 4 ms depending on
    // Depth. Left and right read the same delay line with the modulation
    // 90 degrees apart; that gives width without the mono sum cancelling.
    // From Depth 0.6 upward the delayed signal is as loud as the dry one;
    // the sum is attenuated so that the sound does not get louder.
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

    // String ensemble: three delays around 5 ms. Each is modulated by a
    // slow (around 0.6 Hz, excursion 1.5 ms) and a fast oscillation
    // (around 6 Hz, excursion 0.25 ms); the three lines are 120 degrees
    // apart. Line 1 goes to the left, 3 to the right, 2 to the centre.
    // From Depth 0.6 upward full effect amount.
    void ensemble(float& l, float& r) {
        osc_[1].step(6.2831853f * (0.45f + 0.5f * rate_) / sr_);
        osc_[2].step(6.2831853f * (5.0f + 2.5f * rate_) / sr_);
        const float base = 0.005f * sr_, slow = 0.0015f * sr_, fast = 0.00025f * sr_;
        float tap[3];
        for (int k = 0; k < 3; ++k) {
            // sin(x + k*120 degrees) from the sine/cosine pair
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

    // Chorus of the original VS (schematic SD2400-9), one circuit per channel:
    //   compressor NE571 -> 7 kHz filter -> bucket brigade MN3209 (256 stages)
    //   -> 7 kHz filter -> expander NE571 -> switch -> to the dry signal.
    // Clock: the control voltage of the chorus LFO (DAC 0..5.2 V, triangle *
    // Depth around the centre, ISR @0xDD2) is amplified by 1.325 and offset
    // ("1-8 Volt") and is applied through 10 kOhm (smoothed with 1 uF) to
    // the control input of a 7555 running from +8 V. Its capacitor (220 pF)
    // charges through 44.2 kOhm from Vc/2 to Vc:
    //   T = 9.72 us * ln((8 - Vc/2) / (8 - Vc)) + 1.4 us
    // That yields the 2.5 .. 25 us (400 .. 40 kHz) noted in the schematic. A
    // 4013 halves the clock; the delay is 256 * T = 0.64 .. 6 ms.
    // Mix at the input of the output VCA: dry 15 Vpp through 62 kOhm,
    // chorus 6 Vpp through 47 kOhm -> chorus share 0.53.
    // Not modelled: the dynamics of the compander (linear as a whole).
    static float vsClockPeriod(float cv) {            // cv = DAC fraction 0..1
        const float vop = 1.0f + 6.89f * cv;          // amplifier, trimmed to 1..8 V
        const float vc = 0.8696f * vop + 0.695f;      // 10k against the internal divider (2 x 100k)
        return 9.724e-6f * std::log((8.0f - 0.5f * vc) / (8.0f - vc)) + 1.38e-6f;
    }

    void vsChorus(float& x, int c) {
        vsBuf_[c][vsPos_] = x;
        if (!vsOn_[c]) return;
        // Triangle LFO like the firmware: phase -1..+1, reverse at the edge
        vsPhase_[c] += vsDir_[c] * 4.0f * vsHz_[c] / sr_;
        if (vsPhase_[c] > 1.0f) { vsPhase_[c] = 2.0f - vsPhase_[c]; vsDir_[c] = -1.0f; }
        else if (vsPhase_[c] < -1.0f) { vsPhase_[c] = -2.0f - vsPhase_[c]; vsDir_[c] = 1.0f; }
        // Smoothing of the control voltage: 10k || 66.7k with 1 uF = 8.7 ms
        const float cv = 0.5f + 0.5f * vsPhase_[c] * vsDepth_[c];
        vsCv_[c] += (cv - vsCv_[c]) * vsCvCoef_;
        const float delay = 256.0f * vsClockPeriod(vsCv_[c]) * sr_;    // Samples
        float pos = float(vsPos_) - delay;
        if (pos < 0.0f) pos += float(kVsLen);
        const int i0 = int(pos) & (kVsLen - 1);
        const int i1 = (i0 + 1) & (kVsLen - 1);
        const float f = pos - float(int(pos));
        float wet = vsBuf_[c][i0] * (1.0f - f) + vsBuf_[c][i1] * f;
        // two 2nd-order low-passes at 7 kHz (before and after the bucket brigade)
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

    // shared delay line for chorus and ensemble: 4096 samples are enough for
    // 12 ms up to 192 kHz
    static constexpr int kLen = 4096;
    float buf_[kLen];
    int pos_ = 0;
    Osc osc_[3];                              // chorus, ensemble slow, fast

    static constexpr int kVsLen = 1024;       // 6.4 ms fit up to 96 kHz
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

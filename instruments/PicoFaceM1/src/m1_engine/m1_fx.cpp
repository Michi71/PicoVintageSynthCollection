// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_fx.cpp -- the 33 M1 effect types and the effect section.
//
// A line-by-line port of the Python reference m1_effects.py (analysis sections
// 8.7-8.19), so a host render can be compared against it. Two deliberate
// differences: the delay lines are int16 in one MDE-sized ring per effect
// (values saturate at +-1 on the way in, as the chip's do), and each unit
// produces its dry path and its pure effect signal in one pass instead of
// running twice.
#include "m1_engine/m1_fx.h"

#include <math.h>
#include <new>
#include <string.h>

namespace m1 {
namespace {

constexpr float kQ9  = 511.0f / 512.0f;          // MDE coefficient saturation
constexpr float kG   = -0.966797f;                // -0x1EF/512, the chip's "0.967"
constexpr uint32_t kHalf = 32768u;                // words per effect
constexpr uint32_t kMask = kHalf - 1u;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float clip1(float x) { return clampf(x, -1.0f, 1.0f); }
inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline int s8(uint8_t v) { return (int8_t)v; }
inline int u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline float q9(float x) { return clampf(roundf(x * 512.0f) / 512.0f, -kQ9, kQ9); }

// ---------------------------------------------------------------------------
// One effect's half of the delay RAM. The pointer runs down; a line is an
// offset range [base, base + len) and reading base + d returns what was
// written at base d samples ago.
// ---------------------------------------------------------------------------
struct Ring {
    int16_t* m = nullptr;
    uint32_t n = 0, top = 0;
    bool overflow = false;

    uint32_t alloc(uint32_t len) {
        const uint32_t b = top;
        top += len;
        if (top > kHalf) overflow = true;   // host test asserts this never happens
        return b;
    }
    float rd(uint32_t o) const { return m[(n + o) & kMask] * (1.0f / 32768.0f); }
    void wr(uint32_t o, float v) {
        int32_t q = (int32_t)lrintf(clip1(v) * 32767.0f);
        m[(n + o) & kMask] = (int16_t)q;
    }
    void step() { n = (n - 1u) & kMask; }
};

// First-order shelf as the firmware patcher computes it (F298:1465/14D7):
// H(z) = s (1 + Z z^-1) / (1 + Pl z^-1), DC (low) or Nyquist (high) gain 0.5
// at 0 dB.
struct Shelf { float Z = 0, P = 0, s = 0.5f; };

Shelf shelfCoeffs(const FxTables& t, int kq13, int gainDb, bool high) {
    const float k = kq13 / 8192.0f;
    const int a = gainDb < 0 ? -gainDb : gainDb;
    const float G = t.eq_gain[a > 12 ? 12 : a] / 32768.0f;
    float an, ad;
    if (high) { an = 1 + k + G * (k - 1); ad = 1 + k - G * (k - 1); }
    else      { an = k - 1 + G * (1 + k); ad = 1 - k + G * (1 + k); }
    const float R = an / ad;
    float Z, P, s;
    if (gainDb > 0) { Z = k; P = R; s = 1.0f / ad; }
    else            { Z = R; P = k; s = ad / 4.0f; }
    Shelf o;
    o.Z = q9(Z); o.P = q9(P); o.s = q9(s - 1.0f) + 1.0f;
    return o;
}

// The pre-EQ in front of delay/modulation effects: two shelves at 1 kHz, DC
// gain 0.25, state per channel.
struct PreEq {
    Shelf c1, c2;
    float x1[2] = {0, 0}, a1[2] = {0, 0}, b1[2] = {0, 0};
    void set(const FxTables& t, int eqHigh, int eqLow) {
        c1 = shelfCoeffs(t, t.eq_k[2], eqLow, false);   // stage 1: low shelf (byte 7)
        c2 = shelfCoeffs(t, t.eq_k[2], eqHigh, true);   // stage 2: high shelf (byte 6)
    }
    float run(int ch, float x) {
        const float a = c1.s * (x + c1.Z * x1[ch]) - c1.P * a1[ch];
        const float y = c2.s * (a + c2.Z * a1[ch]) - c2.P * b1[ch];
        x1[ch] = x; a1[ch] = a; b1[ch] = y;
        return y;
    }
};

// 20-bit LFO phase over a 128-point table, linearly interpolated.
inline float wave(const float* w, uint32_t ph, int off, int woff) {
    const int j = (int)(((ph >> 13) + (uint32_t)off) & 0x7F);
    const float fr = (ph & 0x1FFF) * (1.0f / 8192.0f);
    const float* p = w + woff + j;
    return p[0] + (p[1] - p[0]) * fr;
}

// Quadratic three-point read of a modulated line (chorus/flanger family).
inline float modRead(const Ring& r, uint32_t base, float mod) {
    const int im = (int)mod;                    // truncation toward zero, like int()
    const float f = mod - (float)im;
    const uint32_t pos = base + (uint32_t)im;
    const float p0 = r.rd(pos), p1 = r.rd(pos + 1), p2 = r.rd(pos + 2);
    return p0 + (p1 - p0) * f + (f * f - f) * ((p2 + p0) * 0.5f - p1);
}

}  // namespace

// ---------------------------------------------------------------------------
// Unit interface. tick() returns the pure effect signal (the reference at
// wet = 1) and the dry path the section mixes against it (raw input, or the
// EQ'd input for the types whose EQ sits in the direct path).
// ---------------------------------------------------------------------------
class FxUnit {
public:
    virtual ~FxUnit() {}
    virtual void tick(float l, float r, float& fl, float& fr, float& dl, float& dr) = 0;
    virtual void setFast(bool) {}
    Ring ring;
    const FxTables* t = nullptr;
};

namespace {

// ---------------------------------------------------------------- Reverb 0-5
class Reverb final : public FxUnit {
public:
    Reverb(const FxTables* tb, int type, const uint8_t* p) : v(tb->rev[type]) {
        t = tb;
        const int rt = clampi(p[0], 0, 97), hd = clampi(p[2], 0, 99);
        for (int k = 0; k < 4; ++k) {
            fb[k] = v.fb[k][rt];
            damp[k] = v.damp[k][hd];
            cb[k] = kG * (1.0f - damp[k]);
        }
        const int pd = (int)(p[3] * 31.25f) + 6;
        tAp = pd + v.base_ap; tErc = pd + v.base_erc;
        tErl = pd + v.base_erl; tErr = pd + v.base_err;
        const float lv = p[4] * 0.01f;
        er[0] = lv * v.er_scale[0]; er[1] = lv * v.er_scale[1]; er[2] = lv * v.er_scale[2];
        e1 = shelfCoeffs(*tb, tb->eq_k[2], s8(p[7]), false);
        e2 = shelfCoeffs(*tb, tb->eq_k[2], s8(p[6]), true);
        // Pre-delay line long enough for the longest tap at 200 ms.
        int maxTap = 6256 + v.base_ap;
        const int bs[3] = {v.base_erc, v.base_erl, v.base_err};
        for (int b : bs) if (6256 + b > maxTap) maxTap = 6256 + b;
        d0 = ring.alloc((uint32_t)maxTap + 2);
        for (int k = 0; k < 4; ++k) ap[k] = ring.alloc((uint32_t)v.ap_d[k] + 2);
        for (int k = 0; k < 4; ++k) {
            int m = v.comb_d[k];
            if (v.tap_l[k] > m) m = v.tap_l[k];
            if (v.tap_r[k] > m) m = v.tap_r[k];
            cmb[k] = ring.alloc((uint32_t)m + 2);
        }
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        Ring& R = ring;
        const float m = xl + xr;
        const float vv = e1.s * (m + e1.Z * x1) - e1.P * v1;
        const float w = e2.s * (vv + e2.Z * v1) - e2.P * R.rd(d0 + 1);
        x1 = m; v1 = vv;
        R.wr(d0, w);
        const float r0 = R.rd(ap[0] + v.ap_d[0]), r1 = R.rd(ap[1] + v.ap_d[1]);
        const float r2 = R.rd(ap[2] + v.ap_d[2]), r3 = R.rd(ap[3] + v.ap_d[3]);
        const float a0 = v.g_in * R.rd(d0 + tAp) - v.g[0] * r0;
        float sl = 0, sr = 0;
        for (int k = 0; k < 4; ++k) {
            sl += v.comb_out[2 * k] * R.rd(cmb[k] + v.tap_l[k]);
            sr += v.comb_out[2 * k + 1] * R.rd(cmb[k] + v.tap_r[k]);
        }
        const float a2 = sl - v.g[2] * r2, a3 = sr - v.g[3] * r3;
        const float ap0 = r0 + v.g[0] * a0;
        const float a1 = ap0 - v.g[1] * r1;
        const float ap1 = r1 + v.g[1] * a1;
        R.wr(ap[0], a0); R.wr(ap[1], a1); R.wr(ap[2], a2); R.wr(ap[3], a3);
        const float erc = er[0] * R.rd(d0 + tErc);
        fl = r2 + v.g[2] * a2 + erc + er[1] * R.rd(d0 + tErl);
        fr = r3 + v.g[3] * a3 + erc + er[2] * R.rd(d0 + tErr);
        for (int k = 0; k < 4; ++k) {
            const float cin = clip1(fb[k] * R.rd(cmb[k] + v.comb_d[k]) + ap1);
            R.wr(cmb[k], cb[k] * cin + damp[k] * R.rd(cmb[k] + 1));
        }
        dl = xl; dr = xr;
        R.step();
    }
private:
    const FxRevType& v;
    float fb[4], damp[4], cb[4], er[3];
    int tAp, tErc, tErl, tErr;
    Shelf e1, e2;
    float x1 = 0, v1 = 0;
    uint32_t d0, ap[4], cmb[4];
};

// -------------------------------------------------------- Early Ref 6-8
class EarlyRef final : public FxUnit {
public:
    EarlyRef(const FxTables* tb, int type, const uint8_t* p) : v(tb->er[type - 6]) {
        t = tb;
        const float tm = clampf((float)(p[0] + 10), 1.0f, 80.0f);   // stored as time - 10
        const float pd = clampf((float)p[1], 0.0f, 200.0f);
        maxTap = 0;
        for (int k = 0; k < 25; ++k) {
            int d = (int)((tm * tb->er_T[k] + pd) * 31.25f) + 1;
            if (d > 0x7A1B) d = 0x7A1B;
            taps[k] = d;
            if (d > maxTap) maxTap = d;
        }
        for (int j = 0; j < 4; ++j) apd[j] = (int)(tm * tb->er_A[j]) + 1;
        e1 = shelfCoeffs(*tb, tb->eq_k[2], s8(p[7]), false);
        e2 = shelfCoeffs(*tb, tb->eq_k[2], s8(p[6]), true);
        d0 = ring.alloc(0x7A1B + 2);
        // Allpass lines sized for the longest time, so a parameter range never
        // moves the layout.
        for (int j = 0; j < 4; ++j) ap[j] = ring.alloc((uint32_t)(80.0f * tb->er_A[j]) + 3);
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        Ring& R = ring;
        const float m = xl + xr;
        const float vv = e1.s * (m + e1.Z * x1) - e1.P * v1;
        R.wr(d0, e2.s * (vv + e2.Z * v1) - e2.P * R.rd(d0 + 1));
        x1 = m; v1 = vv;
        float S = 0;
        for (int k = 0; k < 25; ++k) S += v.tg[k] * R.rd(d0 + (uint32_t)taps[k]);
        float r[4];
        for (int j = 0; j < 4; ++j) r[j] = R.rd(ap[j] + (uint32_t)apd[j]);
        const float a0 = S - v.g[0] * r[0], ap0 = r[0] + v.g[0] * a0;
        const float a1 = ap0 - v.g[1] * r[1], ap1 = r[1] + v.g[1] * a1;
        const float a2 = ap1 - v.g[2] * r[2], a3 = ap1 - v.g[3] * r[3];
        R.wr(ap[0], a0); R.wr(ap[1], a1); R.wr(ap[2], a2); R.wr(ap[3], a3);
        fl = r[2] + v.g[2] * a2;
        fr = r[3] + v.g[3] * a3;
        dl = xl; dr = xr;
        R.step();
    }
private:
    const FxErType& v;
    int taps[25], apd[4], maxTap;
    Shelf e1, e2;
    float x1 = 0, v1 = 0;
    uint32_t d0, ap[4];
};

// --------------------------------------------- Stereo / Cross Delay 9-10
class Delay final : public FxUnit {
public:
    Delay(const FxTables* tb, int type, const uint8_t* p) {
        t = tb;
        cross = type == 10;
        tap[0] = (int)(clampi(u16(p + 0), 0, 500) * 31.25f) + 1;
        tap[1] = (int)(clampi(u16(p + 4), 0, 500) * 31.25f) + 1;
        fb = clampi(s8(p[2]), -99, 99) * 0.01f;
        const int hd = clampi(p[3], 0, 99);
        a = tb->dly_a[hd]; b = tb->dly_b[hd];
        eq.set(*tb, s8(p[6]), s8(p[7]));
        line[0] = ring.alloc(15626 + 2);
        line[1] = ring.alloc(15626 + 2);
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        const float x[2] = {xl, xr};
        float o[2], d[2];
        for (int ch = 0; ch < 2; ++ch) {
            const float e = eq.run(ch, x[ch]);
            // Cross Delay reads the other channel's line: ping-pong.
            const uint32_t src = line[cross ? 1 - ch : ch];
            const float tp = ring.rd(src + (uint32_t)tap[ch]);
            const float fin = clip1(fb * tp * b + e);
            ring.wr(line[ch], a * ring.rd(src + 1) + b * fin);   // one-pole in the loop
            o[ch] = 4.0f * tp;
            d[ch] = 4.0f * e;
        }
        fl = o[0]; fr = o[1]; dl = d[0]; dr = d[1];
        ring.step();
    }
private:
    bool cross;
    int tap[2];
    float fb, a, b;
    PreEq eq;
    uint32_t line[2];
};


// ------------------------------------------------- Chorus 11-12, Flanger 13-14
// One class: the flanger is the chorus with feedback and a second LFO term.
class ModDelay final : public FxUnit {
public:
    ModDelay(const FxTables* tb, int type, const uint8_t* p) {
        t = tb;
        flanger = type >= 13;
        cross = type == 14;
        phaseR = (type == 11 || type == 13) ? 64 : 0;
        depth = clampi(p[0], 0, 99) * 2.56f;
        inc = (uint32_t)tb->lfo_inc[clampi(p[1], 0, 216)];
        const int wv = p[2];
        if (flanger) {
            const int v = clampi(wv, 0, 7);
            c = (16 * v - 160) / 512.0f;
            woff = 0;
            if (v == 3) { woff = 0x102; c = -0.4375f; }
            else if (v == 1) { woff = 0x102; c = -0.375f; }
            else if (v == 7) woff = 0x102;
            fb = clampf(s8(p[3]) * 0.00934696f, -kQ9, kQ9);
            base = (int)(clampi(p[4], 0, 50) * 31.25f) + 1;
        } else {
            woff = (wv == 1 || wv == 3) ? 0x102 : 0;
            c = 0; fb = 0;
            base = (int)(clampi(p[4], 0, 200) * 31.25f) + 1;
        }
        eq.set(*tb, s8(p[6]), s8(p[7]));
        // 260 words of headroom below the write point: a negative LFO swing
        // reads there, as the reference's masked index does.
        const uint32_t len = (uint32_t)(flanger ? 1563 : 6251) + 256 + 4;
        for (int ch = 0; ch < 2; ++ch) line[ch] = ring.alloc(len + 260) + 260;
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        ph = (ph + inc) & 0xFFFFF;
        const float x[2] = {xl, xr};
        float o[2], d[2];
        for (int ch = 0; ch < 2; ++ch) {
            const int off = ch ? phaseR : 0;
            float lfo = wave(t->wt, ph, off, woff);
            if (flanger) lfo += c * wave(t->wt2, ph, off, 0);
            const uint32_t src = line[cross ? 1 - ch : ch];
            const float y = modRead(ring, src + (uint32_t)base, lfo * depth);
            const float e = eq.run(ch, x[ch]);
            ring.wr(line[ch], fb * y + e);
            o[ch] = 4.0f * y; d[ch] = 4.0f * e;
        }
        fl = o[0]; fr = o[1]; dl = d[0]; dr = d[1];
        ring.step();
    }
private:
    bool flanger, cross;
    int phaseR, woff, base;
    float depth, c, fb;
    uint32_t inc, ph = 0;
    PreEq eq;
    uint32_t line[2];
};

// ----------------------------------------------------------- Phaser 15-16
class Phaser final : public FxUnit {
public:
    Phaser(const FxTables* tb, int type, const uint8_t* p) {
        t = tb;
        stereo = type == 15;
        d = clampi(p[0], 0, 99) * 0.005f;
        m = clampi(p[4], 0, 99) * 0.005f;
        inc = (uint32_t)tb->lfo_inc[clampi(p[1], 0, 216)];
        woff = (p[2] == 1 || p[2] == 3) ? 0x102 : 0;
        fb = clampf(s8(p[3]) * 0.01f, -kQ9, kQ9) * kG * kG;
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        ph = (ph + inc) & 0xFFFFF;
        const float lfo = wave(t->wt, ph, 0, woff) * d;
        const float ks[2] = {-(lfo + m), -((stereo ? -lfo + d : lfo) + m)};
        const float x[2] = {xl, xr};
        float o[2];
        for (int ch = 0; ch < 2; ++ch) {
            const float k = clampf(ks[ch], -kQ9, kQ9);
            float* s = st[ch];
            const float u = clip1(fb * s[3] + 0.125f * x[ch]);   // in x1/8, out x8
            const float a0 = (s[0] * k + u) * kG;
            const float a1 = ((a0 + s[1]) * k + s[0]) * kG;
            const float a2 = ((a1 + s[2]) * k + s[1]) * kG;
            const float y = a2 * k + s[2];
            s[0] = a0; s[1] = a1; s[2] = a2; s[3] = y;
            o[ch] = 8.0f * y;
        }
        fl = o[0]; fr = o[1]; dl = xl; dr = xr;
    }
private:
    bool stereo;
    float d, m, fb;
    int woff;
    uint32_t inc, ph = 0;
    float st[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
};

// ---------------------------------------------------------- Tremolo 17-18
class Tremolo final : public FxUnit {
public:
    Tremolo(const FxTables* tb, int type, const uint8_t* p) {
        t = tb;
        phaseR = type == 17 ? 64 : 0;
        depth = clampi(p[0], 0, 99) / 100.0f;
        inc = (uint32_t)tb->lfo_inc[clampi(p[1], 0, 216)];
        woff = (p[2] == 1 || p[2] == 3) ? 0x102 : 0;
        shape = clampi(s8(p[3]), -99, 99) * tb->trem_k;
        eq.set(*tb, s8(p[6]), s8(p[7]));
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        ph = (ph + inc) & 0xFFFFF;
        const float x[2] = {xl, xr};
        float o[2], dd[2];
        for (int ch = 0; ch < 2; ++ch) {
            const int off = ch ? phaseR : 0;
            float lfo = wave(t->wt, ph, off, woff) + shape * wave(t->wt2, ph, off, 0);
            lfo = clampf(lfo, 0.0f, 1.0f);
            const float e = eq.run(ch, 0.5f * x[ch]);
            o[ch] = 8.0f * (-kG * e + lfo * depth * kG * e);
            dd[ch] = 8.0f * e;
        }
        fl = o[0]; fr = o[1]; dl = dd[0]; dr = dd[1];
    }
private:
    int phaseR, woff;
    float depth, shape;
    uint32_t inc, ph = 0;
    PreEq eq;
};

// --------------------------------------------------------- Equalizer 19
class Equalizer final : public FxUnit {
public:
    Equalizer(const FxTables* tb, const uint8_t* p) {
        t = tb;
        a = shelfCoeffs(*tb, tb->eq_k[clampi(p[5], 0, 2)], s8(p[7]), false);
        b = shelfCoeffs(*tb, tb->eq_k[2 + clampi(p[4], 0, 2)], s8(p[6]), true);
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        const float x[2] = {xl, xr};
        float o[2];
        for (int ch = 0; ch < 2; ++ch) {
            float* s = st[ch];
            const float u = 0.5f * x[ch];
            const float ya = a.s * (u + a.Z * s[0]) - a.P * s[1];
            const float yb = b.s * (ya + b.Z * s[1]) - b.P * s[2];
            s[0] = u; s[1] = ya; s[2] = yb;
            o[ch] = clip1(8.0f * yb);
        }
        fl = o[0]; fr = o[1]; dl = xl; dr = xr;
    }
private:
    Shelf a, b;
    float st[2][3] = {{0, 0, 0}, {0, 0, 0}};
};

// --------------------------------------------- Over Drive 20, Distortion 21
inline float shaper(float a, int power) {
    const float u = 1.0f - 0.999f * fabsf(a) * 0.999f;
    float up = u * u;                 // u^2
    up *= up;                         // u^4
    if (power == 16) { up *= up; up *= up; }
    return (a >= 0 ? 1.0f : -1.0f) * (0.999f - 0.999f * up);
}

class Drive final : public FxUnit {
public:
    Drive(const FxTables* tb, int type, const uint8_t* p) {
        t = tb;
        od = type == 20;
        const float c = clampf(p[2] * 0.00998047f, -kQ9, kQ9);
        const float g1 = 1.0f + c;
        gain = g1 * g1 * g1 * g1;
        level = clampf(p[3] * 0.01f, -kQ9, kQ9);
        eq.set(*tb, od ? s8(p[6]) : 0, s8(p[7]));
        buf = ring.alloc(512);
        t0 = od ? 200 : 83; t1 = od ? 83 : 200;
        tg = od ? 0.95f : 1.0f;
    }
    // Distortion feeds the balance into the shaper's input, so the section
    // hands it the wet gain first.
    float wetIn = 1.0f;
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        const float m = xl + xr;
        const float a = clip1(m * (od ? 0.999f * gain : wetIn * gain));
        const float y = level * shaper(a, od ? 4 : 16);
        if (od) {
            const float e = eq.run(0, y) * 0.966f;
            ring.wr(buf, e);
            fl = clip1(e + tg * ring.rd(buf + (uint32_t)t0));
            fr = clip1(e + tg * ring.rd(buf + (uint32_t)t1));
        } else {
            // Low shelf only (byte 7).
            const Shelf& c1 = eq.c1;
            float e = c1.s * (y + c1.Z * eq.x1[0]) - c1.P * eq.a1[0];
            eq.x1[0] = y; eq.a1[0] = e;
            e *= 0.5f;
            const float rl = ring.rd(buf + (uint32_t)t0), rr = ring.rd(buf + (uint32_t)t1);
            ring.wr(buf, e);
            fl = clip1((rl + e) * 0.5f);
            fr = clip1((rr + e) * 0.5f);
        }
        dl = xl; dr = xr;
        ring.step();
    }
    bool od;
private:
    float gain, level, tg;
    int t0, t1;
    PreEq eq;
    uint32_t buf;
};

// ------------------------------------------------------------- Exciter 22
class Exciter final : public FxUnit {
public:
    Exciter(const FxTables* tb, const uint8_t* p) {
        t = tb;
        b = clampf(s8(p[0]) * tb->exc_k, -1.0f, kQ9);
        D = clampi(p[1], 0, 9) + 1;
        eq.set(*tb, s8(p[6]), s8(p[7]));
        for (int ch = 0; ch < 2; ++ch) { E[ch] = ring.alloc(12); Dd[ch] = ring.alloc(12); }
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        const float x[2] = {xl, xr};
        float o[2], dd[2];
        for (int ch = 0; ch < 2; ++ch) {
            const float e = eq.run(ch, 0.5f * x[ch]);
            const float eD = ring.rd(E[ch] + (uint32_t)D), dD = ring.rd(Dd[ch] + (uint32_t)D);
            const float d = e - eD;
            ring.wr(E[ch], e); ring.wr(Dd[ch], d);
            o[ch] = 8.0f * (eD + d * b - dD * b);
            dd[ch] = 8.0f * e;
        }
        fl = o[0]; fr = o[1]; dl = dd[0]; dr = dd[1];
        ring.step();
    }
private:
    float b;
    int D;
    PreEq eq;
    uint32_t E[2], Dd[2];
};

// -------------------------------------------------- Symphonic Ensemble 23
class Symphonic final : public FxUnit {
public:
    Symphonic(const FxTables* tb, const uint8_t* p) : v(tb->sym) {
        t = tb;
        depth = clampi(p[0], 0, 99) * 2.56f;
        eq.set(*tb, s8(p[6]), s8(p[7]));
        line = ring.alloc(2048);
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        const float e = eq.run(0, (xl + xr) * 0.35f);
        ph[0] = (ph[0] + (uint32_t)v.inc[0]) & 0xFFFFF;
        ph[1] = (ph[1] + (uint32_t)v.inc[1]) & 0xFFFFF;
        float acc[2] = {0, 0};
        for (int k = 0; k < 3; ++k) {
            const float mod = (wave(t->wt2, ph[1], v.off[k], 0) * v.mix2[k]
                               + wave(t->wt2, ph[0], v.off[k], 0)) * depth;
            const int im = (int)mod;
            const float fr2 = mod - (float)im;
            const uint32_t pl = line + (uint32_t)(v.baseL[k] + im);
            const uint32_t pr = line + (uint32_t)(v.baseR[k] + im);
            const float al = ring.rd(pl), cl = ring.rd(pl + 1);
            const float ar = ring.rd(pr), cr = ring.rd(pr + 1);
            acc[0] += al + (cl - al) * fr2;
            acc[1] += ar + (cr - ar) * fr2;
        }
        ring.wr(line, e);
        fl = 4.0f * acc[0]; fr = 4.0f * acc[1];
        dl = xl; dr = xr;
        ring.step();
    }
private:
    const FxSym& v;
    float depth;
    PreEq eq;
    uint32_t ph[2] = {0, 0};
    uint32_t line;
};

// ------------------------------------------------------ Rotary Speaker 24
class Rotary final : public FxUnit {
public:
    Rotary(const FxTables* tb, const uint8_t* p) : v(tb->rot) {
        t = tb;
        const float nd = clampi(p[0], 0, 99) / 99.0f;
        q50 = nd * v.depth_k[0]; q54 = nd * v.depth_k[1];
        q70 = nd * v.depth_k[2]; q74 = nd * v.depth_k[3];
        sp = clampi(s8(p[2]), -10, 10) + 10;
        setFast(p[7] != 0);
        inc[0] = target[0]; inc[1] = target[1];       // start settled
        BH = ring.alloc(1024); BD = ring.alloc(1024);
        MB0 = ring.alloc(1024); MB1 = ring.alloc(1024);
    }
    void setFast(bool f) override {
        target[0] = f ? v.fast[sp] : v.slow[sp];
        target[1] = f ? 256 : 25;
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl, float& dr) override {
        Ring& R = ring;
        const float m = (xl + xr) * 0.5f;
        float lfo[2];
        for (int r = 0; r < 2; ++r) {
            const int c = cnt[r];
            cnt[r] = c - 1;
            if (c < 2) {
                cnt[r] = v.accel[r];
                if (inc[r] < target[r]) ++inc[r];
                else if (inc[r] > target[r]) --inc[r];
            }
            ph[r] = (ph[r] + (uint32_t)inc[r]) & 0xFFFFF;
            lfo[r] = wave(t->wt, ph[r], 0, 0);
        }
        const float hi = ((v.c20 * x1 + m) * v.c40 + v.c28 * hi1) * kG;
        const float lo = (x1 * v.c24 + m) * v.c44 + v.c2c * lo1;
        // horn
        float md = lfo[0] * q70;
        int im = (int)md;
        float f = md - (float)im;
        uint32_t pos = BH + (uint32_t)(im + v.h_base);
        const float hv = R.rd(pos) + (R.rd(pos + 1) - R.rd(pos)) * f;
        const float hs = (R.rd(BH + (uint32_t)v.h_fix) * kQ9 + hv) * kQ9;
        R.wr(BH + (uint32_t)v.h_w, (hi1 * v.c38 + v.c30 * R.rd(BH + (uint32_t)v.h_fb_tap) + hi) * kG);
        rec = q50 * lfo[0] * rec + hs;
        // drum
        const float lo2 = lo * kQ9;
        md = lfo[1] * q74; im = (int)md; f = md - (float)im;
        pos = BD + (uint32_t)(im + v.d_base);
        const float dv = R.rd(pos) + (R.rd(pos + 1) - R.rd(pos)) * f;
        R.wr(BD + (uint32_t)v.d_w, lo1 + lo2);
        x1 = m; hi1 = hi; lo1 = lo;
        // microphones
        const int16_t* a = v.micL;
        float L = R.rd(MB1 + (uint32_t)a[3]) * v.g130 + R.rd(MB0 + (uint32_t)a[1]) * v.g124
                + (R.rd(MB0 + (uint32_t)a[2]) + R.rd(MB0 + (uint32_t)a[0])) * v.g120
                + R.rd(MB1 + (uint32_t)a[4]) * v.g134;
        a = v.micR;
        float Rr = v.g130 * R.rd(MB1 + (uint32_t)a[4]) + R.rd(MB0 + (uint32_t)a[1]) * v.g120
                 + (R.rd(MB0 + (uint32_t)a[2]) + R.rd(MB0 + (uint32_t)a[0])) * v.g124
                 + v.g134 * R.rd(MB1 + (uint32_t)a[3]);
        R.wr(MB0, rec - q54 * lfo[0] * rec);
        R.wr(MB1, lo2 + dv);
        fl = clip1(L); fr = clip1(Rr);
        dl = xl; dr = xr;
        R.step();
    }
private:
    const FxRotary& v;
    float q50, q54, q70, q74;
    int sp;
    int target[2], inc[2], cnt[2] = {0, 0};
    uint32_t ph[2] = {0, 0};
    float x1 = 0, hi1 = 0, lo1 = 0, rec = 0;
    uint32_t BH, BD, MB0, MB1;
};

// ---------------------------------------------------- Combinations 25-32
// L input -> delay (bytes 0-3), R input -> second effect (bytes 4-7), mono.
struct MonoDelay {
    uint32_t line = 0;
    int D = 1;
    float fb = 0, a = 0, b = 0;
    void init(Ring& r) { line = r.alloc(15626 + 2); }
    void set(const FxTables& t, int ms, int fbv, int damp) {
        D = (int)(clampi(ms, 0, 500) * 31.25f) + 1;
        fb = clampi(fbv, -99, 99) * 0.01f;      // the firmware value, v x 0.01
        const int hd = clampi(damp, 0, 99);
        a = t.dly_a[hd]; b = t.dly_b[hd];
    }
    float tick(Ring& r, float x) const {        // returns the pure delay tap
        const float tap = r.rd(line + (uint32_t)D);
        const float fin = clip1(fb * tap * b + x);
        r.wr(line, a * r.rd(line + 1) + b * fin);
        return tap;
    }
};

class Combo final : public FxUnit {
public:
    Combo(const FxTables* tb, int type, const uint8_t* p) : ty(type) {
        t = tb;
        dl.init(ring);
        dl.set(*tb, u16(p), s8(p[2]), p[3]);
        switch (type) {
        case 25: case 26: {
            const FxComboRev& v = tb->crev[type - 25];
            cr = &v;
            const int rt = clampi(p[4], 0, type == 25 ? 97 : 47);
            const int hd = clampi(p[6], 0, 99);
            for (int k = 0; k < 4; ++k) {
                fbk[k] = v.fb[k][rt];
                ak[k] = v.damp[k][hd];
                bk[k] = (1.0f - ak[k]) * -0.9667969f;   // the firmware: G (1 - a)
            }
            const int pd = (int)(clampi(p[7], 0, 150) * 31.25f) + 6;
            tAp = pd + v.base_ap; tA = pd + v.base_c; tB = pd + v.base_b;
            int mx = 150 * 31.25f + 6;
            mx += (v.base_ap > v.base_b ? v.base_ap : v.base_b);
            d0 = ring.alloc((uint32_t)mx + 4);
            for (int k = 0; k < 3; ++k) ap[k] = ring.alloc((uint32_t)v.ap_d[k] + 2);
            for (int k = 0; k < 4; ++k) {
                const int m = v.comb_d[k] > v.comb_tap[k] ? v.comb_d[k] : v.comb_tap[k];
                cmb[k] = ring.alloc((uint32_t)m + 2);
            }
            break;
        }
        case 27: {
            const FxComboEr& v = tb->cer;
            ce = &v;
            const float tm = clampf((float)(p[4] + 10), 10.0f, 40.0f);
            const float pd = clampf((float)p[5], 0.0f, 100.0f);
            int mx = 0;
            for (int k = 0; k < 25; ++k) {
                int d = (int)((tm * v.T[k] + pd) * 31.25f) + 1;
                if (d > 0x7A1B) d = 0x7A1B;
                taps[k] = d;
                if (d > mx) mx = d;
            }
            for (int j = 0; j < 4; ++j) apd[j] = (int)(tm * v.A[j]) + 1;
            int mxAll = 0;
            for (int k = 0; k < 25; ++k) {
                const int d = (int)((40.0f * v.T[k] + 100.0f) * 31.25f) + 1;
                if (d > mxAll) mxAll = d;
            }
            d0 = ring.alloc((uint32_t)mxAll + 2);
            for (int j = 0; j < 4; ++j) ap[j] = ring.alloc((uint32_t)(40.0f * v.A[j]) + 3);
            break;
        }
        case 28:
            dr.init(ring);
            dr.set(*tb, u16(p + 4), s8(p[6]), p[7]);
            break;
        case 29: case 30: {
            depth = clampi(p[4], 0, 99) * 2.56f;
            inc = (uint32_t)tb->lfo_inc[clampi(p[5], 0, 216)];
            const int wv = p[6];
            woff = (wv == 1 || wv == 3 || wv == 7) ? 0x102 : 0;
            c = type == 30 ? (16 * wv - 160) / 512.0f : 0.0f;
            // Delay/Chorus: byte 7 has no effect in the firmware -- no feedback,
            // base 937 samples (30 ms).
            fbr = type == 29 ? 0.0f : clampf(s8(p[7]) * 0.00934696f, -kQ9, kQ9);
            base = type == 29 ? 937 : 1;
            d0 = ring.alloc(937 + 256 + 4 + 260) + 260;
            break;
        }
        case 31:
            d = clampi(p[4], 0, 99) * 0.005f;
            inc = (uint32_t)tb->lfo_inc[clampi(p[5], 0, 216)];
            fbr = clampf(s8(p[6]) * 0.01f, -kQ9, kQ9) * kG * kG;
            m = clampi(p[7], 0, 99) * 0.005f;
            break;
        case 32:
            depth = clampi(p[4], 0, 99) / 100.0f;
            inc = (uint32_t)tb->lfo_inc[clampi(p[5], 0, 216)];
            shape = clampi(s8(p[7]), -99, 99) * 0.0028125f;
            break;
        }
    }
    void tick(float xl, float xr, float& fl, float& fr, float& dl_, float& dr_) override {
        Ring& R = ring;
        fl = dl.tick(R, xl);
        float y = 0;
        switch (ty) {
        case 25: case 26: {
            const FxComboRev& v = *cr;
            const float u = xr;                       // wet = 1: the pure effect
            const float vv = (v.eq[0] * x1 + u) * v.eq[4] + v.eq[1] * v1;
            R.wr(d0, (vv + v1 * v.eq[2]) * v.eq[5] + v.eq[3] * R.rd(d0 + 1));
            x1 = u; v1 = vv;
            const float r0 = R.rd(ap[0] + v.ap_d[0]), r1 = R.rd(ap[1] + v.ap_d[1]);
            const float r2 = R.rd(ap[2] + v.ap_d[2]);
            float cs = 0;
            for (int k = 0; k < 4; ++k) cs += v.cout[k] * R.rd(cmb[k] + v.comb_tap[k]);
            const float a0 = v.g_in * R.rd(d0 + (uint32_t)tAp) - v.g[0] * r0;
            const float a1 = cs - v.g[1] * r1;
            const float a2 = (r0 + v.g[0] * a0) - v.g[2] * r2;
            R.wr(ap[0], a0); R.wr(ap[1], a1); R.wr(ap[2], a2);
            const float ap2 = a2 * v.g[2] + r2;
            for (int k = 0; k < 4; ++k) {
                const float cin = clampf(fbk[k] * R.rd(cmb[k] + v.comb_d[k]) + ap2, -1.0f, 0.9999981f);
                R.wr(cmb[k], bk[k] * cin + ak[k] * R.rd(cmb[k] + 1));
            }
            y = r1 + v.g[1] * a1 + v.ger[0] * R.rd(d0 + (uint32_t)tA) + v.ger[1] * R.rd(d0 + (uint32_t)tB);
            break;
        }
        case 27: {
            const FxComboEr& v = *ce;
            R.wr(d0, v.g_in * xr);
            float S = 0;
            for (int k = 0; k < 25; ++k) S += v.tg[k] * R.rd(d0 + (uint32_t)taps[k]);
            const float r0 = R.rd(ap[0] + (uint32_t)apd[0]), r1 = R.rd(ap[1] + (uint32_t)apd[1]);
            const float r3 = R.rd(ap[3] + (uint32_t)apd[3]);
            const float a0 = S - v.g[0] * r0;
            const float a1 = (r0 - v.g[1] * r1) + a0 * v.g[0];
            const float a3 = (r1 - v.g[3] * r3) + a1 * v.g[1];
            R.wr(ap[0], a0); R.wr(ap[1], a1); R.wr(ap[3], a3);
            y = r3 + a3 * v.g[3];
            break;
        }
        case 28:
            y = dr.tick(R, xr);
            break;
        case 29: case 30: {
            ph = (ph + inc) & 0xFFFFF;
            const float lfo = wave(t->wt, ph, 0, woff) + c * wave(t->wt2, ph, 0, 0);
            y = modRead(R, d0 + (uint32_t)base, lfo * depth);
            R.wr(d0, fbr * y + xr);
            break;
        }
        case 31: {
            ph = (ph + inc) & 0xFFFFF;
            const float lfo = wave(t->wt, ph, 0, 0) * d;
            const float k = clampf(-(lfo + m), -kQ9, kQ9);
            const float u = clip1(fbr * ps[4] + 0.125f * xr);
            const float a0 = (u + k * ps[0]) * kG;
            const float a1 = ((a0 + ps[1]) * k + ps[0]) * kG;
            const float a2 = ((a1 + ps[2]) * k + ps[1]) * kG;
            const float a3 = ((a2 + ps[3]) * k + ps[2]) * kG;
            const float yy = a3 * k + ps[3];
            ps[0] = a0; ps[1] = a1; ps[2] = a2; ps[3] = a3; ps[4] = yy;
            y = 8.0f * yy;
            break;
        }
        case 32: {
            ph = (ph + inc) & 0xFFFFF;
            float lfo = wave(t->wt, ph, 0, 0) + shape * wave(t->wt2, ph, 0, 0);
            lfo = clampf(lfo, 0.0f, 1.0f);
            y = -xr - kG * xr + lfo * depth * kG * xr;
            break;
        }
        }
        fr = y;
        dl_ = xl; dr_ = xr;
        R.step();
    }
private:
    int ty;
    MonoDelay dl, dr;
    const FxComboRev* cr = nullptr;
    const FxComboEr* ce = nullptr;
    float fbk[4], ak[4], bk[4];
    int tAp = 0, tA = 0, tB = 0, taps[25], apd[4];
    uint32_t d0 = 0, ap[4] = {0, 0, 0, 0}, cmb[4] = {0, 0, 0, 0};
    float x1 = 0, v1 = 0;
    float depth = 0, c = 0, fbr = 0, d = 0, m = 0, shape = 0;
    int woff = 0, base = 1;
    uint32_t inc = 0, ph = 0;
    float ps[5] = {0, 0, 0, 0, 0};
};

static_assert(sizeof(Reverb) <= 1024 && sizeof(Combo) <= 1024 && sizeof(EarlyRef) <= 1024 &&
              sizeof(Rotary) <= 1024 && sizeof(ModDelay) <= 1024, "FxUnit storage too small");

FxUnit* makeUnit(void* where, const FxTables* t, int type, const uint8_t* p) {
    if (type <= 5)  return new (where) Reverb(t, type, p);
    if (type <= 8)  return new (where) EarlyRef(t, type, p);
    if (type <= 10) return new (where) Delay(t, type, p);
    if (type <= 14) return new (where) ModDelay(t, type, p);
    if (type <= 16) return new (where) Phaser(t, type, p);
    if (type <= 18) return new (where) Tremolo(t, type, p);
    if (type == 19) return new (where) Equalizer(t, p);
    if (type <= 21) return new (where) Drive(t, type, p);
    if (type == 22) return new (where) Exciter(t, p);
    if (type == 23) return new (where) Symphonic(t, p);
    if (type == 24) return new (where) Rotary(t, p);
    if (type <= 32) return new (where) Combo(t, type, p);
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------
void EffectSection::init(const FxTables* tables, const uint16_t* panTable101, int16_t* mem) {
    t_ = tables;
    pan_ = panTable101;
    mem_ = mem;
    static const uint8_t none[kFxBlockLen] = {kFxNoEffect, kFxNoEffect};
    configure(none);
}

void EffectSection::configure(const uint8_t* b) {
    for (int k = 0; k < 2; ++k) {
        if (fx_[k]) { fx_[k]->~FxUnit(); fx_[k] = nullptr; }
    }
    memset(mem_, 0, 2 * kHalf * sizeof(int16_t));
    serial_ = (b[8] & 0x10) != 0;
    on_[0] = (b[8] & 0x03) != 0;
    on_[1] = (b[8] & 0x0C) != 0;
    for (int k = 0; k < 2; ++k) {
        type_[k] = b[k] < kFxTypes ? b[k] : kFxNoEffect;
        fx_[k] = makeUnit(store_[k], t_, type_[k], b + 9 + 8 * k);
        if (fx_[k]) {
            // The unit allocated its lines before it had memory; hand it its half.
            fx_[k]->ring.m = mem_ + k * kHalf;
        }
        for (int h = 0; h < 2; ++h) {
            int v = on_[k] ? b[2 + 2 * k + h] : 0;    // switched off = balance 0, all dry
            v = clampi(v, 0, 100);
            wet_[2 * k + h] = pan_[v] / 512.0f;
            dry_[2 * k + h] = pan_[100 - v] / 512.0f;
        }
    }
    // Out-3/4 panpot: 0 = off, 1..101 = right .. left (analysis 8.17/8.19).
    for (int k = 0; k < 2; ++k) {
        const int v = clampi(b[6 + k], 0, 101);
        float* g = k ? p4_ : p3_;
        if (v == 0) { g[0] = g[1] = 0.0f; }
        else { g[0] = pan_[v - 1] / 512.0f; g[1] = pan_[101 - v] / 512.0f; }
    }
}

bool EffectSection::overflow() const {
    for (int k = 0; k < 2; ++k)
        if (fx_[k] && fx_[k]->ring.overflow) return true;
    return false;
}

void EffectSection::setRotaryFast(bool fast) {
    for (int k = 0; k < 2; ++k)
        if (fx_[k] && type_[k] == 24) fx_[k]->setFast(fast);
}

void EffectSection::runUnit(int k, float l, float r, float& ol, float& orr) {
    FxUnit* u = fx_[k];
    if (!u) { ol = l; orr = r; return; }           // No Effect: everything off, EQ too
    float fl, fr, dl, dr;
    const float wl = wet_[2 * k], wr = wet_[2 * k + 1];
    if (type_[k] == 21) {
        // Distortion: the balance scales the shaper input (left half's value,
        // as the reference passes one wet to both).
        static_cast<Drive*>(u)->wetIn = wl;
        u->tick(l, r, fl, fr, dl, dr);
        ol = dry_[2 * k] * l + fl;
        orr = dry_[2 * k + 1] * r + fr;
        return;
    }
    u->tick(l, r, fl, fr, dl, dr);
    ol = dry_[2 * k] * dl + wl * fl;
    orr = dry_[2 * k + 1] * dr + wr * fr;
}

void EffectSection::process(const float* a, const float* b, const float* c, const float* d,
                            float* out1, float* out2, int n) {
    for (int i = 0; i < n; ++i) {
        float l, r;
        runUnit(0, a[i], b[i], l, r);
        if (serial_) {
            // Serial: C/D join effect 2's input through the Out-3/4 panpots
            // (manual p. 36); C/D themselves go dry to Out 3/4.
            l += p3_[0] * c[i] + p4_[0] * d[i];
            r += p3_[1] * c[i] + p4_[1] * d[i];
            runUnit(1, l, r, out1[i], out2[i]);
        } else {
            float o3, o4;
            runUnit(1, c[i], d[i], o3, o4);
            out1[i] = l + p3_[0] * o3 + p4_[0] * o4;
            out2[i] = r + p3_[1] * o3 + p4_[1] * o4;
        }
    }
}

}  // namespace m1

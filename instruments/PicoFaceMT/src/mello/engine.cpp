// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// See mello/engine.h. Signal path:
//
//   voices (looped tapes, common capstan speed) -> track selector gains
//     -> high pass (head/coupling) -> tape saturation -> tone low pass -> volume
//
// Every constant in this file is this project's own choice, set by ear and by
// what a tape transport plausibly does; none is taken from another product.
#include "mello/engine.h"
#include <cmath>

// On the Pico the per-sample code runs from RAM: executing it from flash puts
// it behind the XIP cache, which the sample data is already competing for.
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define MELLO_HOT(f) __not_in_flash_func(f)
#else
#define MELLO_HOT(f) f
#endif

namespace mello {

namespace {

constexpr int   kChunk      = 32;                    // control-rate block
constexpr float kPi         = 3.14159265f;
constexpr float kPcmScale   = 1.0f / 32768.0f;
constexpr float kVoiceGain  = 0.30f;                 // headroom for chords
constexpr float kFrac       = 1.0f / 4294967296.0f;  // 2^-32

constexpr float kTuneRange  = 2.0f;                  // semitones, kPitch
constexpr float kTapeSecs   = 8.0f;                  // length of a real tape
constexpr float kTapeEndMs  = 40.0f;                 // run-out fade
constexpr float kRetrigMs   = 8.0f;                  // fade of a re-struck key

constexpr float kWowHz        = 0.55f;               // capstan/flywheel drift
constexpr float kWowNoiseHz   = 1.2f;
constexpr float kWowMax       = 0.006f;              // +-0.6 % speed, ~10 cents
constexpr float kFlutterHz    = 8.7f;
constexpr float kFlutterNoiseHz = 30.0f;
constexpr float kFlutterMax   = 0.0025f;             // +-0.25 %

constexpr float kDragMax    = 0.035f;                // -3.5 % at key-down
constexpr float kDragMs     = 45.0f;

constexpr float kHighPassHz = 38.0f;
constexpr float kToneMinHz  = 900.0f;
constexpr float kToneMaxHz  = 9500.0f;
constexpr float kDriveMaxDb = 24.0f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Sine of a phase given in turns (0..1), parabolic approximation with one
// correction step; error below 0.1 %, plenty for an LFO.
inline float sinTurns(float t) {
    float x = t - 0.5f;                          // -0.5..0.5
    float y = 16.0f * x * (std::fabs(x) - 0.5f); // -sin(2 pi x) shape
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return y;
}

// Cubic soft clipper: linear near zero, reaches +-1 at |x| = 1.5 with zero slope.
inline float softClip(float x) {
    x = clampf(x, -1.5f, 1.5f);
    return x - (4.0f / 27.0f) * x * x * x;
}

}  // namespace

uint32_t Engine::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void Engine::init(float sampleRate) {
    sr_ = sampleRate;
    for (Voice& v : voice_) v.stage = kOff;
    pedal_ = false;
    bend_ = 0.0f;
    wowPhase_ = flutterPhase_ = wowNoise_ = flutterNoise_ = 0.0f;
    hp_ = {};
    lp_ = {};

    param_[kVolume]    = 0.8f;
    param_[kTone]      = 0.75f;
    param_[kPitch]     = 0.5f;
    param_[kTrack]     = 0.0f;
    param_[kAttack]    = 0.0f;
    param_[kRelease]   = 0.12f;
    param_[kVelocity]  = 0.0f;
    param_[kWow]       = 0.25f;
    param_[kFlutter]   = 0.25f;
    param_[kDrive]     = 0.2f;
    param_[kKeyDrag]   = 0.3f;
    param_[kTapeLimit] = 0.0f;

    // The classic M400 frame: flute, three violins, cello.
    static const char* const kDefaults[kNumTracks] = {"MKII Flute", "MKII 3 Violins", "M400 Cello"};
    for (int t = 0; t < kNumTracks; ++t) {
        int pick = t < kBankSize ? t : 0;
        for (int i = 0; i < kBankSize; ++i) {
            const char* a = kBank[i].name;
            const char* b = kDefaults[t];
            while (*a && *a == *b) { ++a; ++b; }
            if (*a == 0 && *b == 0) pick = i;
        }
        setTrackInstrument(t, pick);
    }

    bendDirty_ = filterDirty_ = true;
    updateDerived();
    for (int t = 0; t < kNumTracks; ++t) trackGain_[t] = trackTarget_[t];
    volume_ = volumeTarget_;
}

void Engine::setTrackInstrument(int track, int bankIndex) {
    if (track < 0 || track >= kNumTracks || bankIndex < 0 || bankIndex >= kBankSize) return;
    trackInst_[track] = bankIndex;
    const Instrument& in = kBank[bankIndex];
    for (int n = 0; n < 128; ++n) {
        uint8_t z = 0;
        for (int i = 0; i < in.numZones; ++i)
            if (n >= in.zones[i].keyLo && n <= in.zones[i].keyHi) { z = uint8_t(i + 1); break; }
        zoneOf_[track][n] = z;
    }
}

void Engine::setParam(Param p, float v) {
    if (p >= kNumParams) return;
    param_[p] = clampf(v, 0.0f, 1.0f);
    if (p == kPitch) bendDirty_ = true;
    if (p == kTone) filterDirty_ = true;
    updateDerived();
}

// Everything that follows from the parameters but is too dear to work out per
// sample. Called on parameter changes, not from the audio loop.
void Engine::updateDerived() {
    // Track selector: equal-power blend between neighbouring tracks.
    float x = param_[kTrack] * float(kNumTracks - 1);
    for (int t = 0; t < kNumTracks; ++t) {
        float d = std::fabs(x - float(t));
        trackTarget_[t] = d >= 1.0f ? 0.0f : std::cos(d * 0.5f * kPi);
    }

    volumeTarget_ = param_[kVolume] * param_[kVolume] * 1.6f;

    float attackS = 0.001f * std::pow(500.0f, param_[kAttack]);
    attackInc_ = 1.0f / (attackS * sr_);
    float releaseS = 0.005f * std::pow(600.0f, param_[kRelease]);
    relCoef_ = std::exp(-6.9f / (releaseS * sr_));          // -60 dB in releaseS

    wowDepth_     = kWowMax * param_[kWow] * param_[kWow];
    flutterDepth_ = kFlutterMax * param_[kFlutter] * param_[kFlutter];

    dragDepth_ = kDragMax * param_[kKeyDrag];
    dragCoef_  = std::exp(-float(kChunk) / (kDragMs * 0.001f * sr_));
    tapeFrames_ = param_[kTapeLimit] >= 0.5f ? uint32_t(kTapeSecs * sr_) : 0u;

    // Drive pushes the signal into the clipper and takes most of it back out.
    float driveDb = kDriveMaxDb * param_[kDrive];
    preGain_  = std::pow(10.0f, driveDb / 20.0f);
    postGain_ = std::pow(10.0f, -0.7f * driveDb / 20.0f);

    if (bendDirty_) {
        float semis = (param_[kPitch] - 0.5f) * 2.0f * kTuneRange + bend_ * bendRange_;
        pitchRatio_ = std::pow(2.0f, semis / 12.0f);
        bendDirty_ = false;
    }

    if (filterDirty_) {
        // RBJ cookbook biquads, Q = 0.707.
        const float q = 0.7071f;
        {
            float w = 2.0f * kPi * kHighPassHz / sr_;
            float cs = std::cos(w), al = std::sin(w) / (2.0f * q), a0 = 1.0f + al;
            hp_.b0 = (1.0f + cs) * 0.5f / a0;
            hp_.b1 = -(1.0f + cs) / a0;
            hp_.b2 = hp_.b0;
            hp_.a1 = -2.0f * cs / a0;
            hp_.a2 = (1.0f - al) / a0;
        }
        {
            float hz = kToneMinHz * std::pow(kToneMaxHz / kToneMinHz, param_[kTone]);
            if (hz > 0.45f * sr_) hz = 0.45f * sr_;
            float w = 2.0f * kPi * hz / sr_;
            float cs = std::cos(w), al = std::sin(w) / (2.0f * q), a0 = 1.0f + al;
            lp_.b0 = (1.0f - cs) * 0.5f / a0;
            lp_.b1 = (1.0f - cs) / a0;
            lp_.b2 = lp_.b0;
            lp_.a1 = -2.0f * cs / a0;
            lp_.a2 = (1.0f - al) / a0;
        }
        filterDirty_ = false;
    }
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------
Engine::Voice* Engine::allocVoice() {
    Voice* best = nullptr;
    int bestRank = -1;
    uint32_t bestAge = 0;
    for (Voice& v : voice_) {
        if (v.stage == kOff) return &v;
        // Prefer stealing released voices, then the oldest.
        int rank = v.stage == kReleaseStage ? 2 : (v.keyDown ? 0 : 1);
        uint32_t age = serial_ - v.serial;
        if (rank > bestRank || (rank == bestRank && age > bestAge)) {
            best = &v; bestRank = rank; bestAge = age;
        }
    }
    return best;
}

void Engine::releaseVoice(Voice& v, float coef) {
    if (v.stage == kOff) return;
    v.stage = kReleaseStage;
    v.relCoef = coef;
    v.keyDown = v.held = false;
}

void Engine::startVoice(uint8_t note, float gain, int track) {
    uint8_t zi = zoneOf_[track][note];
    if (!zi) return;
    const Instrument& in = kBank[trackInst_[track]];
    const Zone& z = in.zones[zi - 1];
    Voice* v = allocVoice();
    if (!v) return;

    v->pcm     = in.pcm;
    v->phase   = uint64_t(z.start) << 32;
    bool loop  = z.loopEnd > z.loopStart;
    v->loopEnd = uint64_t(loop ? z.loopEnd : z.end) << 32;
    v->loopLen = loop ? uint64_t(z.loopEnd - z.loopStart) << 32 : 0;
    float semis = float(int(note) - int(z.root)) + float(z.cents) * 0.01f;
    v->ratio   = float(in.sampleRate) / sr_ * std::pow(2.0f, semis / 12.0f);
    v->drag    = dragDepth_;
    v->env     = 0.0f;
    v->gain    = gain;
    v->relCoef = relCoef_;
    v->age     = 0;
    v->serial  = ++serial_;
    v->note    = note;
    v->track   = uint8_t(track);
    v->stage   = kAttackStage;
    v->keyDown = true;
    v->held    = false;
}

void Engine::noteOn(uint8_t note, uint8_t velocity) {
    if (note > 127) return;
    if (velocity == 0) { noteOff(note); return; }
    // A key has one tape: striking it again cuts what it was playing.
    float cut = std::exp(-6.9f / (kRetrigMs * 0.001f * sr_));
    for (Voice& v : voice_)
        if (v.stage != kOff && v.note == note) releaseVoice(v, cut);

    float vel  = float(velocity) / 127.0f;
    float gain = kVoiceGain * (1.0f - param_[kVelocity] + param_[kVelocity] * vel * vel);
    for (int t = 0; t < kNumTracks; ++t)
        if (trackTarget_[t] > 0.001f) startVoice(note, gain, t);
}

void Engine::noteOff(uint8_t note) {
    for (Voice& v : voice_) {
        if (v.stage == kOff || v.note != note || !v.keyDown) continue;
        if (pedal_) { v.keyDown = false; v.held = true; }
        else releaseVoice(v, relCoef_);
    }
}

void Engine::sustainPedal(bool down) {
    pedal_ = down;
    if (down) return;
    for (Voice& v : voice_)
        if (v.stage != kOff && v.held) releaseVoice(v, relCoef_);
}

void Engine::pitchBend(float bend) {
    bend_ = clampf(bend, -1.0f, 1.0f);
    bendDirty_ = true;
    updateDerived();
}

void Engine::allNotesOff() {
    pedal_ = false;
    for (Voice& v : voice_) releaseVoice(v, relCoef_);
}

int Engine::activeVoices() const {
    int n = 0;
    for (const Voice& v : voice_) n += v.stage != kOff;
    return n;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

// Capstan speed for the next n samples: 1 plus wow and flutter. All tapes run
// off the same capstan, so every voice gets the same factor.
float MELLO_HOT(Engine::nextSpeed)(int n) {
    float dt = float(n) / sr_;
    wowPhase_ += kWowHz * dt;
    if (wowPhase_ >= 1.0f) wowPhase_ -= 1.0f;
    flutterPhase_ += kFlutterHz * dt;
    if (flutterPhase_ >= 1.0f) flutterPhase_ -= 1.0f;

    float white = float(int32_t(rnd())) * (1.0f / 2147483648.0f);
    // One-pole low passes at chunk rate; 2*pi*f/rate is close enough to the
    // exact coefficient this far below the rate and costs no exp().
    float chunkRate = sr_ / float(n);
    wowNoise_     += clampf(2.0f * kPi * kWowNoiseHz / chunkRate, 0.0f, 1.0f)
                     * (white * 6.0f - wowNoise_);
    white = float(int32_t(rnd())) * (1.0f / 2147483648.0f);
    flutterNoise_ += clampf(2.0f * kPi * kFlutterNoiseHz / chunkRate, 0.0f, 1.0f)
                     * (white * 1.5f - flutterNoise_);

    float wow     = 0.65f * sinTurns(wowPhase_) + 0.35f * clampf(wowNoise_, -1.0f, 1.0f);
    float flutter = 0.6f * sinTurns(flutterPhase_) + 0.4f * clampf(flutterNoise_, -1.0f, 1.0f);
    return 1.0f + wowDepth_ * wow + flutterDepth_ * flutter;
}

void MELLO_HOT(Engine::renderVoice)(Voice& v, float* mix, int n, float speed, float trackGain) {
    float stepF = v.ratio * pitchRatio_ * speed * (1.0f - v.drag);
    v.drag *= dragCoef_;
    // 8.24 fixed point through float keeps 24 bits of pitch resolution.
    uint64_t step = uint64_t(uint32_t(stepF * 16777216.0f)) << 8;

    const int16_t* pcm = v.pcm;
    uint64_t phase = v.phase;
    float env = v.env;
    const float amp = v.gain * trackGain * kPcmScale;

    for (int i = 0; i < n; ++i) {
        switch (v.stage) {
            case kAttackStage:
                env += attackInc_;
                if (env >= 1.0f) { env = 1.0f; v.stage = kHold; }
                break;
            case kReleaseStage:
                env *= v.relCoef;
                if (env < 1.0e-4f) v.stage = kOff;
                break;
            default: break;
        }
        if (v.stage == kOff) break;

        uint32_t idx = uint32_t(phase >> 32);
        float fr = float(uint32_t(phase)) * kFrac;
        float a = float(pcm[idx]);
        float s = a + (float(pcm[idx + 1]) - a) * fr;
        mix[i] += s * env * amp;

        phase += step;
        if (phase >= v.loopEnd) {
            if (v.loopLen) phase -= v.loopLen;
            else { v.stage = kOff; break; }
        }
    }
    v.phase = phase;
    v.env = env;

    v.age += uint32_t(n);
    if (tapeFrames_ && v.age >= tapeFrames_ && v.stage != kReleaseStage && v.stage != kOff)
        releaseVoice(v, std::exp(-6.9f / (kTapeEndMs * 0.001f * sr_)));
}

void MELLO_HOT(Engine::process)(float* out, int frames) {
    const float gainSlew = clampf(float(kChunk) / (0.01f * sr_), 0.0f, 1.0f);   // ~10 ms
    while (frames > 0) {
        int n = frames < kChunk ? frames : kChunk;
        float mix[kChunk];
        for (int i = 0; i < n; ++i) mix[i] = 0.0f;

        float speed = nextSpeed(n);
        for (int t = 0; t < kNumTracks; ++t)
            trackGain_[t] += gainSlew * (trackTarget_[t] - trackGain_[t]);
        for (Voice& v : voice_)
            if (v.stage != kOff) renderVoice(v, mix, n, speed, trackGain_[v.track]);

        float vol = volume_;
        const float volStep = (volumeTarget_ - volume_) * gainSlew / float(n);
        for (int i = 0; i < n; ++i) {
            float x = hp_.tick(mix[i]);
            x = softClip(x * preGain_) * postGain_;
            x = lp_.tick(x);
            vol += volStep;
            out[i] = x * vol;
        }
        volume_ = vol;
        // Keep the filter state out of the denormal range once the input stops.
        if (std::fabs(hp_.z1) < 1.0e-20f) hp_.z1 = hp_.z2 = 0.0f;
        if (std::fabs(lp_.z1) < 1.0e-20f) lp_.z1 = lp_.z2 = 0.0f;

        out += n;
        frames -= n;
    }
}

void Engine::processStereo16(int16_t* out, int frames) {
    float buf[kChunk];
    while (frames > 0) {
        int n = frames < kChunk ? frames : kChunk;
        process(buf, n);
        for (int i = 0; i < n; ++i) {
            int32_t s = int32_t(clampf(buf[i], -1.0f, 1.0f) * 32767.0f);
            *out++ = int16_t(s);
            *out++ = int16_t(s);
        }
        frames -= n;
    }
}

}  // namespace mello

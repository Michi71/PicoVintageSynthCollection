// RDX_LFO.h
#pragma once
#include "ram_hot.h"
#include <cmath>
#include "RDX_Constants.h"
#include "misc.h"

class RDX_LFO {
public:
    enum class Waveform { SINE = 0, TRIANGLE = 1, SAW_UP = 2, SAW_DOWN = 3, SQUARE = 4, S_HOLD_8 = 5, S_HOLD = 6 };

    inline void init(uint8_t rate = 64, uint8_t delay = 0, Waveform wf = Waveform::SINE) {
        setRate(rate); setDelay(delay); setWaveform(wf);
        phase_ = 0.f; value_ = 0.f; prevValue_ = 0.f; increment_ = 0.f;
        shValue_ = 0.f; shCounter_ = -1; prevPhase_ = 0.f;
        fadeInEnv_ = (delay > 0) ? 0.f : 1.f;
    }

    inline void setDelay(uint8_t delayParam) {
        delay_ = DELAY_TIME_MS[delayParam] * 0.001f;
        delaySamples_ = delay_ * SAMPLE_RATE;
        fadeInSamples_ = delaySamples_ / 3;
        fadeInIncrement_ = (fadeInSamples_ > 0) ? (float)DMA_BUFFER_LEN / (float)fadeInSamples_ : 1.0f;
    }

    inline void setRate(uint8_t rate) {
        if (rate > 127) rate = 127;
        frequency_ = LFO_SPEED[rate];
        phaseInc_ = frequency_ * DMA_BUFFER_LEN * DIV_SAMPLE_RATE;
    }

    inline void setWaveform(Waveform wf) { waveform_ = wf; }

    inline void updateState() {
        if (delaySamples_ > 0) {
            delaySamples_ -= DMA_BUFFER_LEN; fadeInEnv_ += fadeInIncrement_;
            if (fadeInEnv_ >= 1.f) { fadeInEnv_ = 1.f; delaySamples_ = 0; }
        }
        float startPhase = phase_; float endPhase = startPhase + phaseInc_;
        if (endPhase >= 1.f) endPhase -= fast_floorf(endPhase);
        if (waveform_ == Waveform::S_HOLD_8) {
            int step = int(endPhase * 8.0f) & 7;
            if (step != shCounter_) { shCounter_ = step; shValue_ = randomFloat(); }
            value_ = shValue_ * fadeInEnv_; increment_ = 0.f; phase_ = endPhase; prevPhase_ = startPhase;
            return;
        }
        if (waveform_ == Waveform::S_HOLD) {
            if (startPhase < prevPhase_) { shValue_ = randomFloat(); }
            value_ = shValue_ * fadeInEnv_; increment_ = 0.f; phase_ = endPhase; prevPhase_ = startPhase;
            return;
        }
        float v0 = evalWave(startPhase); float v1 = evalWave(endPhase) * fadeInEnv_;
        if ((int)waveform_ > 3) { value_ = v0 * fadeInEnv_; increment_ = 0.f; }
        else { value_ = v0 * fadeInEnv_; increment_ = (v1 - v0) / float(DMA_BUFFER_LEN); }
        prevValue_ = v1; phase_ = endPhase; prevPhase_ = startPhase;
    }

    inline float __attribute__((always_inline)) RAM_HOT(getValue)() const { return value_; }
    inline float __attribute__((always_inline)) RAM_HOT(getIncrement)() const { return increment_; }

private:
    inline float randomFloat() {
        static uint32_t lfsr = 0xA5A5A5A5;
        lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5;
        return ((lfsr & 0xFFFFFF) / float(0x800000) - 1.f);
    }

    inline float evalWave(float ph) const {
        switch (waveform_) {
            case Waveform::SINE: return sin01(ph);
            case Waveform::TRIANGLE: return 1.f - 4.f * fabsf(ph - 0.5f);
            case Waveform::SAW_UP: return 2.f * ph - 1.f;
            case Waveform::SAW_DOWN: return 1.f - 2.f * ph;
            case Waveform::SQUARE: return (ph < 0.5f) ? 1.f : -1.f;
            default: return 0.f;
        }
    }

    float phase_ = 0.f; float frequency_ = 1.f; Waveform waveform_ = Waveform::SINE;
    float delay_ = 0.f; int delaySamples_ = 0; int fadeInSamples_ = 0;
    float fadeInEnv_ = 1.f; float fadeInIncrement_ = 0.f;
    float value_ = 0.f; float prevValue_ = 0.f; float increment_ = 0.f;
    float phaseInc_ = 0.f;
    float shValue_ = 0.f; int   shCounter_ = -1; float prevPhase_ = 0.f;
};

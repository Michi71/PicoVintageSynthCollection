// include/dx_engine/fx_flanger.h
#pragma once
#include "fx_base.h"
#include "misc.h"
#include "RDX_Types.h"
#include "RDX_State.h"
#include "RDX_Constants.h"
#include <cstring>
#include <algorithm>

class FxFlanger : public FXBase {
public:
    void init(float sampleRate, uint8_t slot) override {
        FXBase::init(sampleRate, slot);
        setDepth(0.5f);
        setRate(0.5f);
        writeIndex_ = 0;
        lfoPhase_ = 0.f;
        lfoCur_ = 0.f;
        prepared_ = false;
    }

    inline bool prepare(float* scratch, uint32_t scratchSize, int sampleRate) override {
        sampleRate_ = sampleRate;
        bufferSize_ = std::min<int>(scratchSize / 2, (int)(0.015f * sampleRate) + 4);
        delayL_ = scratch;
        delayR_ = scratch + bufferSize_;
        memset(delayL_, 0, bufferSize_ * 2 * sizeof(float));
        prepared_ = true;
        updateParams();
        return true;
    }

    inline void reset() override {
        if (prepared_) {
            writeIndex_ = 0;
            depth_ = 0.5f;
            rate_ = 0.5f;
            minDelay_ = 0.f;
            maxDelay_ = 0.f;
            lfoPhase_ = 0.f;
            lfoCur_ = 0.f;
            lfoInc_ = 0.f;
            mixDry_ = 0.7f;
            mixWet_ = 0.3f;
            setDepth(64);
            setRate(64);
        }
    }

    inline uint32_t scratchFootprintFloats() const override { return (uint32_t)bufferSize_ * 2; }

    inline void processBlock(float* l, float* r, uint32_t n) override {
        if (!enabled_ || !prepared_) return;

        setDepth(st.effects[slotId_][1] );
        setRate(st.effects[slotId_][2] );
        updateParams();

        for (uint32_t i = 0; i < n; ++i) {
            float phase = lfoPhase_ + (float)i * lfoInc_;
            if (phase >= 1.f) phase -= 1.f;
            float lfo = 0.5f + 0.5f * sin01(phase);
            float dSamp = minDelay_ + (maxDelay_ - minDelay_) * lfo;

            float dL = getDelayed(delayL_, dSamp);
            float dR = getDelayed(delayR_, dSamp * 1.1f);

            float outL = (l[i] + dL) * mixDry_;
            float outR = (r[i] + dR) * mixDry_;

            delayL_[writeIndex_] = l[i] + dL * mixWet_;
            delayR_[writeIndex_] = r[i] + dR * mixWet_;

            l[i] = outL;
            r[i] = outR;

            writeIndex_++;
            if (writeIndex_ >= bufferSize_) writeIndex_ = 0;
        }

        lfoPhase_ += rate_ * (float)n / sampleRate_;
        if (lfoPhase_ >= 1.f) lfoPhase_ -= 1.f;
    }

private:
    RDX_Common& st = RDX_State::getState().workingPatch.common;
    float* delayL_ = nullptr;
    float* delayR_ = nullptr;
    int bufferSize_ = 0;
    int writeIndex_ = 0;

    int depthParam_ = 64;
    int rateParam_  = 64;
    float depth_ = 0.5f;
    float rate_ = 0.5f;
    float minDelay_ = 0.f;
    float maxDelay_ = 0.f;
    float lfoPhase_ = 0.f;
    float lfoCur_ = 0.f;
    float lfoInc_ = 0.f;
    float mixDry_ = 0.7f;
    float mixWet_ = 0.3f;

    inline void setDepth(int d) {
        if (d == depthParam_) return;
        depth_ = fclamp(0.5f * d * MIDI_NORM, 0.f, 1.f);
    }
    inline void setRate(int r)  {
        if (r == rateParam_) return;
        rate_  = LFO_SPEED[r] * 0.2436f ;
    }

    inline void updateParams() {
        lfoInc_ = rate_ / sampleRate_;
        minDelay_ = 0.0015f * sampleRate_;
        maxDelay_ = minDelay_ + depth_ * 0.010f * sampleRate_;
    }

    inline float getDelayed(float* buf, float delaySamp) {
        float readPos = writeIndex_ - delaySamp;
        if (readPos < 0) readPos += bufferSize_;
        int i1 = (int)readPos;
        int i2 = (i1 + 1) % bufferSize_;
        float frac = readPos - i1;
        return buf[i1] * (1.f - frac) + buf[i2] * frac;
    }
};

// include/dx_engine/fx_phaser.h
#pragma once
#include "RDX_Types.h"
#include "RDX_State.h"
#include "RDX_Constants.h"
#include "misc.h"
#include "fx_base.h"
#include <cmath>

class FxPhaser : public FXBase {
public:
    FxPhaser() = default;

    bool prepare(float* scratch, uint32_t scratchLen,
                 int sampleRate = SAMPLE_RATE) override
    {
        sampleRate_ = sampleRate;
        resetPhaser();
        if (scratchLen >= FLANGER_BUF_SIZE * 2) {
            delayL_ = scratch;
            delayR_ = scratch + FLANGER_BUF_SIZE;
            flangerWritePos_ = 0;
        }
        prepared_ = true;
        return true;
    }

    inline void processBlock(float* left, float* right, uint32_t frames) override {
        if (!prepared_) return;
        setDepth(st.effects[slotId_][1] );
        setRate(st.effects[slotId_][2] );
        updatePhaserCoeffs(frames);
        updateFlangerLFO(frames);
        for (uint32_t i = 0; i < frames; ++i) {
            processPhaserSample(left[i], right[i]);
            processFlangerSample(left[i], right[i]);
        }
    }

    inline void reset() override {
        resetPhaser();
        resetFlanger();
    }

    inline uint32_t scratchFootprintFloats() const override { return FLANGER_BUF_SIZE * 2; }

    inline void setDepth(uint8_t d) {
        if (d == lastDepth_) return;
        lastDepth_ = d;
        phaserDepth_ = fclamp(d * 0.0498f + 0.061f, 0.f, 90.f);
        float fHigh = 1000.0f * semitonesToRatio(phaserDepth_ * 0.5f);
        float deltaF = fHigh - 995.0f;
        float maxDelay = 1.f / deltaF;
        flangerDepth_ = fclamp(maxDelay, 0.f, MAX_FLANGER_DEPTH);
    }

    inline void setRate(uint8_t r) {
        if (r == lastRate_) return;
        lastRate_ = r;
        phaserRate_ = LFO_SPEED[r] * 0.188f + 0.0936f;
        flangerRate_ = LFO_SPEED[r] * 0.5f;
    }

private:
    uint8_t lastDepth_ = 0xFF;
    uint8_t lastRate_  = 0xFF;
    static constexpr int FLANGER_BUF_SIZE = 4096;
    static constexpr float MAX_FLANGER_DEPTH = 0.03f;
    float flangerDepth_ = 0.01f;
    float flangerRate_  = 0.25f;
    float flangerMix_ = 0.45f;
    float phaserDepth_ = 0.75f;
    float phaserRate_  = 0.5f;
    float phaserMix_   = 0.6f;
    float phaserFb_     = 0.7f;

    inline float triLFO(float phase) {
        phase = wrap01(phase);
        return 1.f - fabsf(2.f * phase - 1.f);
    }

    RDX_Common& st = RDX_State::getState().workingPatch.common;
    int sampleRate_ = SAMPLE_RATE;
    bool prepared_ = false;

    static constexpr int NUM_NOTCHES = 2;
    float FREQ_LOW[NUM_NOTCHES] = { 90.f, 500.0f };
    float fNotch_[NUM_NOTCHES]{};
    float z1L_[NUM_NOTCHES]{}, z1R_[NUM_NOTCHES]{};
    float alpha_[NUM_NOTCHES]{};
    float alphaTarget_[NUM_NOTCHES]{};
    float prev_inL_ = 0.f, prev_outL_ = 0.f;
    float prev_inR_ = 0.f, prev_outR_ = 0.f;
    float feedbackL_ = 0.f, feedbackR_ = 0.f;
    float lfoPhase_ = 0.f;
    float rate_ = 0.5f;
    float depthSemitones_ = 24.f;

    float* delayL_ = nullptr;
    float* delayR_ = nullptr;
    uint32_t flangerWritePos_ = 0;
    float flangerPhaseL_ = 0.f;
    float flangerPhaseR_ = 0.5f;
    uint32_t flangerWritePosL_ = 0;
    uint32_t flangerWritePosR_ = 0;

    inline void resetPhaser() {
        for (int i = 0; i < NUM_NOTCHES; ++i) {
            z1L_[i] = z1R_[i] = 0.f;
        }
        prev_inL_ = prev_outL_ = 0.f;
        prev_inR_ = prev_outR_ = 0.f;
        feedbackL_ = feedbackR_ = 0.f;
        lfoPhase_ = 0.f;
    }

    inline void updatePhaserCoeffs(uint32_t frames) {
        lfoPhase_ = wrap01(lfoPhase_ + phaserRate_ * frames / (float)sampleRate_);
        float lfo = triLFO(lfoPhase_);
        for (int i = 0; i < NUM_NOTCHES; ++i) {
            fNotch_[i] = FREQ_LOW[i] * semitonesToRatio(depthSemitones_ * lfo);
            float w = TWO_PI * fNotch_[i] / (float)sampleRate_;
            alphaTarget_[i] = (1.f - w) / (1.f + w);
            alpha_[i] += 0.15f * (alphaTarget_[i] - alpha_[i]);
        }
    }

    inline void processPhaserSample(float& left, float& right) {
        float yL = dcBlock(left,  prev_inL_, prev_outL_, DcTimeConst_);
        yL += feedbackL_ * phaserFb_;

        float yR = dcBlock(right, prev_inR_, prev_outR_, DcTimeConst_);
        yR += feedbackR_ * phaserFb_;

        for (int n = 0; n < 1; ++n) {
            for (int i = 0; i < NUM_NOTCHES; ++i) {
                float tL = -alpha_[i] * yL + z1L_[i];
                z1L_[i] = yL + alpha_[i] * tL;
                yL = tL;

                float tR = -alpha_[i] * yR + z1R_[i];
                z1R_[i] = yR + alpha_[i] * tR;
                yR = tR;
            }
        }

        feedbackL_ = yL;
        feedbackR_ = yR;

        left  = left  * (1.f - phaserMix_) + yL * phaserMix_;
        right = right * (1.f - phaserMix_) + yR * phaserMix_;
    }

    inline void resetFlanger() {
        if (!delayL_ || !delayR_) return;
        for (uint32_t i = 0; i < FLANGER_BUF_SIZE; ++i) delayL_[i] = delayR_[i] = 0.f;
        flangerWritePos_ = 0;
        flangerPhaseL_ = 0.f;
        flangerPhaseR_ = 0.5f;
    }

    inline void updateFlangerLFO(uint32_t frames) {
        float inc = flangerRate_ * frames * DIV_SAMPLE_RATE;
        flangerPhaseL_ = wrap01(flangerPhaseL_ + inc);
        flangerPhaseR_ = wrap01(flangerPhaseR_ + inc);
    }

    inline void processFlangerSample(float& l, float& r) {
        if (!delayL_ || !delayR_) return;
        float lfoL = 0.6f * triLFO(flangerPhaseL_) ;
        float modL = (0.1f + lfoL) * flangerDepth_ * sampleRate_;
        float modR = (0.7f - lfoL) * flangerDepth_ * sampleRate_;

        float readPosL = flangerWritePosL_ - modL;
        if (readPosL < 0.f) readPosL += FLANGER_BUF_SIZE;

        uint32_t iL = (uint32_t)readPosL;
        uint32_t iL2 = (iL + 1) & (FLANGER_BUF_SIZE - 1);
        float fracL = readPosL - (float)iL;

        float outL = delayL_[iL] * (1.f - fracL) + delayL_[iL2] * fracL;

        float readPosR = flangerWritePosR_ - modR;
        if (readPosR < 0.f) readPosR += FLANGER_BUF_SIZE;

        uint32_t iR = (uint32_t)readPosR;
        uint32_t iR2 = (iR + 1) & (FLANGER_BUF_SIZE - 1);
        float fracR = readPosR - (float)iR;

        float outR = delayL_[iR] * (1.f - fracR) + delayL_[iR2] * fracR;

        delayL_[flangerWritePosL_] = l;
        delayR_[flangerWritePosR_] = r;

        l = l * (1.f - flangerMix_) + outL * flangerMix_;
        r = r * (1.f - flangerMix_) + outR * flangerMix_;

        flangerWritePosL_ = (flangerWritePosL_ + 1) % FLANGER_BUF_SIZE;
        if (flangerWritePosL_ >= FLANGER_BUF_SIZE) flangerWritePosL_ -= FLANGER_BUF_SIZE;
        flangerWritePosR_ = (flangerWritePosR_ + 1) % FLANGER_BUF_SIZE;
        if (flangerWritePosR_ >= FLANGER_BUF_SIZE) flangerWritePosR_ -= FLANGER_BUF_SIZE;
    }

    inline float dcBlock(float x, float &x1, float &y1, float k)
    {
        float y = x - x1 + k * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    static constexpr float DcTimeConst_ = 0.996f;
};

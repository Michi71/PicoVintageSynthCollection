// include/dx_engine/fx_touch_wah.h
#pragma once
#include "RDX_Types.h"
#include "RDX_State.h"
#include "RDX_Constants.h"
#include "fx_base.h"
#include "misc.h"
#include <cstring>
#include <cmath>

class FxTouchWah : public FXBase {
public:
    FxTouchWah() = default;

    bool prepare(float* = nullptr, uint32_t = 0, int sampleRate = SAMPLE_RATE) override {
        sampleRate_ = sampleRate;
        reset();
        prepared_ = true;
        return true;
    }

    inline void processBlock(float* left, float* right, uint32_t frames) override {
        if (!prepared_) return;

        const uint8_t sensParam = st.effects[slotId_][1];
        const uint8_t resoParam = st.effects[slotId_][2];
        setSens(sensParam);
        setReso(resoParam);

        if (recovering_) {
            recoveryFade_ += 0.0025f;
            if (recoveryFade_ >= 1.f) {
                recoveryFade_ = 1.f;
                recovering_ = false;
            }
        }

        const float minF = 0.02f;
        const float maxF = 0.4f;
        const float envAttack = 0.02f + 0.08f * sens_;
        const float envRelease = 0.003f;

        float env = env_;
        float a = a_;

        for (uint32_t i = 0; i < frames; ++i) {
            float l = left[i];
            float r = right[i];

            float level = 0.5f * (fabsf(l) + fabsf(r));
            float coeff = (level > env) ? envAttack : envRelease;
            env += (level - env) * coeff;

            if ((i & 7) == 0) {
                float freq = minF + (maxF - minF) * env * sens_;
                freq = fclamp(freq, minF, maxF);
                a = (1.f - freq) / (1.f + freq);
            }

            float xL = l - prevInL_ + DC_TC * prevOutL_;
            prevInL_ = l;
            prevOutL_ = xL;
            float xR = r - prevInR_ + DC_TC * prevOutR_;
            prevInR_ = r;
            prevOutR_ = xR;

            float fb = (FEEDBACK_BASE + reso_ * 0.2f) * recoveryFade_;
            xL += feedbackL_ * fb;
            xR += feedbackR_ * fb;

            for (int s = 0; s < STAGES; ++s) {
                float yL = -a * xL + z1L_[s];
                z1L_[s] = xL + a * yL;
                xL = yL;

                float yR = -a * xR + z1R_[s];
                z1R_[s] = xR + a * yR;
                xR = yR;
            }

            feedbackL_ = xL * reso_;
            feedbackR_ = xR * reso_;

            float mixAmt = WET_DRY_MIX * recoveryFade_;
            left[i]  = l * (1.f - mixAmt) + xL * mixAmt;
            right[i] = r * (1.f - mixAmt) + xR * mixAmt;
        }

        env_ = env;
        a_ = a;
    }

    inline void reset() override {
        if (!prepared_) return;
        memset(z1L_, 0, sizeof(z1L_));
        memset(z1R_, 0, sizeof(z1R_));
        feedbackL_ = 0.f, feedbackR_ = 0.f;
        env_ = 0.f;
        a_ = 0.5f;
        sens_ = 0.5f;
        reso_ = 0.5f;
        prevInL_ = 0.f;
        prevOutL_ = 0.f;
        prevInR_ = 0.f;
        prevOutR_ = 0.f;
    }

private:
    RDX_Common& st = RDX_State::getState().workingPatch.common;

    static constexpr int STAGES = 6;
    static constexpr float FEEDBACK_BASE = 0.6f;
    static constexpr float WET_DRY_MIX = 0.7f;
    static constexpr float DC_TC = 0.996f;

    bool prepared_ = false;
    bool recovering_ = false;
    float recoveryFade_ = 1.f;
    int sampleRate_ = SAMPLE_RATE;

    float z1L_[STAGES]{};
    float z1R_[STAGES]{};
    float feedbackL_ = 0.f, feedbackR_ = 0.f;
    float env_ = 0.f;
    float a_ = 0.5f;
    float sens_ = 0.5f;
    float reso_ = 0.5f;

    float prevInL_ = 0.f, prevOutL_ = 0.f;
    float prevInR_ = 0.f, prevOutR_ = 0.f;

    inline void setSens(uint8_t s) { sens_ = s * MIDI_NORM; }
    inline void setReso(uint8_t r) { reso_ = 0.4f +  r * 0.7f * MIDI_NORM; }
};

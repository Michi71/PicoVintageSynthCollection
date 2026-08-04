// include/dx_engine/fx_reverb.h
#pragma once
#include "fx_base.h"
#include "RDX_Types.h"
#include "RDX_State.h"
#include "RDX_Constants.h"
#include <cmath>
#include <cstring>

constexpr int NUM_COMBS = 4;
constexpr int NUM_ALLPASSES = 2;

static const float comb_lengths_ms[NUM_COMBS] = {47.3f, 53.7f, 71.1f, 89.5f};
static const float allpass_lengths_ms[NUM_ALLPASSES] = {9.7f, 3.2f};

class FxReverb : public FXBase {
public:
    FxReverb() = default;

    bool prepare(float* scratch, uint32_t scratchSize,
                 int sampleRate) override
    {
        (void)scratchSize;
        sampleRate_ = sampleRate;
        float* ptr = scratch;
        for (int ch=0; ch<2; ++ch) {
            for (int i=0; i<NUM_COMBS; ++i) {
                int len = int((comb_lengths_ms[i] / 1000.f) * sampleRate_) + ch * 17;
                combSize_[ch][i] = len;
                combBuf_[ch][i] = ptr;
                ptr += len;
                std::memset(combBuf_[ch][i], 0, len * sizeof(float));
                combIdx_[ch][i] = 0;
                combLPF_[ch][i] = 0.f;
            }
            for (int i=0; i<NUM_ALLPASSES; ++i) {
                int len = int((allpass_lengths_ms[i] / 1000.f) * sampleRate_) + i + ch;
                allSize_[ch][i] = len;
                allBuf_[ch][i] = ptr;
                ptr += len;
                std::memset(allBuf_[ch][i], 0, len * sizeof(float));
                allIdx_[ch][i] = 0;
            }
        }
        prepared_ = true;
        return true;
    }

    inline void reset() override {
        if (!prepared_) return;
        damping_ = 0.3f;
        lastTime_ = -1.f;
        prev_in = 0.0f;
        prev_out = 0.0f;
    }

    inline uint32_t scratchFootprintFloats() const override {
        uint32_t total = 0;
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < NUM_COMBS; ++i) total += (uint32_t)combSize_[ch][i];
            for (int i = 0; i < NUM_ALLPASSES; ++i) total += (uint32_t)allSize_[ch][i];
        }
        return total;
    }

    inline void processBlock(float* L, float* R, uint32_t n) override {
        if (!prepared_) return;
        float depth = st.effects[slotId_][1] / 127.0f * 0.2f;
        float time  = st.effects[slotId_][2] / 127.0f;
        updateFeedback(time);
        for (uint32_t i=0; i<n; ++i) {
            float in = 0.5f * (L[i] + R[i]);
            prev_out = in - prev_in + DcTimeConst * prev_out;
            prev_in = in;
            float wetL = processCh(0, prev_out);
            float wetR = processCh(1, prev_out);
            L[i] = (1.0f - depth) * L[i] + depth * wetL;
            R[i] = (1.0f - depth) * R[i] + depth * wetR;
        }
    }

private:
    RDX_Common& st = RDX_State::getState().workingPatch.common;
    float* combBuf_[2][NUM_COMBS];
    int combSize_[2][NUM_COMBS];
    int combIdx_[2][NUM_COMBS];
    float combLPF_[2][NUM_COMBS];
    float combGain_[NUM_COMBS];

    float* allBuf_[2][NUM_ALLPASSES];
    int allSize_[2][NUM_ALLPASSES];
    int allIdx_[2][NUM_ALLPASSES];
    float allGain_[NUM_ALLPASSES] = {0.7f, 0.7f};

    float damping_ = 0.3f;
    float lastTime_ = -1.f;
    float prev_in = 0.0f;
    float prev_out = 0.0f;
    float DcTimeConst = 0.996f;

    inline void updateFeedback(float t) {
        if (fabsf(t - lastTime_) < 1e-4f) return;
        float rt60 = 0.25f * powf(24.f, t);
        for (int i=0; i<NUM_COMBS; ++i) {
            float delaySec = combSize_[0][i] * DIV_SAMPLE_RATE;
            float g = powf(10.0f, -3.0f * delaySec / rt60);
            combGain_[i] = fminf(g, 0.95f);
        }
        lastTime_ = t;
    }

    inline  float processCh(int ch, float in) {
        float sum = 0.f;
        for (int i=0; i<NUM_COMBS; ++i) sum += processComb(ch, i, in);
        sum *= 0.25f;
        for (int i=0; i<NUM_ALLPASSES; ++i) sum = processAll(ch, i, sum);
        return sum;
    }

    inline float processComb(int ch, int i, float x) {
        float* buf = combBuf_[ch][i];
        int& idx = combIdx_[ch][i];
        int N = combSize_[ch][i];
        float y = buf[idx];
        combLPF_[ch][i] = y + damping_ * (combLPF_[ch][i] - y);
        buf[idx] = x + combLPF_[ch][i] * combGain_[i];
        if (++idx >= N) { idx = 0; }
        return y;
    }

    inline float processAll(int ch, int i, float x) {
        float* buf = allBuf_[ch][i];
        int& idx = allIdx_[ch][i];
        int N = allSize_[ch][i];
        float g = allGain_[i];
        float y = buf[idx];
        buf[idx] = x + y * g;
        float out = y - g * x;
        if (++idx >= N) { idx = 0; }
        return out;
    }
};

// dsp_reverb.h
#pragma once
#include <cmath>
#include <cstring>
#include "cp_hot.h"

class Reverb {
public:
    void init(float sampleRate) {
        sr_ = sampleRate;
        float scale = sampleRate / 44100.0f;
        for (int i = 0; i < 4; ++i) {
            combLenL_[i] = clampLen(static_cast<int>(combLenLRef_[i] * scale), kMaxCombLen);
            combLenR_[i] = clampLen(static_cast<int>(combLenRRef_[i] * scale), kMaxCombLen);
        }
        for (int i = 0; i < 2; ++i) {
            apLenL_[i] = clampLen(static_cast<int>(apLenLRef_[i] * scale), kMaxAPLen);
            apLenR_[i] = clampLen(static_cast<int>(apLenRRef_[i] * scale), kMaxAPLen);
        }
        reset();
    }

    void setDepth(float norm) {
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        depth_ = norm;
    }

    void CP_HOT(process)(float& l, float& r) {
        float dryL = l;
        float dryR = r;
        float input = (l + r) * 0.5f;

        float wetL = 0.0f;
        for (int i = 0; i < 4; ++i)
            wetL += processComb(combL_[i], combIdxL_[i], combLenL_[i], combStoreL_[i], input);

        float wetR = 0.0f;
        for (int i = 0; i < 4; ++i)
            wetR += processComb(combR_[i], combIdxR_[i], combLenR_[i], combStoreR_[i], input);

        wetL = processAllpass(apL_[0], apIdxL_[0], apLenL_[0], wetL);
        wetL = processAllpass(apL_[1], apIdxL_[1], apLenL_[1], wetL);
        wetR = processAllpass(apR_[0], apIdxR_[0], apLenR_[0], wetR);
        wetR = processAllpass(apR_[1], apIdxR_[1], apLenR_[1], wetR);

        l = dryL * (1.0f - depth_) + wetL * depth_;
        r = dryR * (1.0f - depth_) + wetR * depth_;
    }

    void reset() {
        for (int i = 0; i < 4; ++i) {
            memset(combL_[i], 0, sizeof(float) * kMaxCombLen);
            memset(combR_[i], 0, sizeof(float) * kMaxCombLen);
            combIdxL_[i] = 0;
            combIdxR_[i] = 0;
            combStoreL_[i] = 0.0f;
            combStoreR_[i] = 0.0f;
        }
        for (int i = 0; i < 2; ++i) {
            memset(apL_[i], 0, sizeof(float) * kMaxAPLen);
            memset(apR_[i], 0, sizeof(float) * kMaxAPLen);
            apIdxL_[i] = 0;
            apIdxR_[i] = 0;
        }
    }

private:
    static constexpr int kMaxCombLen = 1700;
    static constexpr int kMaxAPLen = 600;

    static constexpr int combLenLRef_[4] = {1557, 1617, 1491, 1422};
    static constexpr int combLenRRef_[4] = {1580, 1640, 1514, 1445};
    static constexpr int apLenLRef_[2] = {556, 441};
    static constexpr int apLenRRef_[2] = {579, 464};

    static constexpr float kCombFeedback = 0.84f;
    static constexpr float kDampCoeff = 0.2f;
    static constexpr float kAllpassFeedback = 0.5f;

    float sr_;
    float depth_ = 0.0f;

    int combLenL_[4];
    int combLenR_[4];
    int apLenL_[2];
    int apLenR_[2];

    float combL_[4][kMaxCombLen];
    float combR_[4][kMaxCombLen];
    float apL_[2][kMaxAPLen];
    float apR_[2][kMaxAPLen];

    int combIdxL_[4];
    int combIdxR_[4];
    int apIdxL_[2];
    int apIdxR_[2];

    float combStoreL_[4];
    float combStoreR_[4];

    static inline int clampLen(int v, int maxLen) {
        if (v < 1) v = 1;
        if (v > maxLen) v = maxLen;
        return v;
    }

    inline float processComb(float* buf, int& idx, int len, float& store, float input) {
        float delayed = buf[idx];
        store = delayed * (1.0f - kDampCoeff) + store * kDampCoeff;
        float out = delayed;
        buf[idx] = store * kCombFeedback + input;
        if (++idx >= len) idx = 0;
        return out;
    }

    inline float processAllpass(float* buf, int& idx, int len, float input) {
        float delayed = buf[idx];
        float out = delayed - input * kAllpassFeedback;
        buf[idx] = input + delayed * kAllpassFeedback;
        if (++idx >= len) idx = 0;
        return out;
    }
};

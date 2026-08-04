#pragma once
#include "ram_hot.h"
#include "RDX_Constants.h"
#include "RDX_State.h"

class  RDX_Envelope {
public:
    enum class Stage { ATTACK, DECAY1, DECAY2, RELEASE, SUSTAIN_STAGE, IDLE };

    inline void initAEG(const uint8_t rates[4], const uint8_t levels[4], bool need_reset = true) {
        for (int i = 0; i < 4; ++i) {
            rateIndices_[i]  = rates[i];
            levelIndices_[i] = AEG_LEVEL[levels[i]] ;
        }
        levelIndices_[(int)Stage::SUSTAIN_STAGE] = levelIndices_[(int)Stage::DECAY2];
        levelIndices_[(int)Stage::IDLE] = levelIndices_[(int)Stage::RELEASE];
        if (need_reset) reset();
    }

    inline void reset() {
        currentL_ = 0.0f; targetL_  = 1.0f; stage_    = Stage::IDLE;
        gate_     = false; rising_   = true; k_ = 1.0f ; c_ = 0.0f;
    }

    inline float __attribute__((always_inline, hot)) RAM_HOT(processAEG)() {
        float v = processInternal();
        if (rising_) { return k_ * v + c_ ; } else { return rdxGain(v); }
    }

    inline float __attribute__((always_inline, hot)) RAM_HOT(processInternal)() {
        if (stage_ == Stage::IDLE) return currentL_;
        if (stage_ == Stage::SUSTAIN_STAGE) { if (!gate_) enterStage(Stage::RELEASE); return currentL_; }
        if ( rising_ ) {
            currentL_ += step_;
            if (currentL_ >= targetL_) { currentL_ = targetL_; advanceStage(); }
        } else {
            currentL_ -= step_;
            if (currentL_ <= targetL_) { currentL_ = targetL_; advanceStage(); }
        }
        return currentL_;
    }

    inline void gate(bool g) {
        bool was = gate_; gate_ = g;
        bool glide = (common_.monoPoly != RDX_MODE_POLY) && (common_.portaTime > 0);
        if (!glide) {
            if (g && !was) { if (stage_ == Stage::IDLE || stage_ == Stage::RELEASE ) currentL_ = 0.0f; enterStage(Stage::ATTACK); }
            else if (!g && was) { if (stage_ != Stage::IDLE && stage_ != Stage::RELEASE) enterStage(Stage::RELEASE); }
        } else {
            if (g && was) { /* do nothing */ }
            else if (g && !was){
                if (common_.monoPoly == RDX_MODE_MONO_LEGATO) { enterStage(Stage::ATTACK); }
                else if (common_.monoPoly == RDX_MODE_MONO_FULL) {
                    if (stage_ == Stage::IDLE || stage_ == Stage::RELEASE ) currentL_ = 0.0f;
                    enterStage(Stage::ATTACK);
                }
            }
        }
    }

    inline Stage getStage() const { return stage_; }
    inline float getLevel() const { return currentL_; }
    inline bool isActive() const { return stage_ != Stage::IDLE; }
    inline bool isGateOn() const { return gate_; }

private:
    RDX_Common& common_ = RDX_State::getState().workingPatch.common;
    inline void enterStage(Stage s) {
        targetL_ = levelIndices_[static_cast<int>(s)]; stage_  = s;
        if (s == Stage::RELEASE && rising_) {
            float val = k_ * currentL_ + c_; currentL_ = rdxGainInv(val);
        }
        if (s == Stage::SUSTAIN_STAGE) {
            rising_ = false;
            if (currentL_ < 0.0f) { currentL_ = 0.0f; advanceStage(); }
            return;
        }
        if (s == Stage::IDLE) { return; }
        rising_ = (targetL_ > currentL_) ;
        int idx = rateIndices_[static_cast<int>(s)];
        if ( rising_ ) {
            float V0 = mapLevel(currentL_); float V1 = mapLevel(targetL_);
            if (targetL_ != currentL_) { k_ = (V1 - V0) / (targetL_ - currentL_); c_ = V1 - k_ * targetL_; }
            else { k_ = V0 / currentL_; c_ = 0.0f; }
            float unitsPerSec = 4.1f * PEG_SPEED[idx];
            step_ =  unitsPerSec * DIV_SAMPLE_RATE;
        } else {
            float unitsPerSec = 0.27f * PEG_SPEED[idx];
            step_ =  unitsPerSec * DIV_SAMPLE_RATE;
        }
    }

    inline void advanceStage() {
        switch (stage_) {
            case Stage::IDLE: enterStage(Stage::ATTACK); break;
            case Stage::ATTACK: enterStage(gate_ ? Stage::DECAY1 : Stage::RELEASE); break;
            case Stage::DECAY1: enterStage(gate_ ? Stage::DECAY2 : Stage::RELEASE); break;
            case Stage::DECAY2: enterStage(gate_ ? Stage::SUSTAIN_STAGE : Stage::RELEASE); break;
            case Stage::SUSTAIN_STAGE: if (!gate_) { enterStage(Stage::RELEASE); } break;
            case Stage::RELEASE: reset(); break;
            default: break;
        }
    }

    float currentL_ = 0.0f; float targetL_  = 0.0f; float step_ = 0.0f;
    bool  rising_   = true; float c_ = 0.0f; float k_ = 1.0f;
    Stage stage_ = Stage::IDLE; volatile bool gate_ = false;
    int     rateIndices_[4] = {0,0,0,0};
    float   levelIndices_[6] = {0,0,0,0,0,0};
};

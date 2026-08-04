#pragma once
#include "ram_hot.h"
#include "RDX_Constants.h"

class  RDX_PEG {
public:
    enum class Stage { ATTACK, DECAY1, DECAY2, RELEASE, SUSTAIN_STAGE, IDLE };

    inline void initPEG(const uint8_t rates[4], const uint8_t levels[4], bool need_reset = true) {
        for (int i = 0; i < 4; ++i) { rateIndices_[i]  = rates[i]; levelTargets_[i] = levels[i]; }
        if (need_reset) reset();
    }

    inline float __attribute__((always_inline)) RAM_HOT(outVal)() const { return current_ - CENTER; }

    inline float __attribute__((always_inline)) RAM_HOT(processPEG)() {
        if (stage_ == Stage::IDLE) return outVal();
        if (stage_ == Stage::SUSTAIN_STAGE) { if (!gate_) enterStage(Stage::RELEASE); return outVal(); }
        if ( rising_ ) {
            current_ += stepIncrement_;
            if (current_ >= border_) { current_ = border_; advanceStage(); }
        } else {
            current_ += stepIncrement_;
            if (current_ <= border_) { current_ = border_; advanceStage(); }
        }
        return outVal();
    }

    inline void gate(bool g) {
        bool was = gate_; gate_ = g;
        if (g && !was) { if (stage_ == Stage::IDLE) current_ = CENTER; enterStage(Stage::ATTACK); }
        else if (!g && was) { if (stage_ != Stage::IDLE && stage_ != Stage::RELEASE) enterStage(Stage::RELEASE); }
    }

    inline void reset() {
        current_ = CENTER; border_ = CENTER; stage_ = Stage::IDLE;
        gate_ = false; rising_ = true; stepIncrement_ = 0.0f;
    }

    inline Stage getStage() const { return stage_; }
    inline bool isActive() const { return stage_ != Stage::IDLE; }
    inline bool isGateOn() const { return gate_; }

private:
    inline void enterStage(Stage s) {
        stage_  = s; border_ = stageTarget(s); float speed;
        if (s == Stage::SUSTAIN_STAGE) { rising_ = false; return; }
        if (s == Stage::IDLE) { return; }
        rising_ = (border_ > current_) ; target_ = border_;
        const int idx = (int)rateIndices_[(int)s];
        if (rising_) { speed = PEG_SPEED[idx]; stepIncrement_ =   speed * DIV_SAMPLE_RATE ; }
        else { speed = PEG_SPEED[idx]; stepIncrement_ = -speed * DIV_SAMPLE_RATE * 1.1f ; }
    }

    inline void advanceStage() {
        switch (stage_) {
            case Stage::ATTACK: enterStage(gate_ ? Stage::DECAY1 : Stage::RELEASE); break;
            case Stage::DECAY1: enterStage(gate_ ? Stage::DECAY2 : Stage::RELEASE); break;
            case Stage::DECAY2: enterStage(gate_ ? Stage::SUSTAIN_STAGE : Stage::RELEASE); break;
            case Stage::SUSTAIN_STAGE: if (!gate_) { enterStage(Stage::RELEASE); } break;
            case Stage::RELEASE: stage_ = Stage::IDLE; enterStage(Stage::IDLE); break;
            default: break;
        }
    }

    inline float stageTarget(Stage s) const {
        switch (s) {
            case Stage::ATTACK:  return levelTargets_[0];
            case Stage::DECAY1:  return levelTargets_[1];
            case Stage::DECAY2:  return levelTargets_[2];
            case Stage::RELEASE: return levelTargets_[3];
            case Stage::SUSTAIN_STAGE: return levelTargets_[2];
            default:             return outVal();
        }
    }

    const float CENTER = 64.0f;
	volatile float current_ = CENTER;
    float border_  = CENTER; float target_ = CENTER; float stepIncrement_= 0.0f;
    bool rising_ = false; Stage stage_ = Stage::IDLE; bool gate_ = false;
    float rateIndices_[4] = {0.f,0.f,0.f,0.f};
    float levelTargets_[4] = {CENTER,CENTER,CENTER,CENTER};
};

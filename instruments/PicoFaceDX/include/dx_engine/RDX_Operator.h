// RDX_Operator.h
#pragma once

#include "misc.h"
#include "RDX_Types.h"
#include "RDX_State.h"
#include "RDX_Envelope.h"
#include "RDX_Constants.h" // provides rdxGain(), RDX_GAIN[], sinTable[], sin01()

class RDX_Operator {
public:
    RDX_Operator(int idx)
        : params_(RDX_State::getState().workingPatch.ops[idx]),
          idx_(idx) {}

    inline void setParams( int note, int vel, float baseHz) {
        setFrequency(baseHz);
        scaling_ = calcScalingFactor( note, params_.scaleLD, (RDX_ScaleCurve)params_.scaleLC,  params_.scaleRD, (RDX_ScaleCurve)params_.scaleRC);
        velogain_ = velocityGain( vel, params_.velSens, 1.08f);
        outGain_  = rdxGain(params_.outLevel * velogain_ ) * scaling_;
        env_.initAEG(params_.egRate, params_.egLevel, true);
        fbRectify_ = (params_.fbType != RDX_FB_SAW) ;
        fbScale_  = FEEDBACK_K[params_.feedback]   ;
        enabled_ = params_.enable;
    }

    inline void updateParams() {
        outGain_  = rdxGain(params_.outLevel * velogain_ ) * scaling_;
        env_.initAEG(params_.egRate, params_.egLevel, false);
        fbRectify_ = (params_.fbType != RDX_FB_SAW) ;
        fbScale_  = FEEDBACK_K[params_.feedback]   ;
        enabled_ = params_.enable;
    }

    inline RDX_OpParams& params() { return params_; }
    inline const RDX_OpParams& params() const { return params_; }

    inline void reset() {
        phase_    = 0.0f; fbAcc_   = 0.0f; fbFilter_ = 0.f;  env_.reset();
    }

    inline void gate(bool on) { env_.gate(on); }

    inline float __attribute__((always_inline, hot)) RAM_HOT(compute)(float inputPhaseOffset, float phaseModSemitones = 0.f) {
        if (!params_.enable) return 0.f;
        if (params_.fbType && fbAcc_ < 0.f) fbAcc_ = -fbAcc_;
        fbFilter_ += fbLpCoef_ * (fbAcc_ - fbFilter_);
        float lookupPhase = wrap01(phase_ + inputPhaseOffset + fbFilter_ * fbScale_ );
        if (phaseModSemitones != 0.0f)
            phase_ += phaseInc_ * semitonesToRatio(phaseModSemitones);
        else
            phase_ += phaseInc_;
        if(phase_>1.0f) phase_ -= 1.0f;
        fbAcc_  = sin01(lookupPhase);
        return fbAcc_ * outGain_ * env_.processAEG();
    }

    inline void setFrequency(float baseHz) {
		float freqHz = 0.0f;
		if (params_.freqMode == 0) {
			if (params_.freqCoarse > 0)
				freqHz = baseHz * (params_.freqCoarse + params_.freqFine * 0.01f);
			else
				freqHz = baseHz * (0.5f + params_.freqFine * 0.005f);
		} else {
			float c = powf(10.0f, fclamp(params_.freqCoarse >> 3, 0.0f, 3.0f));
			constexpr float n = 9.772f;
			float step = powf(n, params_.freqFine * 0.01010101f );
			freqHz = c * step;
		}
		int detuneVal = params_.freqDetune;
		int dt = detuneVal - 64;
		if (dt != 0) {
			float detuneFactor = powf(1.00033913f, float(dt));
			freqHz *= detuneFactor;
		}
		phaseInc_ = freqHz * DIV_SAMPLE_RATE;
	}

    inline bool isActive() const { return env_.isActive(); }
    inline float getEnvLevel() const { return env_.getLevel(); }

private:
    RDX_OpParams& params_;
    RDX_Envelope env_;
    RDX_Controls& ctl_ = RDX_State::getState().controls;
    RDX_Common& common_ = RDX_State::getState().workingPatch.common;
    float scaling_ = 1.0f; float velogain_ = 1.0f; bool  enabled_ = true;
    float fbFilter_ = 0.f; float fbLpCoef_ = 0.356f;
    int idx_ = 0;
    float phase_     = 0.0f; float phaseInc_  = 0.0f; float fbAcc_     = 0.0f;
    float outGain_   = 1.0f; float fbScale_   = 0.0f; bool  fbRectify_ = false;

	inline float __attribute__((always_inline)) RAM_HOT(linearScale)(float x) { return   x; }
	inline float __attribute__((always_inline)) RAM_HOT(expScale)(float x) { return   (1.0f - expf(-4.0f * x)); }

    inline float __attribute__((always_inline)) RAM_HOT(calcScalingFactor)(uint8_t note, int8_t lDepth, RDX_ScaleCurve lCurve, int8_t rDepth, RDX_ScaleCurve rCurve) {
        constexpr int BP = 60;
        constexpr float LEFT_RANGE  = (float)BP;
        constexpr float RIGHT_RANGE = (float)(127-BP);
        const float MAX_ATTENUATION_K = 8.0f;
        const float MAX_BOOST_K = 8.0f;
        float factor = 1.0f; float normK = 1.0f; float distance = 0.0f;
        if (note > BP) {
            distance = note - BP; normK = distance / 127.0f / LEFT_RANGE;
            switch (rCurve) {
                case RDX_SCALE_NEG_LIN: factor = 1.0f / (1.0f + (float)rDepth * normK * MAX_ATTENUATION_K); break;
                case RDX_SCALE_NEG_EXP: factor = 1.0f / (1.0f + AEG_LEVEL[rDepth] * normK * MAX_ATTENUATION_K); break;
                case RDX_SCALE_POS_EXP: factor = 1.0f + AEG_LEVEL[rDepth] * normK * MAX_BOOST_K; break;
                case RDX_SCALE_POS_LIN: factor = 1.0f + (float)rDepth * normK * MAX_BOOST_K; break;
                default: return 1.0f;
            }
        } else if (note < BP) {
            distance = BP - note; normK = distance / 127.0f / RIGHT_RANGE;
            switch (lCurve) {
                case RDX_SCALE_NEG_LIN: factor = 1.0f / (1.0f + (float)lDepth * normK * MAX_ATTENUATION_K); break;
                case RDX_SCALE_NEG_EXP: factor = 1.0f / (1.0f + AEG_LEVEL[lDepth] * normK * MAX_ATTENUATION_K); break;
                case RDX_SCALE_POS_EXP: factor = 1.0f + AEG_LEVEL[lDepth] * normK * MAX_BOOST_K; break;
                case RDX_SCALE_POS_LIN: factor = 1.0f + (float)lDepth * normK * MAX_BOOST_K; break;
                default: return 1.0f;
            }
        }
        return fclamp(factor, 0.f, 2.f);
    }

    inline float __attribute__((always_inline)) RAM_HOT(velocityGain)(uint8_t vel, uint8_t sens, float max_out = 1.1f) {
        float normSens = sens / 127.0f;
        float factor = (1.0f - normSens) + VELO_SENS[vel] * normSens;
        return max_out * factor;
    }
};

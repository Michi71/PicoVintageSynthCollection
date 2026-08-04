// RD_VintageFX: Vintage FX chain for MKS-20/MK-80 emulation.
// Signal flow: in -> DAC filter -> bass shelf -> treble shelf -> tremolo -> BBD chorus (stereo) -> volume.
// Mono in / stereo out. Native rate 20000/32000 Hz. RP2350 optimized.

#ifndef RD_EFFECTS_H
#define RD_EFFECTS_H

#include <stdint.h>
#include "rd_params.h"

class RD_VintageFX {
public:
    void init(float sampleRate);
    void setSampleRate(float sr);
    void setParam(uint8_t id, float v01); // RdParamId from "rd_params.h"
    void process(float in, float* outL, float* outR); // per-sample, hot path

private:
    // Params
    float volume_ = 0.8f;
    float chorusOn_ = 0;
    float chorusRate_ = 0.4f;
    float chorusDepth_ = 0.5f;
    float tremOn_ = 0;
    float tremRate_ = 0.5f;
    float tremDepth_ = 0.5f;
    float bass_ = 0.5f;
    float treble_ = 0.5f;
    float dacOn_ = 1.0f;
    // Phaser (mono 4-stage allpass; PicoFaceCP port). Defaults: off / 50% / 50%.
    float phaserOn_    = 0.0f; // RD_PARAM_PHASER_ON
    float phaserRate_  = 0.5f; // RD_PARAM_PHASER_RATE
    float phaserDepth_ = 0.5f; // RD_PARAM_PHASER_DEPTH
    float sampleRate_ = 32000.0f;

    // DAC lowpass
    float dacLpState_;
    float dacLpState2_ = 0; // second pole (12 dB/oct)
    float dacLpCoef_;

    // Shelves
    float bassLpState_;
    float bassGain_;
    float bassCoef_;
    float trebleLpState_;
    float trebleGain_;
    float trebleCoef_;

    // Tremolo
    float tremPhase_;
    float tremInc_;

    // Phaser (mono 4-stage allpass, PicoFaceCP port)
    static constexpr int kPhaserStages = 4;
    float    phPhase_;                 // LFO phase 0..1
    float    phInc_;                   // phase increment per sample
    float    phRateHz_;                // mapped rate in Hz
    float    phA_;                     // current allpass coefficient
    float    phFb_;                    // current feedback
    uint32_t phCnt_;                   // 8-sample coefficient decimation counter
    float    phX1_[kPhaserStages];     // APF input history
    float    phY1_[kPhaserStages];     // APF output history
    float    phLast_;                  // last feedback sample

    // Chorus
    static constexpr int kDelayLen = 512;
    float delay_[kDelayLen];
    int writeIdx_;
    float chorusPhase_;
    float chorusInc_;
    float chorusBaseSamples_;
    float chorusModSamples_;
    float bbdLpStateA_;
    float bbdLpStateB_;
    float bbdLpCoef_;

    // Helpers
    static float sinApprox(float phase01);
    static float triangle(float phase01);
};

#endif // RD_EFFECTS_H

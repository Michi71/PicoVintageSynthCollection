// RDX_Synth.h
#pragma once
#include <cstring>
#include "dx_engine_config.h"
#include "RDX_Voice.h"
#include "RDX_Types.h"
#include "RDX_State.h"
#include "RDX_VoiceAlloc.h"

class RDX_Synth {
public:
    inline void init() {
        for (auto& v : voices_) { v.init(); }
    }

    inline RDX_Patch& currentPatch() { return state_.workingPatch; }
    inline const RDX_Patch& currentPatch() const { return state_.workingPatch; }

    inline void applyPatch(const RDX_Patch& patch) {
        outputGain_ = 0.0f;
        for (int i = 0; i < MAX_VOICES; i++) { voices_[i].init(); }
        state_.workingPatch = patch;
        calcOutputGain();
        state_.storedPatch = patch;
    }

    inline void noteOn(uint8_t note, uint8_t vel) {
        const uint8_t mode = patch_.common.monoPoly;
        const int idx = voiceAlloc_.findVoice(voices_, MAX_VOICES, note, vel, mode);
        if (mode == RDX_MODE_MONO_LEGATO && voiceAlloc_.legatoPending()) {
            voices_[idx].noteOn(note, vel);
        } else {
            voices_[idx].noteOn(note, vel);
        }
    }

    inline void noteOff(uint8_t note) {
        voiceAlloc_.noteOff(voices_, MAX_VOICES, note, patch_.common.monoPoly);
    }

    float __attribute__((always_inline)) RAM_HOT(process)() {
        float mix = 0.f;
        const float outGain = outputGain_;
        for (int i = 0; i < MAX_VOICES; i++) {
                if (!voices_[i].isActive()) continue; // skip idle voices — re-synced on noteOn via syncLFO()
                mix += voices_[i].step() * outGain;
        }
        return mix;
    }

    void __attribute__((always_inline, hot)) RAM_HOT(renderAudioBlock)(float* outL, float* outR, uint32_t len = DMA_BUFFER_LEN) {
        float sample = 0.f;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (!voices_[i].isActive()) continue; // skip idle voices — re-synced on noteOn via syncLFO()
            voices_[i].updateLfo();
        }
        for (uint32_t i = 0; i < len; ++i) {
            sample = process();
            outL[i] = sample;
            outR[i] = sample;
        }
    }

    inline void updateCache() {
        voices_[voiceUpdateIdx_].cacheParams();
        voiceUpdateIdx_ ++;
        if (voiceUpdateIdx_ >= MAX_VOICES) voiceUpdateIdx_ = 0;
    }

    inline RDX_Patch DigiChordPatch() {
        RDX_Patch p{};
        const char name[10] = {'D','i','g','i','C','h','o','r','d','\0'};
        memcpy(p.common.voiceName, name, 10);
        p.common.reserved1[0] = 0; p.common.reserved1[1] = 0;
        p.common.transpose   = 244;
        p.common.monoPoly    = 1;
        p.common.portaTime   = 0;
        p.common.pbRange     = 66;
        p.common.algorithm   = 3;
        p.common.lfoWave     = 0;
        p.common.lfoSpeed    = 85;
        p.common.lfoDelay    = 0;
        p.common.lfoPMD      = 0;
        p.common.pegRate[0]  = 64; p.common.pegRate[1]  = 64; p.common.pegRate[2]  = 64; p.common.pegRate[3]  = 64;
        p.common.pegLevel[0] = 64; p.common.pegLevel[1] = 64; p.common.pegLevel[2] = 64; p.common.pegLevel[3] = 64;
        p.common.effects[0][0] = 4; p.common.effects[0][1] = 64; p.common.effects[0][2] = 64;
        p.common.effects[1][0] = 7; p.common.effects[1][1] = 59; p.common.effects[1][2] = 20;
        p.common.reserved2[0] = 0; p.common.reserved2[1] = 0; p.common.reserved2[2] = 0;
        uint8_t opEnable[4]       = {1,1,1,1};
        uint8_t egRate[4][4]      = {{127,44,46,50},{127,81,80,64},{127,44,68,72},{127,105,54,64}};
        uint8_t egLevel[4][4]     = {{127,81,0,0},{127,127,0,0},{127,81,0,0},{127,53,0,0}};
        uint8_t rateScaling[4]    = {0,0,0,0};
        uint8_t scaleLD[4]        = {0,20,0,0};
        uint8_t scaleRD[4]        = {20,0,0,0};
        uint8_t scaleLC[4]        = {0,3,0,0};
        uint8_t scaleRC[4]        = {0,0,0,0};
        uint8_t lfoAMD[4]         = {0,0,0,0};
        uint8_t lfoPMDEnable[4]   = {1,1,1,1};
        uint8_t pegEnable[4]      = {1,1,1,1};
        uint8_t velSens[4]        = {40,40,57,65};
        uint8_t outLevel[4]       = {109,70,105,100};
        uint8_t feedback[4]       = {0,97,0,27};
        uint8_t fbType[4]         = {0,0,0,1};
        uint8_t freqMode[4]       = {0,0,0,0};
        uint8_t freqCoarse[4]     = {1,14,1,1};
        uint8_t freqFine[4]       = {0,0,0,0};
        uint8_t freqDetune[4]     = {0,0,0,0};
        for (int i = 0; i < 4; i++) {
            p.ops[i].enable = opEnable[i];
            p.ops[i].egRate[0] = egRate[i][0]; p.ops[i].egRate[1] = egRate[i][1]; p.ops[i].egRate[2] = egRate[i][2]; p.ops[i].egRate[3] = egRate[i][3];
            p.ops[i].egLevel[0] = egLevel[i][0]; p.ops[i].egLevel[1] = egLevel[i][1]; p.ops[i].egLevel[2] = egLevel[i][2]; p.ops[i].egLevel[3] = egLevel[i][3];
            p.ops[i].rateScaling = rateScaling[i];
            p.ops[i].scaleLD = scaleLD[i]; p.ops[i].scaleRD = scaleRD[i]; p.ops[i].scaleLC = scaleLC[i]; p.ops[i].scaleRC = scaleRC[i];
            p.ops[i].lfoAMD = lfoAMD[i]; p.ops[i].lfoPMDEnable = lfoPMDEnable[i]; p.ops[i].pegEnable = pegEnable[i];
            p.ops[i].velSens = velSens[i]; p.ops[i].outLevel = outLevel[i]; p.ops[i].feedback = feedback[i]; p.ops[i].fbType = fbType[i];
            p.ops[i].freqMode = freqMode[i]; p.ops[i].freqCoarse = freqCoarse[i]; p.ops[i].freqFine = freqFine[i]; p.ops[i].freqDetune = freqDetune[i];
            p.ops[i].reserved[0] = 0; p.ops[i].reserved[1] = 0; p.ops[i].reserved[2] = 0;
        }
        return p;
    }

    void processCC(int channel, uint8_t cc, uint8_t val) {
        switch (cc) {
            case 0: ctl_.wantBankMSB = val & 0x7F; break;
            case 32: ctl_.wantBankLSB = val & 0x7F; break;
            case 1: ctl_.modWheel = val & 0x7F; ctl_.modWheelFactor = val * MIDI_NORM; break;
            case 5: patch_.common.portaTime = val; ctl_.portaTimeS = 0.06f + (val - 64) * MIDI_NORM  * 0.059f * 2.0f; break;
            case 7: ctl_.mainVolume = val & 0x7F; ctl_.mainVolumeFactor = val * MIDI_NORM; calcOutputGain(); break;
            case 11: ctl_.expression = val & 0x7F; ctl_.expressionFactor = val * MIDI_NORM; calcOutputGain(); break;
            case 64:
                ctl_.sustain = val > 63;
                if (!ctl_.sustain) {
                    for (int i = 0; i < MAX_VOICES; ++i) {
                        RDX_Voice& v = voices_[i];
                        if (!v.isHeld() && v.isSustained()) { v.setSustained(false); v.noteOff(); }
                    }
                }
                break;
            case 65: ctl_.portamento = val>63 ? true : false ; break;
            case 80:
                patch_.common.algorithm = val * 12 / 128; break;
            case 85: patch_.ops[0].outLevel = val; break;
            case 86: patch_.ops[0].feedback = val; break;
            case 87: patch_.ops[0].fbType = val; break;
            case 88: patch_.ops[0].freqMode = val; break;
            case 89: patch_.ops[0].freqCoarse = val; break;
            case 90: patch_.ops[0].freqFine = val; break;
            case 102: patch_.ops[1].outLevel = val; break;
            case 103: patch_.ops[1].feedback = val; break;
            case 104: patch_.ops[1].fbType = val; break;
            case 105: patch_.ops[1].freqMode = val; break;
            case 106: patch_.ops[1].freqCoarse = val; break;
            case 107: patch_.ops[1].freqFine = val; break;
            case 108: patch_.ops[2].outLevel = val; break;
            case 109: patch_.ops[2].feedback = val; break;
            case 110: patch_.ops[2].fbType = val; break;
            case 111: patch_.ops[2].freqMode = val; break;
            case 112: patch_.ops[2].freqCoarse = val; break;
            case 113: patch_.ops[2].freqFine = val; break;
            case 114: patch_.ops[3].outLevel = val; break;
            case 115: patch_.ops[3].feedback = val; break;
            case 116: patch_.ops[3].fbType = val; break;
            case 117: patch_.ops[3].freqMode = val; break;
            case 118: patch_.ops[3].freqCoarse = val; break;
            case 119: patch_.ops[3].freqFine = val; break;
            case 120: voiceAlloc_.allSoundOff(voices_, MAX_VOICES);
            case 123: voiceAlloc_.allNotesOff(voices_, MAX_VOICES);
        }
    }

    void updatePB(int channel, int val) {
        ctl_.pitchbend = val;
        float pbNorm = val / 8192.0f;
        ctl_.pitchbendSemitones = pbNorm * ((float)(patch_.common.pbRange - 64));
    }

    // Master Tune (SYSTEM SysEx parameter, not a MIDI CC): additive semitone
    // offset applied uniformly to all operators alongside pitch bend.
    inline void setMasterTune(float semitones) { ctl_.tuningSemitones = semitones; }

    void calcOutputGain() {
        if (!RDX_State::isInitialized()) { return; }
        polyMixCoeff_ = 0.8f / sqrtf((float)MAX_VOICES);
        int algo = state_.workingPatch.common.algorithm;
        switch (algo) {
            case 5: case 6: case 7: algoMixCoeff_ = ONE_DIV_SQRT2  ; break;
            case 8: case 9: case 10: algoMixCoeff_ = ONE_DIV_SQRT3  ; break;
            case 11: algoMixCoeff_ = 0.5f  ; break;
            default: algoMixCoeff_ = 1.0f  ; break;
        }
        outputGain_ = algoMixCoeff_ * ctl_.mainVolumeFactor * ctl_.expressionFactor * polyMixCoeff_ ;
    }
    RDX_Voice& getVoice(int idx)  {return voices_[idx];}

private:
    RDX_Voice           voices_[MAX_VOICES];
    RDX_VoiceAllocator  voiceAlloc_;
    SynthState&         state_  = RDX_State::getState();
    RDX_Controls&       ctl_    = RDX_State::getState().controls;
    RDX_Patch&          patch_  = RDX_State::getState().workingPatch;
    float algoMixCoeff_ = 1.0f;
    float polyMixCoeff_ = 1.0f;
    float outputGain_ = 1.0f;
    int voiceUpdateIdx_ = 0;
};

// v2 bridge -- descriptor-driven RdNewEngine instead of the MAME emulation;
// sustain handling lives here now (the firmware that used to do it is gone).

#pragma once

#include <cstdint>
#include "rd_engine/rd_new_engine.h"
#include "rd_engine/rd_packs_data.h"
#include "rd_effects.h"

class RD_Synth_Bridge
{
public:
    void init();
    void fill_buffer_i32(int32_t* out, int length);

    inline void noteOn(uint8_t note, uint8_t vel)
    {
        noteOnCount_++;
        if (!engineReady_) return;
        engine_.noteOn(note, vel);
        deferredOff_[note >> 5] &= ~(1u << (note & 31));
    }

    inline void noteOff(uint8_t note)
    {
        if (!engineReady_) return;
        if (pedal_)
        {
            deferredOff_[note >> 5] |= 1u << (note & 31);
        }
        else
        {
            engine_.noteOff(note);
        }
    }

    inline void sustain(uint8_t value)
    {
        if (value >= 64)
        {
            pedal_ = true;
        }
        else
        {
            pedal_ = false;
            if (engineReady_)
            {
                for (int w = 0; w < 4; ++w)
                {
                    uint32_t mask = deferredOff_[w];
                    while (mask)
                    {
                        uint8_t bit = static_cast<uint8_t>(__builtin_ctz(mask));
                        mask &= mask - 1u;
                        engine_.noteOff(static_cast<uint8_t>((w << 5) | bit));
                    }
                    deferredOff_[w] = 0;
                }
            }
            else
            {
                deferredOff_[0] = deferredOff_[1] = deferredOff_[2] = deferredOff_[3] = 0;
            }
        }
    }

    inline void allNotesOff()
    {
        if (!engineReady_) return;
        pedal_ = false;
        deferredOff_[0] = deferredOff_[1] = deferredOff_[2] = deferredOff_[3] = 0;
        engine_.allNotesOff();
    }

    int activeVoices() const { return const_cast<RdNewEngine&>(engine_).activeVoices(); }

    uint32_t noteOnCount() const { return noteOnCount_; }
    void setInstrument(uint8_t id);
    uint8_t instrument() const { return instrument_; }
    void instrumentName(char* out, uint32_t maxLen);
    uint32_t currentSampleRate() const;
    bool consumeSampleRateChanged();
    void setFxParam(uint8_t id, uint8_t val255);

    void pitchBend(uint16_t bend14);  // MIDI 0..16383, center 8192; MK-80 bender depth: +-2 semitones
    void setMasterTune(int cents);    // -50..+50 cents
    int masterTuneCents() const { return masterTuneCents_; }

    // Voice-count governor: 0..3 = fixed 8/16/24/32, 4 = Auto (load-adaptive)
    void setVoiceMode(uint8_t mode);
    uint8_t voiceMode() const { return voiceMode_; }
    uint8_t voiceLimit() const { return effectiveLimit_; }
    void voiceGovernorEmergency();  // main loop: underrun delta -> force cut in Auto

    float cpuLoadPercent() const { return cpuLoadPercent_; }
    float cpuLoadPeakPercent() const { return cpuLoadPeakPercent_; }
    RdNewEngine* engineForWorker() { return &engine_; }  // dual-core wiring

    float chipLoadPercent() const { return 0; }   // diagnostics compatibility
    float mcuLoadPercent() const { return 0; }

private:
    void setInstrumentInternal(uint8_t id);

    static constexpr uint8_t kVoiceModeAuto = 4;
    static constexpr uint8_t kAutoFloor = 6;
    static const uint8_t s_voiceModeTable[4]; // {8,16,24,32}

    uint8_t autoBaseLimit() const;         // rate-dependent proven cap
    void applyVoiceMode(bool resetAuto = false);
    void governorTick(float load, int length);

    RdNewEngine engine_;
    RD_VintageFX fx_;
    bool engineReady_ = false;
    bool pedal_ = false;
    uint32_t deferredOff_[4] = {0, 0, 0, 0};
    uint8_t instrument_ = 0;
    bool sampleRateChanged_ = false;
    volatile uint32_t noteOnCount_ = 0;
    float cpuLoadPercent_ = 0;
    float cpuLoadPeakPercent_ = 0;

    int8_t masterTuneCents_ = 0;           // -50..+50
    uint8_t voiceMode_ = kVoiceModeAuto;   // current mode
    uint8_t autoLimit_ = 16;               // governor-controlled limit (Auto mode)
    uint8_t effectiveLimit_ = 16;          // last limit pushed to engine
    int32_t autoHoldSamples_ = 0;          // recovery hold countdown in samples
};

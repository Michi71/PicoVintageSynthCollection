

#ifndef YC_SYNTH_BRIDGE_H
#define YC_SYNTH_BRIDGE_H

#include <cstdint>
#include "yc_engine/yc_engine.h"
#include "yc_engine/yc_reverb.h"

// YC_Synth_Bridge: Wraps the yc_engine organ engine for the Pico audio task.
// NOTE: noteOn()/noteOff()/fill_buffer() are audio-rate/mutating and must only be
// called from Core 0 (the audio DMA IRQ context). Core 1 code may safely call state()
// for READ-ONLY access; writing panel fields from Core 1 must go through
// IPC_CMD_YC_PANEL_UPDATE (ipc_send_yc_panel_update), see YC_Controller and
// ipc_apply() in main.cpp.
class YC_Synth_Bridge {
public:
    void init();

    // Main audio entry point. buffer is interleaved stereo (L,R,L,R...), length is number of frames.
    void fill_buffer(float* buffer, int length);

    inline void noteOn(uint8_t note, uint8_t velocity) {
        yc_engine_note_on(state_, pstate_, note, velocity);
    }
    inline void noteOff(uint8_t note) {
        yc_engine_note_off(state_, note);
    }
    inline void allNotesOff() {
        yc_engine_all_notes_off(state_);
    }
    inline void setParam(uint8_t param_id, uint16_t value) {
        yc_engine_set_param(state_, param_id, value);
    }
    inline void setSustain(bool held) {
        state_.sustain_held = held;
        if (!held) { yc_engine_release_sustained(state_); }
    }
    inline void setRotaryTarget(uint8_t speed) {
        state_.rotary_speed = speed;
    }
    inline yc_engine_state_t& state() { return state_; }

    // CPU load of the most recently rendered audio block, and the peak value
    // observed since boot, as a percentage of the real-time budget for that
    // block. Written by fill_buffer() on Core 0; Core 1 may read these
    // directly for on-screen display (same convention as DX's cpuLoadPercent()).
    inline float cpuLoadPercent() const { return cpuLoadPercent_; }
    inline float cpuLoadPeakPercent() const { return cpuLoadPeakPercent_; }

private:
    static constexpr int kChunkLen = 64;

    yc_engine_state_t    state_;
    yc_rotary_state_t    rotaryState_;
    yc_vibrato_state_t   vibratoState_;
    yc_percussion_state_t pstate_;
    yc_reverb_state_t    reverbState_;

    float scratchL_[kChunkLen];
    float scratchR_[kChunkLen];

    float cpuLoadPercent_     = 0.0f;
    float cpuLoadPeakPercent_ = 0.0f;
    uint32_t overloadBlocks_  = 0;
};

#endif // YC_SYNTH_BRIDGE_H


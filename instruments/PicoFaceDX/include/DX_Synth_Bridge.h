// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef DX_SYNTH_BRIDGE_H
#define DX_SYNTH_BRIDGE_H

#include "dx_engine/RDX_Synth.h"
#include "dx_engine/DX_FXHost.h"
#include <cstdint>
#include <cmath>

// DX_Synth_Bridge: Wraps the RDX_Synth FM engine for the picoface audio path.
// NOTE: noteOn()/noteOff()/fill_buffer_i32() are audio-rate/mutating and belong
// to the producer, i.e. render(). The UI may read patch() and the load getters
// directly, but writing patch fields from the UI goes through the ring
// (ipc_send_dx_param), see DX_Controller and applyIpc() in DX_Instrument.cpp.
// Everything runs on core0; the split is producer vs. control side, not core vs.
// core - which is why a UI read never sees a half-written block boundary.

class DX_Synth_Bridge {
public:
    void init();

    // Main audio entry point. buffer is interleaved stereo (L,R,L,R...), length is number of frames.
    void fill_buffer_i32(int32_t* out, int length);

    // Trivial forwards kept inline
    inline void noteOn(uint8_t note, uint8_t velocity) {
        synth_.noteOn(note, velocity);
    }

    inline void noteOff(uint8_t note) {
        synth_.noteOff(note);
    }

    inline void processCC(uint8_t cc, uint8_t val) {
        synth_.processCC(0, cc, val);
    }

    inline void updatePB(int bend) {
        synth_.updatePB(0, bend);
    }

    inline void setMasterTune(float semitones) {
        synth_.setMasterTune(semitones);
    }

    // Master volume, 0..100 %. Device-level output attenuator applied AFTER
    // the soft-clip stage in fill_buffer_i32(), so lowering it only changes
    // the level -- the patch keeps the exact saturation character it has at
    // 100 %. Not part of RDX_Patch, hence never stored in a preset or touched
    // by a SysEx voice dump. Core 0 only (called from ipc_apply).
    //
    // snap=true bypasses the slew and applies the gain instantly. Only valid
    // before the first block is rendered (boot restore): otherwise a jump in
    // gain is exactly the click the slew exists to avoid. It matters at boot
    // because the slew starts from unity -- without the snap, a device with a
    // stored volume of 0 would still play the first ~50 ms at full level.
    inline void setMasterVolume(uint8_t percent, bool snap = false) {
        if (percent > 100) percent = 100;
        masterVolPercent_ = percent;
        // Square-law taper: perceptually closer to an audio-taper pot than a
        // linear one, and exactly 1.0 at 100 %.
        const float n = (float)percent * 0.01f;
        masterVolTarget_ = n * n;
        if (snap) masterVolCur_ = masterVolTarget_;
    }
    inline uint8_t masterVolume() const { return masterVolPercent_; }

    inline RDX_Patch& patch() {
        return synth_.currentPatch();
    }

    // CPU load of the most recently rendered audio block, and the peak value
    // observed since boot, as a percentage of the real-time budget for that
    // block (100% = fill_buffer() takes exactly as long as the audio itself
    // lasts). Written by fill_buffer() on Core 0; Core 1 may read these
    // directly for on-screen display (same convention as patch()).
    //
    // Since the producer moved out of the DMA IRQ into the Core 0 main loop,
    // this is WALL-CLOCK time, so it includes any interrupt that preempted the
    // render (DMA, USB). The peak in particular now shows the occasional
    // preemption spike rather than pure render cost -- which is the number
    // that actually matters, because it is what the buffer lead has to absorb.
    inline float cpuLoadPercent() const { return cpuLoadPercent_; }
    inline float cpuLoadPeakPercent() const { return cpuLoadPeakPercent_; }

private:
    RDX_Synth synth_;
    DX_FXHost fxHost_;
    // Fixed scratch buffers to avoid heap allocation in the audio path
    // Soft-clip a normalized sample to (-1, 1): transparent below 0.9,
    // smoothly saturates toward +/-1.0 above (moved here from main.cpp;
    // fused into fill_buffer_i32 to avoid a second output pass).
    static inline float softClipSample(float x) {
        constexpr float kSoftClipThreshold = 0.9f;
        constexpr float kSoftClipRange = 1.0f - kSoftClipThreshold;
        float ax = fabsf(x);
        if (ax <= kSoftClipThreshold) return x;
        float excess = ax - kSoftClipThreshold;
        float y = kSoftClipThreshold + kSoftClipRange * (excess / (excess + kSoftClipRange));
        return (x < 0.0f) ? -y : y;
    }

    float scratchL_[DMA_BUFFER_LEN];
    float scratchR_[DMA_BUFFER_LEN];
    float cpuLoadPercent_ = 0.0f;
    float cpuLoadPeakPercent_ = 0.0f;
    // Master volume: target is set from the UI thread via IPC, cur_ is slewed
    // towards it per sample so an encoder sweep does not produce zipper noise.
    // Boots at unity so a device without a stored setting sounds unchanged.
    uint8_t masterVolPercent_ = 100;
    float masterVolTarget_ = 1.0f;
    float masterVolCur_ = 1.0f;
    // One-pole coefficient, ~11 ms time constant at 44.1 kHz: far above the
    // ~1 ms below which gain steps become audible as zipper noise, and still
    // fast enough that an encoder sweep tracks without feeling laggy.
    static constexpr float kMasterVolSlew = 0.002f;
};

#endif // DX_SYNTH_BRIDGE_H

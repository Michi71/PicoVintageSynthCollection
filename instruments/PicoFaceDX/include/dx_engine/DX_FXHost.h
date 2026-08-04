// include/dx_engine/DX_FXHost.h
//
// 2-slot effect router for the reface DX effects section (Thru/Distortion/
// Touch Wah/Chorus/Flanger/Phaser/Delay/Reverb), each slot independently
// selectable. Adapted from the ESP32 reference project's FXHost (RDX_FX.h):
// that version dynamically probed heap_caps/PSRAM for scratch buffers and
// throttled polyphony based on measured per-effect CPU time; neither applies
// here -- this project already has a fixed MAX_VOICES budget, and RP2350 has
// no PSRAM tier in this build, so each slot gets one fixed static scratch
// buffer sized for the largest single effect that ever needs it (FxReverb,
// which needs 24276 floats; rounded up to 24576 for margin).
#pragma once
#include <cstdint>
#include <cstring>
#include "RDX_Types.h"
#include "RDX_State.h"
#include "ram_hot.h"
#include "fx_base.h"
#include "fx_distortion.h"
#include "fx_touch_wah.h"
#include "fx_chorus.h"
#include "fx_flanger.h"
#include "fx_phaser.h"
#include "fx_delay.h"
#include "fx_reverb.h"

#define FX_SLOTS 2
#define FX_SCRATCH_FLOATS 24576  // FxReverb needs 24276 floats (2ch x (4 combs+2 allpasses)); rounded up for margin

// Effect type enum -- matches the real reface DX Common-block Effect Type
// byte values (0-7) exactly, confirmed against the official Yamaha Data List.
enum FX_ID : uint8_t {
    FX_THRU = 0,
    FX_DISTORTION,
    FX_TOUCHWAH,
    FX_CHORUS,
    FX_FLANGER,
    FX_PHASER,
    FX_DELAY,
    FX_REVERB,
    FX_COUNT
};

// Short display names for the front-panel FX1/FX2 pages, indexed by FX_ID.
static const char* const FX_NAMES[FX_COUNT] = {
    "Thru", "Distort", "T.Wah", "Chorus", "Flanger", "Phaser", "Delay", "Reverb"
};

// Label for the front-panel-editable Param1 value, indexed by FX_ID (confirmed
// against each fx_*.h's processBlock()). FX_THRU has no parameters, so index 0
// is unused and never displayed.
static const char* const FX_PARAM1_LABELS[FX_COUNT] = {
    "", "Drive", "Sens", "Depth", "Depth", "Depth", "Feedback", "Depth"
};

class DX_FXHost {
public:
    void init(float sampleRate) {
        sampleRate_ = sampleRate;
        for (int i = 0; i < FX_COUNT; ++i) {
            for (int s = 0; s < FX_SLOTS; ++s) {
                FXBase* fx = getInstance((FX_ID)i, s);
                fx->init(sampleRate, (uint8_t)s);
                fx->prepare(scratch_[s], FX_SCRATCH_FLOATS, (int)sampleRate);
            }
        }
        setSlot(0, FX_THRU);
        setSlot(1, FX_THRU);
    }

    // Processes the interleaved-planar L/R block in place, post voice-mix.
    inline void RAM_HOT(process)(float* left, float* right, uint32_t n) {
        for (int s = 0; s < FX_SLOTS; ++s) {
            uint8_t wantId = common_.effects[s][0];
            if (wantId >= FX_COUNT) wantId = FX_THRU;
            if (fx_[s] != wantId) setSlot(s, (FX_ID)wantId);
            if (slots_[s]) slots_[s]->processBlock(left, right, n);
        }
    }

private:
    static_assert(FX_SCRATCH_FLOATS >= 24276, "FX scratch buffer too small for FxReverb");

    float sampleRate_ = (float)SAMPLE_RATE;
    RDX_Common& common_ = RDX_State::getState().workingPatch.common;
    uint8_t fx_[FX_SLOTS] = {0, 0};
    FXBase* slots_[FX_SLOTS] = {nullptr, nullptr};

    float scratch_[FX_SLOTS][FX_SCRATCH_FLOATS];

    // Static pool: one instance per effect type per slot, avoids any
    // allocation when the user switches effect types live.
    FxThru       thru_[FX_SLOTS];
    FxDistortion distortion_[FX_SLOTS];
    FxTouchWah   touchwah_[FX_SLOTS];
    FxChorus     chorus_[FX_SLOTS];
    FxFlanger    flanger_[FX_SLOTS];
    FxPhaser     phaser_[FX_SLOTS];
    FxDelay      delay_[FX_SLOTS];
    FxReverb     reverb_[FX_SLOTS];

    inline void resetSlot(uint8_t slot, FXBase* incoming) {
        uint32_t n = incoming->scratchFootprintFloats();
        if (n > FX_SCRATCH_FLOATS) n = FX_SCRATCH_FLOATS;
        if (n > 0) memset(scratch_[slot], 0, n * sizeof(float));
    }

    inline void setSlot(uint8_t slot, FX_ID id) {
        if (slot >= FX_SLOTS || id >= FX_COUNT) return;
        FXBase* incoming = getInstance(id, slot);
        resetSlot(slot, incoming);
        slots_[slot] = incoming;
        slots_[slot]->enable(false);
        slots_[slot]->reset();
        slots_[slot]->enable(true);
        fx_[slot] = id;
    }

    inline FXBase* getInstance(FX_ID id, uint8_t slot) {
        switch (id) {
            case FX_THRU:       return &thru_[slot];
            case FX_DISTORTION: return &distortion_[slot];
            case FX_TOUCHWAH:   return &touchwah_[slot];
            case FX_CHORUS:     return &chorus_[slot];
            case FX_FLANGER:    return &flanger_[slot];
            case FX_PHASER:     return &phaser_[slot];
            case FX_DELAY:      return &delay_[slot];
            case FX_REVERB:     return &reverb_[slot];
            default:            return &thru_[slot];
        }
    }
};

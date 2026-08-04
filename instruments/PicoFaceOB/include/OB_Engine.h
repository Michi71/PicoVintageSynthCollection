// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// OB_Engine.h - voice allocation and parameter mapping for PicoFaceOB.
//
// This is the PicoFace replacement for OB-Xf's Motherboard.h and
// SynthEngine.h (776 + 579 lines). Those two carry the desktop plugin's
// world: 32 voices, unison, MPE, panning, the modulation matrix, patch banks,
// tempo sync and the whole parameter automation layer. None of that fits on
// three encoders and 13875 cycles per sample, so this file keeps only what the
// instrument needs - allocate a voice, set a parameter, render a block - and
// takes the parameter ranges from upstream so the sound stays OB-Xf's.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).

#ifndef OB_ENGINE_H
#define OB_ENGINE_H

#include <cstdint>

#include "obxf/Voice.h"
#include "obxf/Smoother.h"
#include "ob_params.h"
#include "ob_presets.h"

class OB_Engine
{
  public:
    void init(float sampleRate);

    // --- note events (core0, from the ring) ---------------------------------
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void allNotesOff();
    void setSustain(bool on);
    void setPitchBend(float bipolar); // -1 .. +1
    void setModWheel(float v01);

    // --- parameters ---------------------------------------------------------
    // Every parameter is normalised 0..1; the mapping to native units follows
    // OB-Xf's SynthEngine.
    void setParam(uint8_t id, float v01);
    void applyPreset(int index);
    float getParam(uint8_t id) const { return (id < OB_PARAM_COUNT) ? params_[id] : 0.f; }

    // --- audio (producer context) -------------------------------------------
    // Renders one mono block; the adapter writes the same sample to both
    // channels. (The original is stereo - per-voice pan pots to L/R - but
    // this port sums to mono.)
    void renderBlock(float* out, int frames);

    int soundingVoices();

  private:
    void applyParam(uint8_t id, float v01);
    void updateLfo1PitchDepth();
    int allocateVoice(uint8_t note);

    Voice voices_[MAX_VOICES];
    LFO lfo1_;

    Smoother cutoffSmoother_;
    Smoother resSmoother_;
    Smoother multimodeSmoother_;

    float params_[OB_PARAM_COUNT]{};
    float sampleRate_{32000.f};
    float volume_{0.24f};
    float pitchBend_{0.f};
    float modWheel_{0.f};
    bool sustain_{false};

    // Round robin start point for voice stealing, so a stolen voice is not
    // always the same one.
    uint8_t nextVoice_{0};
};

#endif // OB_ENGINE_H

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// OB_Engine.cpp - see OB_Engine.h.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).

#include "OB_Engine.h"

#include <cmath>

#include "pico.h"

void OB_Engine::init(float sampleRate)
{
    sampleRate_ = sampleRate;

    lfo1_.setSampleRate(sampleRate);
    cutoffSmoother_.setSampleRate(sampleRate);
    resSmoother_.setSampleRate(sampleRate);
    multimodeSmoother_.setSampleRate(sampleRate);

    for (int i = 0; i < MAX_VOICES; ++i)
    {
        voices_[i].voiceIndex = i;
        voices_[i].setSampleRate(sampleRate);
        // The engine is fed a single global LFO; per-voice LFO2 exists but
        // stays at rest until something drives it.
        voices_[i].lfo2BaseRate = 0.f;
    }

    for (uint8_t i = 0; i < OB_PARAM_COUNT; ++i)
    {
        params_[i] = obParams[i].def;
        applyParam(i, params_[i]);
    }
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

int OB_Engine::allocateVoice(uint8_t note)
{
    // 1. a voice already holding this note (retrigger)
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (voices_[i].midiNote == note && voices_[i].isGatedOrSustainPedaled())
        {
            return i;
        }
    }
    // 2. a silent voice, starting where the last allocation left off
    for (int n = 0; n < MAX_VOICES; ++n)
    {
        const int i = (nextVoice_ + n) % MAX_VOICES;
        if (!voices_[i].isSounding())
        {
            nextVoice_ = (uint8_t)((i + 1) % MAX_VOICES);
            return i;
        }
    }
    // 3. steal: the quietest voice that is no longer gated, else round robin
    int best = -1;
    float bestLevel = 1e9f;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (!voices_[i].isGatedOrSustainPedaled())
        {
            const float lvl = voices_[i].getVoiceAmpEnvStatus();
            if (lvl < bestLevel)
            {
                bestLevel = lvl;
                best = i;
            }
        }
    }
    if (best < 0)
    {
        best = nextVoice_;
    }
    nextVoice_ = (uint8_t)((best + 1) % MAX_VOICES);
    return best;
}

void OB_Engine::noteOn(uint8_t note, uint8_t velocity)
{
    const int i = allocateVoice(note);
    voices_[i].NoteOn((int)note, (float)velocity * (1.f / 127.f), 0);
}

void OB_Engine::noteOff(uint8_t note)
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (voices_[i].midiNote == (int)note && voices_[i].isGated())
        {
            voices_[i].NoteOff(0.f);
        }
    }
}

void OB_Engine::allNotesOff()
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        voices_[i].NoteOff(0.f);
        voices_[i].sustOff();
    }
}

void OB_Engine::setSustain(bool on)
{
    sustain_ = on;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (on)
        {
            voices_[i].sustOn();
        }
        else
        {
            voices_[i].sustOff();
        }
    }
}

void OB_Engine::setPitchBend(float bipolar) { pitchBend_ = bipolar; }

void OB_Engine::setModWheel(float v01)
{
    modWheel_ = v01;
    // The mod wheel rides on LFO 1 depth, as on the original front panel.
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        voices_[i].par.lfo1.amt1 =
            logsc(logsc(params_[OB_LFO_TO_PITCH], 0.f, 1.f, 60.f), 0.f, 60.f, 10.f) *
            (0.2f + 0.8f * modWheel_);
    }
}

int OB_Engine::soundingVoices()
{
    int n = 0;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (voices_[i].isSounding())
        {
            n++;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// Parameters - ranges taken from OB-Xf's SynthEngine so the mapping matches
// ---------------------------------------------------------------------------

#define FOR_EACH_VOICE(expr)                                                                       \
    for (int vi = 0; vi < MAX_VOICES; ++vi)                                                        \
    {                                                                                              \
        voices_[vi].expr;                                                                          \
    }

void OB_Engine::setParam(uint8_t id, float v01)
{
    if (id >= OB_PARAM_COUNT)
    {
        return;
    }
    if (v01 < 0.f)
    {
        v01 = 0.f;
    }
    if (v01 > 1.f)
    {
        v01 = 1.f;
    }
    params_[id] = v01;
    applyParam(id, v01);
}

void OB_Engine::applyParam(uint8_t id, float v)
{
    switch (id)
    {
    // --- oscillators ---
    case OB_OSC1_MIX:    FOR_EACH_VOICE(oscs.par.mix.osc1 = v); break;
    case OB_OSC2_MIX:    FOR_EACH_VOICE(oscs.par.mix.osc2 = v); break;
    case OB_OSC2_DETUNE: FOR_EACH_VOICE(oscs.par.osc.detune = logsc(v, 0.001f, 0.6f)); break;
    case OB_OSC1_SAW:    FOR_EACH_VOICE(oscs.par.osc.saw1 = v > 0.5f); break;
    case OB_OSC1_PULSE:  FOR_EACH_VOICE(oscs.par.osc.pulse1 = v > 0.5f); break;
    case OB_OSC2_SAW:    FOR_EACH_VOICE(oscs.par.osc.saw2 = v > 0.5f); break;
    case OB_OSC2_PULSE:  FOR_EACH_VOICE(oscs.par.osc.pulse2 = v > 0.5f); break;
    case OB_PULSE_WIDTH: FOR_EACH_VOICE(oscs.par.osc.pw = linsc(v, 0.f, 0.95f)); break;
    case OB_OSC_SYNC:    FOR_EACH_VOICE(oscs.par.osc.sync = v > 0.5f); break;
    case OB_CROSSMOD:    FOR_EACH_VOICE(oscs.par.osc.crossmod = v * 48.f); break;
    case OB_NOISE_MIX:   FOR_EACH_VOICE(oscs.par.mix.noise = v); break;
    case OB_RINGMOD_MIX: FOR_EACH_VOICE(oscs.par.mix.ringMod = v); break;
    // Oscillator coarse pitch, 0..48 semitones with the neutral position at
    // 24 - NOT at 0. Voice.h feeds the oscillators `midiNote - 93`, so A4
    // (note 69) only lands on getPitch(0) = 440 Hz once the 24 are added
    // back: 69 - 93 + 24 = 0. Upstream hides this in processOsc1Pitch(),
    // which maps the normalised parameter to val * 48; leaving pitch1 at its
    // declared default of 0 puts the whole instrument two octaves down.
    case OB_OSC1_PITCH:  FOR_EACH_VOICE(oscs.par.osc.pitch1 = (float)(int)(v * 48.f + 0.5f)); break;
    case OB_OSC2_PITCH:  FOR_EACH_VOICE(oscs.par.osc.pitch2 = (float)(int)(v * 48.f + 0.5f)); break;
    case OB_BRIGHTNESS:  FOR_EACH_VOICE(setBrightness(linsc(v, 4000.f, 26000.f))); break;

    // --- filter ---
    // cutoff, resonance and multimode are smoothed per sample in renderBlock()
    case OB_CUTOFF:      cutoffSmoother_.setStep(linsc(v, 0.f, 120.f)); break;
    case OB_RESONANCE:   resSmoother_.setStep(juce::jlimit(0.f, 0.991f, v)); break;
    case OB_MULTIMODE:   multimodeSmoother_.setStep(v); break;
    case OB_FOUR_POLE:   FOR_EACH_VOICE(par.filter.fourPole = v > 0.5f); break;
    case OB_FILTER_ENV_AMT: FOR_EACH_VOICE(par.filter.envAmt = linsc(v, 0.f, 140.f)); break;
    case OB_FILTER_KEYTRACK: FOR_EACH_VOICE(par.filter.keytrack = v); break;
    case OB_PUSH_2POLE:  FOR_EACH_VOICE(setFilter2PolePush(v > 0.5f ? 1.f : 0.f)); break;

    // --- envelopes ---
    case OB_FILT_ATTACK:
        FOR_EACH_VOICE(filterEnv.setAttack(logsc(v, 1.f, 60000.f, 900.f)));
        FOR_EACH_VOICE(filterEnvAttackBase = logsc(v, 1.f, 60000.f, 900.f));
        break;
    case OB_FILT_DECAY:  FOR_EACH_VOICE(filterEnv.setDecay(logsc(v, 1.f, 60000.f, 900.f))); break;
    case OB_FILT_SUSTAIN: FOR_EACH_VOICE(filterEnv.setSustain(v)); break;
    case OB_FILT_RELEASE:
        FOR_EACH_VOICE(filterEnv.setRelease(logsc(v, 1.f, 60000.f, 900.f)));
        FOR_EACH_VOICE(filterEnvReleaseBase = logsc(v, 1.f, 60000.f, 900.f));
        break;
    case OB_AMP_ATTACK:
        FOR_EACH_VOICE(ampEnv.setAttack(logsc(v, 4.f, 60000.f, 900.f)));
        FOR_EACH_VOICE(ampEnvAttackBase = logsc(v, 4.f, 60000.f, 900.f));
        break;
    case OB_AMP_DECAY:   FOR_EACH_VOICE(ampEnv.setDecay(logsc(v, 4.f, 60000.f, 900.f))); break;
    case OB_AMP_SUSTAIN: FOR_EACH_VOICE(ampEnv.setSustain(v)); break;
    case OB_AMP_RELEASE:
        FOR_EACH_VOICE(ampEnv.setRelease(logsc(v, 8.f, 60000.f, 900.f)));
        FOR_EACH_VOICE(ampEnvReleaseBase = logsc(v, 8.f, 60000.f, 900.f));
        break;

    // --- LFO 1 (global) ---
    case OB_LFO_RATE: lfo1_.setRate(logsc(v, 0.01f, 50.f, 100.f)); break;
    case OB_LFO_WAVE:
    {
        // Five positions: sine, triangle, saw, square, sample and hold. The
        // LFO blends waveform pairs, so each position is one blend at full
        // travel - upstream can mix them, three encoders cannot.
        const int w = (int)(v * 4.99f);
        lfo1_.par.wave1blend = (w == 0) ? -1.f : (w == 1 ? 1.f : 0.f);
        lfo1_.par.wave2blend = (w == 2) ? 1.f : (w == 3 ? -1.f : 0.f);
        lfo1_.par.wave3blend = (w == 4) ? -1.f : 0.f;
        break;
    }
    // amt1 and amt2 use upstream's double logsc curves, so a value taken from
    // a factory patch means the same thing here.
    case OB_LFO_TO_PITCH:
        FOR_EACH_VOICE(par.lfo1.osc1Pitch = 1.f);
        FOR_EACH_VOICE(par.lfo1.osc2Pitch = 1.f);
        FOR_EACH_VOICE(par.lfo1.amt1 = logsc(logsc(v, 0.f, 1.f, 60.f), 0.f, 60.f, 10.f) *
                                       (0.2f + 0.8f * modWheel_));
        break;
    case OB_LFO_TO_PW:
        FOR_EACH_VOICE(par.lfo1.osc1PW = 1.f);
        FOR_EACH_VOICE(par.lfo1.osc2PW = 1.f);
        FOR_EACH_VOICE(par.lfo1.amt2 = juce::jlimit(0.f, 0.7f, v * 0.7f));
        break;
    case OB_LFO_TO_CUTOFF: FOR_EACH_VOICE(par.lfo1.cutoff = v * 60.f); break;

    // --- global ---
    case OB_PORTAMENTO: FOR_EACH_VOICE(par.osc.portamento = logsc(1.f - v, 0.14f, 250.f, 150.f)); break;
    case OB_VOICE_SLOP:
        FOR_EACH_VOICE(par.slop.cutoff = linsc(v, 0.f, 0.75f));
        FOR_EACH_VOICE(par.slop.portamento = linsc(v, 0.f, 0.75f));
        FOR_EACH_VOICE(par.slop.level = linsc(v, 0.f, 0.25f));
        FOR_EACH_VOICE(setEnvTimingOffset(linsc(v, 0.f, 1.f)));
        break;
    case OB_VOLUME: volume_ = linsc(v, 0.f, 0.30f); break;

    default: break;
    }
}

void OB_Engine::applyPreset(int index)
{
    if (index < 0 || index >= OB_NPRESETS)
    {
        return;
    }
    for (uint8_t i = 0; i < OB_PARAM_COUNT; ++i)
    {
        setParam(i, obPresets[index].value[i]);
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

// RAM resident: the voice path runs entirely inside this function after
// inlining, and executing it from XIP flash costs cache misses the audio
// budget cannot pay. Same treatment as PicoFaceCP's cp_render_block().
void __no_inline_not_in_flash_func(OB_Engine::renderBlock)(float* out, int frames)
{
    for (int s = 0; s < frames; ++s)
    {
        lfo1_.update();
        const float lfo1Val = lfo1_.getVal();

        // Upstream smooths cutoff, resonance and filter mode per sample and
        // pushes them into the sounding voices; a jump would click.
        const float co = cutoffSmoother_.smoothStep();
        const float re = resSmoother_.smoothStep();
        const float fm = multimodeSmoother_.smoothStep();

        float mix = 0.f;

        for (int i = 0; i < MAX_VOICES; ++i)
        {
            Voice& v = voices_[i];

            if (!v.updateSoundingState())
            {
                continue;
            }

            v.par.filter.cutoff = co;
            v.filter.setResonance(re);
            v.filter.setMultimode(fm);
            v.pitchBend = pitchBend_;
            v.lfo1In = lfo1Val;

            mix += v.ProcessSample();
        }

        out[s] = mix * volume_;
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// OB_Engine.cpp - see OB_Engine.h.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).

#include "OB_Engine.h"

#include <cmath>

// Host tests build this file without the Pico SDK; ObxfPort.h (pulled in via
// OB_Engine.h) provides the no-op placement macros for that case.
#if __has_include("pico.h")
#include "pico.h"
#endif

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
        // The engine feeds a single global LFO; the per-voice LFO2 of the
        // upstream engine is removed from the render path (see Voice.h).
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
    updateLfo1PitchDepth();
}

// LFO 1 pitch depth = programmed depth plus the modulation lever. The
// programmed amount always applies in full (a patch sounds the same with the
// lever at rest), and the lever ADDS vibrato on top - up to half a semitone -
// which is how the original's modulation lever sits on the panel DEPTH.
void OB_Engine::updateLfo1PitchDepth()
{
    const float depth =
        logsc(logsc(params_[OB_LFO_TO_PITCH], 0.f, 1.f, 60.f), 0.f, 60.f, 10.f);
    const float lever = modWheel_ * 0.5f;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        voices_[i].par.lfo1.amt1 = depth + lever;
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
    case OB_PW_OFFSET:   FOR_EACH_VOICE(par.osc.pwOsc2Offset = linsc(v, 0.f, 0.95f)); break;
    case OB_OSC_SYNC:    FOR_EACH_VOICE(oscs.par.osc.sync = v > 0.5f); break;
    case OB_CROSSMOD:    FOR_EACH_VOICE(oscs.par.osc.crossmod = v * 48.f); break;
    case OB_NOISE_MIX:   FOR_EACH_VOICE(oscs.par.mix.noise = v); break;
    case OB_NOISE_COLOR: FOR_EACH_VOICE(setNoiseColor(v)); break;
    case OB_RINGMOD_MIX: FOR_EACH_VOICE(oscs.par.mix.ringMod = v); break;
    // Oscillator coarse pitch, 0..48 semitones with the neutral position at
    // 24 - NOT at 0. Voice.h feeds the oscillators `midiNote - 93`, so A4
    // (note 69) only lands on getPitch(0) = 440 Hz once the 24 are added
    // back: 69 - 93 + 24 = 0. Upstream hides this in processOsc1Pitch(),
    // which maps the normalised parameter to val * 48; leaving pitch1 at its
    // declared default of 0 puts the whole instrument two octaves down.
    case OB_OSC1_PITCH:  FOR_EACH_VOICE(oscs.par.osc.pitch1 = (float)(int)(v * 48.f + 0.5f)); break;
    case OB_OSC2_PITCH:  FOR_EACH_VOICE(oscs.par.osc.pitch2 = (float)(int)(v * 48.f + 0.5f)); break;
    case OB_BRIGHTNESS:  FOR_EACH_VOICE(setBrightness(linsc(v, 7000.f, 26000.f))); break;

    // --- filter ---
    // cutoff, resonance and multimode are smoothed per sample in renderBlock()
    case OB_CUTOFF:      cutoffSmoother_.setStep(linsc(v, 0.f, 120.f)); break;
    // Upstream's reverse-log law: most of the knob works the region below
    // self-oscillation, the top squeezes onto 0.991. A linear map here made
    // the factory patches lose their resonance character.
    case OB_RESONANCE:   resSmoother_.setStep(0.991f - logsc(1.f - v, 0.f, 0.991f, 40.f)); break;
    case OB_MULTIMODE:   multimodeSmoother_.setStep(v); break;
    case OB_FOUR_POLE:   FOR_EACH_VOICE(par.filter.fourPole = v > 0.5f); break;
    // Bipolar: upstream splits this into an amount and an invert switch; a
    // signed amount is the same maths (envAmt multiplies the envelope) and
    // saves the extra parameter. 0.5 is neutral.
    case OB_FILTER_ENV_AMT: FOR_EACH_VOICE(par.filter.envAmt = linsc(v, -140.f, 140.f)); break;
    case OB_FILTER_KEYTRACK: FOR_EACH_VOICE(par.filter.keytrack = v); break;
    case OB_PUSH_2POLE:  FOR_EACH_VOICE(setFilter2PolePush(v > 0.5f ? 1.f : 0.f)); break;
    case OB_BP_BLEND:    FOR_EACH_VOICE(filter.par.bpBlend2Pole = v > 0.5f); break;
    // The Xpander pole-mix table has been compiled into Filter.h all along;
    // these two switches only steer the existing branch.
    case OB_XPANDER:     FOR_EACH_VOICE(filter.par.xpander4Pole = v > 0.5f); break;
    case OB_XPANDER_MODE:
        FOR_EACH_VOICE(filter.par.xpanderMode =
                           (uint8_t)juce::roundToInt(v * (NUM_XPANDER_MODES - 1)));
        break;

    // --- envelopes ---
    // The upstream *Base copies (filterEnvAttackBase etc.) are not written:
    // they only feed the modulation matrix, which is not ported.
    case OB_FILT_ATTACK: FOR_EACH_VOICE(filterEnv.setAttack(logsc(v, 1.f, 60000.f, 900.f))); break;
    case OB_FILT_DECAY:  FOR_EACH_VOICE(filterEnv.setDecay(logsc(v, 1.f, 60000.f, 900.f))); break;
    case OB_FILT_SUSTAIN: FOR_EACH_VOICE(filterEnv.setSustain(v)); break;
    case OB_FILT_RELEASE: FOR_EACH_VOICE(filterEnv.setRelease(logsc(v, 1.f, 60000.f, 900.f))); break;
    // The filter envelope's pitch and PW targets. Bipolar amounts fold
    // upstream's invert switches into the sign; the fields have been part of
    // every ProcessSample() since the port, so this costs nothing per sample.
    case OB_ENV_TO_PITCH:
        FOR_EACH_VOICE(par.osc.envPitchAmt = fabsf(2.f * v - 1.f) * 40.f);
        FOR_EACH_VOICE(oscs.par.mod.envToPitchInvert = v < 0.5f);
        break;
    case OB_ENV_PITCH_BOTH: FOR_EACH_VOICE(par.osc.envPitchBothOscs = v > 0.5f); break;
    case OB_ENV_TO_PW:
        FOR_EACH_VOICE(par.osc.envPWAmt = fabsf(2.f * v - 1.f) * 1.055555555555555f);
        FOR_EACH_VOICE(oscs.par.mod.envToPWInvert = v < 0.5f);
        break;
    case OB_ENV_PW_BOTH: FOR_EACH_VOICE(par.osc.envPWBothOscs = v > 0.5f); break;
    case OB_AMP_ATTACK:  FOR_EACH_VOICE(ampEnv.setAttack(logsc(v, 4.f, 60000.f, 900.f))); break;
    case OB_AMP_DECAY:   FOR_EACH_VOICE(ampEnv.setDecay(logsc(v, 4.f, 60000.f, 900.f))); break;
    case OB_AMP_SUSTAIN: FOR_EACH_VOICE(ampEnv.setSustain(v)); break;
    case OB_AMP_RELEASE: FOR_EACH_VOICE(ampEnv.setRelease(logsc(v, 8.f, 60000.f, 900.f))); break;

    // --- LFO 1 (global) ---
    // Upstream's curve, so a rate from a factory patch means the same Hz.
    // (The original hardware spans 0.1..20 Hz - service manual, trimmer T5 -
    // which lives comfortably inside this range.)
    case OB_LFO_RATE: lfo1_.setRate(logsc(v, 0.f, 250.f, 3775.f)); break;
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
    // The depth curves are upstream's (amt1 the double logsc, amt2 linear
    // 0..0.7), so a value taken from a factory patch means the same thing
    // here. Where upstream shares ONE depth (amt1) between the pitch and
    // cutoff targets and routes them on/off, this port gives each target its
    // own depth knob; the cutoff term in Voice.h drops the amt1 factor for
    // that.
    case OB_LFO_TO_PITCH:
        FOR_EACH_VOICE(par.lfo1.osc1Pitch = 1.f);
        FOR_EACH_VOICE(par.lfo1.osc2Pitch = 1.f);
        updateLfo1PitchDepth();
        break;
    case OB_LFO_TO_PW:
        FOR_EACH_VOICE(par.lfo1.osc1PW = 1.f);
        FOR_EACH_VOICE(par.lfo1.osc2PW = 1.f);
        FOR_EACH_VOICE(par.lfo1.amt2 = linsc(v, 0.f, 0.7f));
        break;
    case OB_LFO_TO_CUTOFF:
        FOR_EACH_VOICE(par.lfo1.cutoff = logsc(logsc(v, 0.f, 1.f, 60.f), 0.f, 60.f, 10.f));
        break;
    // Tremolo. Like the cutoff route this has its own depth here; the
    // volume term in Voice.h dropped the shared amt2 factor for it.
    case OB_LFO_TO_VOL: FOR_EACH_VOICE(par.lfo1.volume = v); break;

    // --- global ---
    case OB_PORTAMENTO: FOR_EACH_VOICE(par.osc.portamento = logsc(1.f - v, 0.14f, 250.f, 150.f)); break;
    // Upstream's slop ranges (filter 18 semitones, level 0.67), plus the
    // per-oscillator tuning scatter: unisonDetune * tuningSlop sits in the
    // pitch term anyway, so wiring it costs nothing per sample - and the
    // voice-to-voice drift is most of the "old Oberheim" character. The
    // service manual even specs a 2:1 portamento spread between voices.
    case OB_VOICE_SLOP:
        FOR_EACH_VOICE(par.slop.cutoff = linsc(v, 0.f, 18.f));
        FOR_EACH_VOICE(par.slop.portamento = linsc(v, 0.f, 0.75f));
        FOR_EACH_VOICE(par.slop.level = linsc(v, 0.f, 0.67f));
        FOR_EACH_VOICE(oscs.par.pitch.unisonDetune = logsc(v, 0.001f, 1.f));
        FOR_EACH_VOICE(setEnvTimingOffset(linsc(v, 0.f, 1.f)));
        break;
    case OB_VOLUME: volume_ = linsc(v, 0.f, 0.30f); break;
    // Service manual bend calibration: Narrow = 0.167 V = 2 semitones,
    // Broad = 1.000 V = one octave. Symmetrical, like the lever.
    case OB_BEND_RANGE:
        FOR_EACH_VOICE(par.extmod.pbUp = (v > 0.5f ? 12.f : 2.f));
        FOR_EACH_VOICE(par.extmod.pbDown = (v > 0.5f ? 12.f : 2.f));
        break;

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
        // The bend range is a performance switch on the bend assembly, not
        // part of a program - on the original the programmer does not store
        // it, so a preset does not move it here either.
        if (i == OB_BEND_RANGE)
        {
            continue;
        }
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

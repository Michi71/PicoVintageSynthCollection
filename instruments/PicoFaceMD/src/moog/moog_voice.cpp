// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_voice.cpp -- keyboard, mixer, modifiers, output stage
*/

#include "moog/moog_voice.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Setup                                                                     */
/* ------------------------------------------------------------------------ */
void MoogVoice::init(float sampleRate)
{
    sr_      = sampleRate;
    osSr_    = sampleRate * (float) MOOG_OVERSAMPLE;
    invOsSr_ = 1.0f / osSr_;

    /* Seeds chosen only to be far apart. Three oscillators sharing a
     * generator would drift in lockstep, which is the one thing drift must
     * not do -- the whole point is that they pull against each other. */
    osc1_.init(osSr_, 0x9E3779B9u, false);
    osc2_.init(osSr_, 0x85EBCA6Bu, false);
    osc3_.init(osSr_, 0xC2B2AE35u, true);

    noise_.seed(0x1F123BB5u);
    hiss_.seed(0x27D4EB2Fu);

    ladder_.init(osSr_);
    envFilt_.init(sr_);
    envAmp_.init(sr_);

    static const uint32_t kDriftSeeds[4] = {
        0x2545F491u, 0x8A9B1C3Du, 0x53A7F0E1u, 0xB5297A4Du
    };
    for (int i = 0; i < 3; ++i)
        driftOsc_[i].init(kDriftSeeds[i], sr_);
    driftFilt_.init(kDriftSeeds[3], sr_);

    /* A fixed tuning error per oscillator, drawn once. Two oscillators set to
     * the same interval on a real instrument are never exactly in tune, and
     * without this the unison settings that a Model D is famous for would
     * cancel into something thin and static. */
    MoogNoise err;
    err.seed(0x6C078965u);
    for (int i = 0; i < 3; ++i)
        tuneError_[i] = err.white() * (MOOG_TUNE_ERROR_CT / 100.0f);

    /* 6th order Butterworth: three biquads whose Q values are the poles of a
     * Butterworth cascade. */
    dec_[0].set(MOOG_DECIMATE_HZ, 0.51764f, osSr_);
    dec_[1].set(MOOG_DECIMATE_HZ, 0.70711f, osSr_);
    dec_[2].set(MOOG_DECIMATE_HZ, 1.93185f, osSr_);

    dcOut_.setCutoff(MOOG_DC_BLOCK_HZ, sr_);
    toneLp_.setCutoff(MOOG_TONE_MAX_HZ, sr_);
    a440Inc_ = MOOG_A440_HZ / sr_;

    reset();
    applyAll();
}

void MoogVoice::setSampleRate(float sampleRate)
{
    init(sampleRate);
}

void MoogVoice::reset()
{
    heldCount_  = 0;
    pedal_      = false;
    gate_       = false;
    curNote_    = -1;
    everPlayed_ = false;
    curPitch_   = (float) MOOG_CENTER_NOTE;
    targetPitch_= (float) MOOG_CENTER_NOTE;
    feedback_   = 0.0f;
    lastMod_    = 0.0f;
    bendSemi_   = 0.0f;
    bendNorm_   = 0.0f;

    memset(sustained_, 0, sizeof(sustained_));

    ladder_.reset();
    envFilt_.reset();
    envAmp_.reset();
    dcOut_.reset();
    toneLp_.reset();
    for (int i = 0; i < 3; ++i) dec_[i].reset();
}

/* ------------------------------------------------------------------------ */
/* Panel                                                                     */
/* ------------------------------------------------------------------------ */

/* Range switch to an octave offset relative to 8'. LO sits far below 32' --
 * with the oscillator 3 keyboard switch off, that position is the only LFO
 * the instrument has. */
float MoogVoice::oscOctaves(int rangeParam) const
{
    static const float kOct[MOOG_RANGE_COUNT] = {
        MOOG_LO_OCTAVES,  /* LO  */
        -2.0f,            /* 32' */
        -1.0f,            /* 16' */
         0.0f,            /*  8' */
         1.0f,            /*  4' */
         2.0f             /*  2' */
    };
    return kOct[moogParamStep(p_[rangeParam], MOOG_RANGE_COUNT)];
}

void MoogVoice::setParameter(int id, float value)
{
    if (id < 0 || id >= MOOG_PARAM_COUNT) return;
    p_[id] = moogClamp(value, 0.0f, 1.0f);
    applyParameter(id);
}

void MoogVoice::applyAll()
{
    for (int i = 0; i < MOOG_PARAM_COUNT; ++i)
        applyParameter(i);
}

void MoogVoice::applyParameter(int id)
{
    const float v = p_[id];

    switch (id) {

    /* --- Controllers ---------------------------------------------------- */
    case MOOG_TUNE:
        tuneSemi_ = (v * 2.0f - 1.0f) * MOOG_TUNE_SEMITONES;
        break;

    case MOOG_GLIDE: {
        /* Squared, so the useful short times are not crammed into the first
         * degree of travel. ln(10) time constants: the pitch covers 90 % of
         * the interval in the nominal time. */
        const float t = v * v * MOOG_GLIDE_MAX_S;
        glideCoef_ = (t < 1.0e-4f) ? 1.0f
                                   : 1.0f - expf(-2.3026f / (t * sr_));
        break;
    }

    case MOOG_GLIDE_ON:
        glideOn_ = moogParamOn(v);
        break;

    case MOOG_MOD_MIX:
        modMix_ = v;                       /* 0 = oscillator 3, 1 = noise */
        break;

    case MOOG_MOD_WHEEL:
        /*
         * Squared, and this is a departure from the instrument.
         *
         * The wheel reaches 7.2 semitones of pitch modulation at the top,
         * which is what it is for -- sirens and dive bombs are a Model D
         * sound. But linearly, a musical vibrato of 20 cents then lives at
         * wheel position 0.03, and everything from there upwards is already
         * too much: the first perceptible movement and "unusable" are three
         * per cent of the travel apart. Squaring puts 20 cents at 0.17 and
         * keeps the full depth at the top, so the useful range is the first
         * fifth of the wheel rather than the first thirtieth.
         *
         * The panel still reads 0..10 linearly, as a volume control does
         * while its pot is tapered.
         */
        modWheel_ = v * v;
        break;

    case MOOG_OSC_MOD:
        oscModOn_ = moogParamOn(v);
        break;

    case MOOG_OSC3_CTRL:
        osc3Kbd_ = moogParamOn(v);
        /* The frequency control of oscillator 3 changes range with this
         * switch, so its cached value has to be worked out again. */
        applyParameter(MOOG_OSC3_FREQ);
        break;

    case MOOG_BEND_RANGE:
        bendRange_ = (float) moogParamStep(v, 13);
        bendSemi_  = bendNorm_ * bendRange_;
        break;

    /* --- Oscillator Bank ------------------------------------------------ */
    case MOOG_OSC1_RANGE: oscOct_[0] = oscOctaves(MOOG_OSC1_RANGE); break;
    case MOOG_OSC2_RANGE: oscOct_[1] = oscOctaves(MOOG_OSC2_RANGE); break;
    case MOOG_OSC3_RANGE: oscOct_[2] = oscOctaves(MOOG_OSC3_RANGE); break;

    case MOOG_OSC1_WAVE:
        osc1_.setWave(moogParamStep(v, MOOG_WAVE_COUNT));
        break;
    case MOOG_OSC2_WAVE:
        osc2_.setWave(moogParamStep(v, MOOG_WAVE_COUNT));
        break;
    case MOOG_OSC3_WAVE:
        osc3_.setWave(moogParamStep(v, MOOG_WAVE_COUNT));
        break;

    case MOOG_OSC2_FREQ:
        oscFine_[1] = (v * 2.0f - 1.0f) * MOOG_FREQ_SEMITONES;
        break;

    case MOOG_OSC3_FREQ:
        /* "You will also observe that Oscillator 3's FREQUENCY control has a
         * much wider range when switch (B) is off ... a frequency sweep of 6
         * octaves rather than one octave." */
        oscFine_[2] = (v * 2.0f - 1.0f) *
                      (osc3Kbd_ ? MOOG_FREQ_SEMITONES
                                : MOOG_FREQ_OCTAVES * 12.0f);
        break;

    /* --- Mixer ---------------------------------------------------------- */
    case MOOG_OSC1_VOL: case MOOG_OSC1_ON:
        mixGain_[0] = moogParamOn(p_[MOOG_OSC1_ON]) ? p_[MOOG_OSC1_VOL] : 0.0f;
        break;
    case MOOG_OSC2_VOL: case MOOG_OSC2_ON:
        mixGain_[1] = moogParamOn(p_[MOOG_OSC2_ON]) ? p_[MOOG_OSC2_VOL] : 0.0f;
        break;
    case MOOG_OSC3_VOL: case MOOG_OSC3_ON:
        mixGain_[2] = moogParamOn(p_[MOOG_OSC3_ON]) ? p_[MOOG_OSC3_VOL] : 0.0f;
        break;
    case MOOG_NOISE_VOL: case MOOG_NOISE_ON:
        mixGain_[3] = moogParamOn(p_[MOOG_NOISE_ON]) ? p_[MOOG_NOISE_VOL] : 0.0f;
        break;
    case MOOG_NOISE_COLOR:
        pinkNoise_ = (moogParamStep(v, 2) == 1);
        break;
    case MOOG_FEEDBACK_VOL: case MOOG_FEEDBACK_ON:
        /* Held below unity. The saturating input stage of the ladder bounds
         * the loop in any case, but there is no musical reason to sit on the
         * edge of it. */
        fbGain_ = moogParamOn(p_[MOOG_FEEDBACK_ON])
                    ? p_[MOOG_FEEDBACK_VOL] * 0.9f : 0.0f;
        break;

    /* --- Modifiers ------------------------------------------------------ */
    case MOOG_CUTOFF:
        cutoffOct_ = v * log2f(MOOG_CUTOFF_MAX_HZ / MOOG_CUTOFF_MIN_HZ);
        break;

    case MOOG_EMPHASIS:
        resonance_ = v * MOOG_RESONANCE_MAX;
        ladder_.setResonance(resonance_);
        break;

    case MOOG_CONTOUR_AMT:
        contourOct_ = v * MOOG_CONTOUR_OCTAVES;
        break;

    case MOOG_FILTER_MOD:
        filtModOn_ = moogParamOn(v);
        break;

    case MOOG_KB_CTRL_1: case MOOG_KB_CTRL_2:
        kbTrack_ = (moogParamOn(p_[MOOG_KB_CTRL_1]) ? MOOG_KBTRACK_1 : 0.0f)
                 + (moogParamOn(p_[MOOG_KB_CTRL_2]) ? MOOG_KBTRACK_2 : 0.0f);
        break;

    case MOOG_FILT_ATTACK:  envFilt_.setAttack(v);  break;
    case MOOG_FILT_DECAY:   envFilt_.setDecay(v);   break;
    case MOOG_FILT_SUSTAIN: envFilt_.setSustain(v); break;
    case MOOG_LOUD_ATTACK:  envAmp_.setAttack(v);   break;
    case MOOG_LOUD_DECAY:   envAmp_.setDecay(v);    break;
    case MOOG_LOUD_SUSTAIN: envAmp_.setSustain(v);  break;

    case MOOG_DECAY_SW:
        /* One switch, both contours -- as on the instrument. */
        envFilt_.setDecaySwitch(moogParamOn(v));
        envAmp_.setDecaySwitch(moogParamOn(v));
        break;

    /* --- Output --------------------------------------------------------- */
    case MOOG_VOLUME:
        volume_ = v * v;                   /* closer to the taper of a pot */
        break;

    case MOOG_A440:
        a440On_ = moogParamOn(v);
        break;

    /* --- Voicing -------------------------------------------------------- */
    case MOOG_DRIVE:
        inputGain_   = 0.30f + v * 0.90f;
        ladderDrive_ = 1.00f + v * 2.50f;
        ladder_.setDrive(ladderDrive_);
        break;

    case MOOG_DRIFT:
        driftDepth_ = v;
        break;

    case MOOG_TONE:
        toneLp_.setCutoff(MOOG_TONE_MIN_HZ *
                          moogExp2f(v * log2f(MOOG_TONE_MAX_HZ /
                                              MOOG_TONE_MIN_HZ)), sr_);
        break;

    case MOOG_NOTE_PRIORITY:
        priority_ = moogParamStep(v, 3);
        break;

    case MOOG_TRIGGER:
        multiTrig_ = (moogParamStep(v, 2) == 1);
        break;

    case MOOG_TRANSPOSE:
        transpose_ = moogParamStep(v, 5) - 2;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Keyboard                                                                  */
/*                                                                           */
/* One control voltage, one gate. The manual on what happens with more than   */
/* one key: "If more than one key is held down, only the lowest one has       */
/* effect." Low note priority is therefore the default; high and last are     */
/* offered because a mono synth played from a MIDI keyboard is a different    */
/* proposition from one played from its own 44 keys.                          */
/* ------------------------------------------------------------------------ */
int MoogVoice::pickNote() const
{
    if (heldCount_ <= 0) return -1;

    switch (priority_) {
        case 1: {                       /* highest */
            int best = held_[0];
            for (int i = 1; i < heldCount_; ++i)
                if (held_[i] > best) best = held_[i];
            return best;
        }
        case 2:                         /* last struck */
            return held_[heldCount_ - 1];
        default: {                      /* lowest */
            int best = held_[0];
            for (int i = 1; i < heldCount_; ++i)
                if (held_[i] < best) best = held_[i];
            return best;
        }
    }
}

void MoogVoice::removeHeld(int note)
{
    for (int i = 0; i < heldCount_; ++i) {
        if (held_[i] == note) {
            for (int j = i; j < heldCount_ - 1; ++j)
                held_[j] = held_[j + 1];
            --heldCount_;
            return;
        }
    }
}

void MoogVoice::noteOn(int note)
{
    if (note < 0 || note > 127) return;

    sustained_[note] = false;

    /* Already down: move it to the end so that last-note priority sees the
     * retrigger. */
    removeHeld(note);

    if (heldCount_ >= MOOG_MAX_HELD_KEYS) {
        /* Drop the oldest rather than the new one -- a stuck note from a lost
         * note-off should not block the keyboard for good. */
        for (int j = 0; j < heldCount_ - 1; ++j)
            held_[j] = held_[j + 1];
        --heldCount_;
    }
    held_[heldCount_++] = note;

    updateKeyboard();
}

void MoogVoice::noteOff(int note)
{
    if (note < 0 || note > 127) return;

    if (pedal_) {
        /* The pedal holds the note; the key itself is up. */
        sustained_[note] = true;
        return;
    }
    removeHeld(note);
    updateKeyboard();
}

void MoogVoice::sustainPedal(bool on)
{
    if (on == pedal_) return;
    pedal_ = on;

    if (!on) {
        for (int n = 0; n < 128; ++n) {
            if (sustained_[n]) {
                sustained_[n] = false;
                removeHeld(n);
            }
        }
        updateKeyboard();
    }
}

void MoogVoice::allNotesOff()
{
    heldCount_ = 0;
    pedal_     = false;
    memset(sustained_, 0, sizeof(sustained_));
    updateKeyboard();
}

void MoogVoice::updateKeyboard()
{
    const int sel = pickNote();

    if (sel < 0) {
        if (gate_) {
            gate_ = false;
            envFilt_.gateOff();
            envAmp_.gateOff();
        }
        curNote_ = -1;
        return;
    }

    const bool wasGate = gate_;
    if (sel == curNote_ && wasGate)
        return;

    curNote_     = sel;
    targetPitch_ = (float) (sel + transpose_ * 12);

    if (!everPlayed_) {
        /* Nothing to glide from on the very first note. */
        curPitch_   = targetPitch_;
        everPlayed_ = true;
    } else if (!glideOn_) {
        curPitch_ = targetPitch_;
    }

    gate_ = true;

    /* Single trigger: gateOn(false) does nothing while a contour is still
     * running, which is exactly the legato behaviour of the original. After
     * silence the contour is idle or releasing and triggers either way. */
    envFilt_.gateOn(multiTrig_);
    envAmp_.gateOn(multiTrig_);
}

void MoogVoice::setPitchBend(float normalised)
{
    bendNorm_ = moogClamp(normalised, -1.0f, 1.0f);
    bendSemi_ = bendNorm_ * bendRange_;
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                 */
/*                                                                           */
/* Control rates, from slowest to fastest:                                   */
/*                                                                           */
/*   per output sample (44.1 kHz)  glide, drift, both contours, the base      */
/*                                 pitches and the ladder cutoff              */
/*   per oversampled step (88.2 kHz)                                          */
/*                                 the oscillators, the noise, the mixer, the */
/*                                 ladder and the decimation chain            */
/*                                                                           */
/* Pitch modulation sits in the fast group on purpose. Running it at the      */
/* control rate would be cheaper and perfectly adequate for vibrato, but      */
/* oscillator 3 pointed at the other two at audio rate is a sound this        */
/* instrument is known for, and a fifth order 2^x costs a few multiplies.     */
/* ------------------------------------------------------------------------ */
void MoogVoice::process(float* out, int frames)
{
    const float driftSemi = driftDepth_ * (MOOG_DRIFT_CENTS / 100.0f);
    const float driftOct  = driftDepth_ * (MOOG_FILTER_DRIFT_CT / 1200.0f);
    const float twoPi     = 2.0f * (float) M_PI;

    /* The fixed per-oscillator error fades out with the Drift control along
     * with the wander. Leaving it in at zero would mean the one setting that
     * asks for a perfectly stable instrument still delivered oscillators two
     * cents apart, with nothing on the panel able to remove it. */
    const float e0 = tuneError_[0] * driftDepth_;
    const float e1 = tuneError_[1] * driftDepth_;
    const float e2 = tuneError_[2] * driftDepth_;

    for (int i = 0; i < frames; ++i)
    {
        /* --- Control ---------------------------------------------------- */
        curPitch_ += (targetPitch_ - curPitch_) * glideCoef_;

        const float d0 = driftOsc_[0].process() * driftSemi;
        const float d1 = driftOsc_[1].process() * driftSemi;
        const float d2 = driftOsc_[2].process() * driftSemi;
        const float df = driftFilt_.process() * driftOct;

        const float ef = envFilt_.process();
        const float ea = envAmp_.process();

        const float base = curPitch_ + tuneSemi_ + bendSemi_;

        const float n1 = base + oscOct_[0] * 12.0f + e0 + d0;
        const float n2 = base + oscOct_[1] * 12.0f + oscFine_[1] + e1 + d1;

        /* With the oscillator 3 keyboard switch off, the oscillator comes off
         * the keyboard entirely and free-runs from a fixed reference. That is
         * how it becomes the LFO of the instrument. */
        const float n3 = (osc3Kbd_ ? base : (float) MOOG_CENTER_NOTE + tuneSemi_)
                       + oscOct_[2] * 12.0f + oscFine_[2] + e2 + d2;

        const float inc1 = moogNoteToHz(n1) * invOsSr_;
        const float inc2 = moogNoteToHz(n2) * invOsSr_;
        const float inc3 = moogNoteToHz(n3) * invOsSr_;

        /* Cutoff: the panel setting, plus the contour, plus whatever the
         * keyboard tracking switches let through, plus the modulation mix if
         * its switch is on -- all additive in octaves, which is how the
         * control voltages add in the original. */
        float cutOct = cutoffOct_
                     + contourOct_ * ef
                     + kbTrack_ * (curPitch_ - (float) MOOG_CENTER_NOTE) * (1.0f / 12.0f)
                     + df;
        if (filtModOn_)
            cutOct += lastMod_ * MOOG_FILT_MOD_OCTAVES;

        ladder_.setCutoff(MOOG_CUTOFF_MIN_HZ * moogExp2f(cutOct));

        /* --- Audio ------------------------------------------------------ */
        float sample = 0.0f;

        for (int os = 0; os < MOOG_OVERSAMPLE; ++os)
        {
            const float pmul = oscModOn_
                ? moogExp2Fast(lastMod_ * MOOG_OSC_MOD_OCTAVES) : 1.0f;

            osc1_.setIncrement(inc1 * pmul);
            osc2_.setIncrement(inc2 * pmul);
            osc3_.setIncrement(inc3 * pmul);

            const float s1 = osc1_.process();
            const float s2 = osc2_.process();
            const float s3 = osc3_.process();

            const float w  = noise_.white();
            const float nz = pinkNoise_ ? noise_.pink(w) : w;

            /* Oscillator 3 and the noise source feed the modulation mix
             * whether or not the mixer switches let them be heard. */
            lastMod_ = moogLerp(s3, nz, modMix_) * modWheel_;

            float mix = s1 * mixGain_[0]
                      + s2 * mixGain_[1]
                      + s3 * mixGain_[2]
                      + nz * mixGain_[3]
                      + feedback_ * fbGain_;

            mix += hiss_.white() * MOOG_HISS_LEVEL;

            float f = ladder_.process(mix * inputGain_);

            /* Decimation runs at the oversampled rate; the last of the
             * MOOG_OVERSAMPLE filtered values is the output sample. */
            f = dec_[0].process(f);
            f = dec_[1].process(f);
            f = dec_[2].process(f);
            sample = f;
        }

        /* --- Amplifier and output stage --------------------------------- */
        float y = sample * ea;

        /* Tapped here, before the main volume: on the instrument the feedback
         * loop does run through the volume control, but a feedback amount
         * that changes character every time the volume moves is a trap
         * rather than a feature. */
        feedback_ = y;

        y = dcOut_.process(y);
        y = toneLp_.process(y);
        y = moogSoftClip(y * 1.10f);
        y *= volume_;

        if (a440On_) {
            a440Phase_ += a440Inc_;
            if (a440Phase_ >= 1.0f) a440Phase_ -= 1.0f;
            y += sinf(a440Phase_ * twoPi) * MOOG_A440_LEVEL;
        }

        out[i] = y;
    }
}

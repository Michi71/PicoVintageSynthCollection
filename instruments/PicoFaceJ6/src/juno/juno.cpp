// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno.cpp -- Roland Juno-60, top level
*/

#include "juno/juno.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

Juno::Juno()
{
    /*
     * The instrument settings need defaults of their own.
     *
     * They live in the second half of p_, which is zero-initialised, and
     * setProgram only writes the first half -- so without this the master
     * volume starts at zero and the whole instrument is silent until someone
     * finds the OUTPUT page. Which is exactly what happened.
     */
    for (int i = JUNO_PARAM_COUNT; i < JUNO_TOTAL_COUNT; ++i)
        p_[i] = junoInstrumentDefault(i);

    setSampleRate(samplerate_);
    setProgram(0);
}

void Juno::setSampleRate(float sampleRate)
{
    if (sampleRate < 8000.0f) sampleRate = 8000.0f;
    samplerate_ = sampleRate;

    const float osSr = sampleRate * (float) JUNO_OVERSAMPLE;

    /* Seeds only need to be far apart. The DCO does not drift, so these decide
     * nothing but the starting phase -- which is still worth varying, because
     * six voices starting in phase would sum into something much louder than
     * six voices that do not. */
    static const uint32_t kSeeds[JUNO_VOICES] = {
        0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u,
        0x27D4EB2Fu, 0x165667B1u, 0xD3A2646Cu
    };
    for (int v = 0; v < JUNO_VOICES; ++v)
        voice_[v].init(sampleRate, kSeeds[v]);

    lfo_.init(sampleRate);
    hpf_.init(sampleRate);
    chorus_.init(sampleRate);
    arp_.init(sampleRate);

    dcL_.setCutoff(JUNO_DC_BLOCK_HZ, sampleRate);
    dcR_.setCutoff(JUNO_DC_BLOCK_HZ, sampleRate);
    hiss_.seed(0x1F123BB5u);

    /* 6th order Butterworth ahead of the decimation, on the summed voices --
     * one chain for all six, not one each. */
    dec_[0].set(JUNO_DECIMATE_HZ, 0.51764f, osSr);
    dec_[1].set(JUNO_DECIMATE_HZ, 0.70711f, osSr);
    dec_[2].set(JUNO_DECIMATE_HZ, 1.93185f, osSr);
    for (int i = 0; i < 3; ++i) dec_[i].reset();

    applyAll();
}

/* ------------------------------------------------------------------------ */
/* Panel                                                                     */
/* ------------------------------------------------------------------------ */
void Juno::setParameter(int32_t index, float value)
{
    if (index < 0 || index >= JUNO_TOTAL_COUNT) return;
    p_[index] = junoClamp(value, 0.0f, 1.0f);
    applyParameter((int) index);
}

void Juno::applyAll()
{
    for (int i = 0; i < JUNO_TOTAL_COUNT; ++i) applyParameter(i);
}

void Juno::applyParameter(int id)
{
    const float v = p_[id];

    switch (id) {
    /* --- LFO ------------------------------------------------------------ */
    case JUNO_LFO_RATE:  lfo_.setRate(v);  break;
    case JUNO_LFO_DELAY: lfo_.setDelay(v); break;

    case JUNO_LFO_TRIG:
        /* Auto retriggers the delay on every new phrase; Manual leaves it to
         * the panel button. */
        lfoAuto_ = (junoParamStep(v, 2) == 0);
        break;

    /* --- DCO ------------------------------------------------------------ */
    case JUNO_DCO_RANGE: {
        /* 16' / 8' / 4', with 8' the note as played. */
        static const float kOct[JUNO_RANGE_COUNT] = { -1.0f, 0.0f, 1.0f };
        vp_.octave = kOct[junoParamStep(v, JUNO_RANGE_COUNT)];
        break;
    }
    /*
     * Squared. Measured against Roland's plugin at 0.1 steps, the pitch it
     * reaches is 384 cents times the square of the setting, and the square
     * fits every one of those ten points to a hundredth. Taken straight -- as
     * it was -- the slider at a tenth gives 29 cents where the instrument
     * gives three, which is the difference between a hint of vibrato and a
     * wobble. Sixteen factory patches use it, nearly all of them down at that
     * end: the violin, the clarinet and the oboe are meant to have a few cents
     * of it, not tens.
     */
    case JUNO_DCO_LFO:       vp_.dcoLfo   = v * v; break;
    case JUNO_DCO_PWM:       vp_.pwm      = v; break;
    case JUNO_DCO_PWM_MODE:  vp_.pwmMode  = junoParamStep(v, 3); break;
    case JUNO_DCO_SAW:       vp_.saw      = junoParamOn(v); break;
    case JUNO_DCO_PULSE:     vp_.pulse    = junoParamOn(v); break;
    case JUNO_DCO_SUB:       vp_.subOn    = junoParamOn(v); break;
    /* Through the measured fader taper, once here rather than per sample. */
    case JUNO_DCO_SUB_LEVEL: vp_.subLevel = junoDcoLevel(v); break;
    case JUNO_DCO_NOISE:     vp_.noise    = junoDcoLevel(v); break;

    /* --- HPF ------------------------------------------------------------ */
    case JUNO_HPF:
        hpf_.setPosition(junoParamStep(v, JUNO_HPF_POSITIONS));
        break;

    /* --- VCF ------------------------------------------------------------ */
    case JUNO_VCF_FREQ:
        /*
         * Where the 20 Hz .. 18 kHz of the specifications page sits on the
         * slider. It used to be spread over the whole travel, and it is not:
         * measured against Roland's plugin the corner climbs 17 octaves per
         * unit of slider, passing 20 Hz at 0.16 and 18 kHz at 0.74, with the
         * ends flat. The range is the service notes' and unchanged; only its
         * placement moves, and that is what the plugin is being asked.
         *
         * Below 0.16 the plugin keeps going down to about 13 Hz rather than
         * stopping, but that is under the specification's own floor and below
         * anything a note reaches through four poles, so the floor stays where
         * the service notes put it.
         */
        vp_.cutoffOct = junoClamp(v * JUNO_CUTOFF_OCT_PER_UNIT - JUNO_CUTOFF_OCT_ZERO,
                                  0.0f, log2f(JUNO_CUTOFF_MAX_HZ / JUNO_CUTOFF_MIN_HZ));
        break;
    case JUNO_VCF_RES:      vp_.resonance = v * JUNO_RESONANCE_MAX; break;
    case JUNO_VCF_ENV:      vp_.envAmount = v; break;
    case JUNO_VCF_POLARITY:
        vp_.envPolarity = (junoParamStep(v, 2) == 1) ? 1.0f : -1.0f;
        break;
    /* Held in octaves, through the measured curve. */
    case JUNO_VCF_LFO:      vp_.lfoOct = junoVcfLfoOctaves(v); break;
    case JUNO_VCF_KYBD:     vp_.keyFollow = v; break;

    /* --- VCA ------------------------------------------------------------ */
    case JUNO_VCA_LEVEL:
        /*
         * Squared.
         *
         * It was linear, on the grounds that the imported junox patch set was
         * authored against a linear mapping and bending the curve underneath
         * it would misrepresent every patch at once. That argument has since
         * lapsed: the 56 patches now come from the owner's manual chart, whose
         * level column could not be read at all, so every one of them carries
         * the same 0.700 and no patch is authored against either curve.
         *
         * Roland's plugin settles it. Its level slider follows the square of
         * the setting to within a decibel over the top two thirds of the
         * travel -- 0.5 gives -12.3 dB where linear would give -6 -- and eases
         * off below 0.3, where it runs up to 3 dB above the square. The square
         * is taken here and the easing is not: it is the bottom of a level
         * control, and no factory patch is anywhere near it.
         *
         * The 0.700 that the whole bank sits at loses 3.1 dB to the change, so
         * the headroom constant on the summed voices makes it back (0.35 ->
         * 0.50) and the bank comes out exactly where it was. This is also the
         * level that drives the chorus, so it decides how hard the
         * bucket-brigade lines are pushed -- and that product, too, is
         * unchanged for the bank.
         */
        volume_ = v * v;
        break;
    case JUNO_VCA_MODE: {
        const bool gate = (junoParamStep(v, 2) == 1);
        vp_.gateMode = gate;
        for (int i = 0; i < JUNO_VOICES; ++i) voice_[i].setGateMode(gate);
        break;
    }

    /* --- ENV ------------------------------------------------------------ */
    case JUNO_ENV_ATTACK:
    case JUNO_ENV_DECAY:
    case JUNO_ENV_SUSTAIN:
    case JUNO_ENV_RELEASE:
        for (int i = 0; i < JUNO_VOICES; ++i)
            voice_[i].setEnvelope(p_[JUNO_ENV_ATTACK], p_[JUNO_ENV_DECAY],
                                  p_[JUNO_ENV_SUSTAIN], p_[JUNO_ENV_RELEASE]);
        break;

    /* --- Chorus --------------------------------------------------------- */
    case JUNO_CHORUS:
        chorus_.setMode(junoParamStep(v, JUNO_CH_COUNT));
        break;

    /* --- Not on the panel ------------------------------------------------ */
    case JUNO_TUNE:
        tuneSemis_ = (v * 2.0f - 1.0f) * (JUNO_TUNE_CENTS / 100.0f);
        break;
    case JUNO_BEND_RANGE:
        /* Eight positions, 0..7 semitones: the range of the instrument's
         * own Bend Sens (DCO) control. See JUNO_BEND_MAX_SEMITONES. */
        bendRange_ = (float) junoParamStep(v, 8);
        break;
    case JUNO_TRANSPOSE:
        transpose_ = junoParamStep(v, 5) - 2;
        break;

    /* --- Instrument settings, no patch touches these --------------------- */
    case JUNO_MASTER:
        master_ = v;
        break;

    case JUNO_ARP_ON: {
        const bool on = junoParamOn(v);
        if (on != arpOn_) {
            arpOn_ = on;
            /* Switching either way leaves the keyboard in a different world,
             * so everything sounding is released rather than left hanging. */
            stopVoices();
            arp_.clear();
        }
        break;
    }
    case JUNO_ARP_MODE:  arp_.setMode(junoParamStep(v, JUNO_ARP_MODES));      break;
    case JUNO_ARP_RANGE: arp_.setRange(junoParamStep(v, JUNO_ARP_RANGES) + 1); break;
    case JUNO_ARP_RATE:  arp_.setRate(v);                                     break;

    case JUNO_HOLD: {
        const bool h = junoParamOn(v);
        if (h != hold_) {
            hold_ = h;
            /* Letting go of HOLD drops whatever it was latching. */
            if (!h) {
                arp_.clear();
                stopVoices();
            }
        }
        break;
    }

    default: break;
    }

    /* Anything that moves the pitch has to be pushed into the voices, which
     * cache their phase increment rather than working it out every sample. */
    const float pitch = tuneSemis_ + bendNorm_ * bendRange_
                      + (float) (transpose_ * 12);
    if (pitch != vp_.pitchSemis || id == JUNO_DCO_RANGE) {
        vp_.pitchSemis = pitch;
        for (int i = 0; i < JUNO_VOICES; ++i) voice_[i].updatePitch(vp_);
    }
}

/* ------------------------------------------------------------------------ */
/* Voice assignment                                                          */
/*                                                                           */
/* Six voices, and what happens on the seventh note is part of how a Juno     */
/* plays. Free voices are handed out in round-robin order rather than always   */
/* from the top, which is what gives a Juno its characteristic cycling: hold   */
/* a chord, release it, play it again and a different set of cards responds.   */
/*                                                                           */
/* When none is free the quietest one that is no longer held goes first, and   */
/* only if every voice is still held does the oldest get taken.               */
/* ------------------------------------------------------------------------ */
int Juno::allocate(int note)
{
    /* Same note already sounding: reuse that card rather than stacking two. */
    for (int i = 0; i < JUNO_VOICES; ++i)
        if (voice_[i].isHeld() && voice_[i].note() == note)
            return i;

    for (int n = 0; n < JUNO_VOICES; ++n) {
        const int i = (nextVoice_ + n) % JUNO_VOICES;
        if (!voice_[i].isActive()) {
            nextVoice_ = (i + 1) % JUNO_VOICES;
            return i;
        }
    }

    int best = -1;
    float quietest = 1.0e9f;
    for (int i = 0; i < JUNO_VOICES; ++i) {
        if (voice_[i].isHeld()) continue;
        const float c = voice_[i].claim();
        if (c < quietest) { quietest = c; best = i; }
    }
    if (best >= 0) return best;

    uint32_t oldest = 0xFFFFFFFFu;
    best = 0;
    for (int i = 0; i < JUNO_VOICES; ++i)
        if (taken_[i] < oldest) { oldest = taken_[i]; best = i; }
    return best;
}

void Juno::noteOn(int32_t note, int32_t velocity)
{
    /* The keyboard produces a gate, not a velocity: every key sounds at the
     * same level however it is struck. */
    if (velocity <= 0) { noteOff(note); return; }
    if (note < 0 || note > 127) return;

    /*
     * With the arpeggiator running the key does not reach a voice card at all:
     * it joins the pattern, and the arpeggio clock decides when it sounds.
     */
    if (arpOn_) {
        arp_.keyDown((int) note);
        return;
    }

    /* Auto trigger restarts the LFO delay when a phrase begins -- that is,
     * when nothing else is being held. */
    if (lfoAuto_) {
        bool anyHeld = false;
        for (int i = 0; i < JUNO_VOICES; ++i)
            if (voice_[i].isHeld()) { anyHeld = true; break; }
        if (!anyHeld) lfo_.trigger();
    }

    const int v = allocate((int) note);
    taken_[v]     = ++stamp_;
    pedalHeld_[v] = false;
    voice_[v].noteOn((int) note, vp_);
}

void Juno::noteOff(int32_t note)
{
    if (arpOn_) {
        /* HOLD latches the keys, so releasing one leaves the pattern alone. */
        if (!hold_) arp_.keyUp((int) note);
        return;
    }

    for (int i = 0; i < JUNO_VOICES; ++i) {
        if (voice_[i].isHeld() && voice_[i].note() == (int) note) {
            if (sustain_) pedalHeld_[i] = true;
            else          voice_[i].noteOff();
        }
    }
}

void Juno::resetVoices()
{
    arp_.clear();
    for (int i = 0; i < JUNO_VOICES; ++i) voice_[i].reset();
    memset(pedalHeld_, 0, sizeof(pedalHeld_));
    sustain_ = false;
    chorus_.reset();
    hpf_.reset();
    dcL_.reset(); dcR_.reset();
    for (int i = 0; i < 3; ++i) dec_[i].reset();
}

void Juno::stopVoices()
{
    sustain_ = false;
    for (int i = 0; i < JUNO_VOICES; ++i) {
        pedalHeld_[i] = false;
        if (voice_[i].isHeld()) voice_[i].noteOff();
    }
}

void Juno::resetControllers()
{
    sustain_  = false;
    bendNorm_ = 0.0f;
    applyParameter(JUNO_TUNE);
}

int Juno::activeVoices() const
{
    int n = 0;
    for (int i = 0; i < JUNO_VOICES; ++i) if (voice_[i].isActive()) ++n;
    return n;
}

/*
 * The arpeggiator's own way into the voice allocator. It cannot go through
 * noteOn/noteOff, because those now hand keys to the arpeggiator -- which
 * would put every step straight back into the pattern.
 */
void Juno::arpNoteOn(int note)
{
    const int v = allocate(note);
    taken_[v]     = ++stamp_;
    pedalHeld_[v] = false;
    voice_[v].noteOn(note, vp_);
}

void Juno::arpNoteOff(int note)
{
    for (int i = 0; i < JUNO_VOICES; ++i)
        if (voice_[i].isHeld() && voice_[i].note() == note)
            voice_[i].noteOff();
}

/* ------------------------------------------------------------------------ */
/* MIDI                                                                     */
/* ------------------------------------------------------------------------ */
bool Juno::processMidiController(uint8_t cc, uint8_t value)
{
    switch (cc) {
        case 64:
            sustain_ = (value >= 64);
            if (!sustain_) {
                for (int i = 0; i < JUNO_VOICES; ++i) {
                    if (pedalHeld_[i]) {
                        pedalHeld_[i] = false;
                        voice_[i].noteOff();
                    }
                }
            }
            return true;

        case 7:
            /*
             * Channel Volume drives the master, not the patch's VCA level.
             * Handled here as well as in the firmware's MIDI front end -- the
             * engine has to answer for itself, or the host test and anything
             * else driving it directly would send CC 7 into a table that no
             * longer contains it and get silence in reply.
             */
            setMasterVolume((float) value / 127.0f);
            return true;

        case 120: resetVoices();      return true;
        case 123: stopVoices();       return true;
        case 121: resetControllers(); return true;
        default: break;
    }

    const int id = junoParamForCc(cc);
    if (id >= 0) {
        setParameter(id, (float) value / 127.0f);
        return true;
    }
    return false;
}

void Juno::setPitchBend(int32_t bend14)
{
    if (bend14 < 0)     bend14 = 0;
    if (bend14 > 16383) bend14 = 16383;
    bendNorm_ = (bend14 >= 8192) ? (float) (bend14 - 8192) / 8191.0f
                                 : (float) (bend14 - 8192) / 8192.0f;
    applyParameter(JUNO_TUNE);      /* recomputes vp_.pitchSemis */
}

/* CC 7 is Channel Volume, so it belongs to the instrument rather than to the
 * patch. The patch's own VCA level is a separate control. */
void Juno::setVolume(uint8_t value)
{
    if (value > 127) value = 127;
    setMasterVolume((float) value / 127.0f);
}

uint8_t Juno::getVolume() const
{
    return (uint8_t) (master_ * 127.0f + 0.5f);
}

/* ------------------------------------------------------------------------ */
/* Patches                                                                   */
/* ------------------------------------------------------------------------ */
void Juno::setProgram(int32_t program)
{
    if (program < 0 || program >= JUNO_NPROGRAMS) return;
    curProgram_ = program;

    /* A patch is the whole sound, so every one of its controls is written and
     * nothing is left over from the sound before it. It stops at
     * JUNO_PARAM_COUNT: the arpeggiator and the master volume are instrument
     * settings, and a patch change in the middle of a performance must not
     * stop the arpeggio or move the volume. */
    for (int i = 0; i < JUNO_PARAM_COUNT; ++i)
        setParameter(i, junoPrograms[program].param[i]);
}

void Juno::getProgramName(char* name) const
{
    if (!name) return;
    strncpy(name, junoPrograms[curProgram_].name, sizeof(junoPrograms[0].name));
    name[sizeof(junoPrograms[0].name) - 1] = 0;
}

void Juno::getParameterName(int32_t index, char* text) const
{
    if (!text) return;
    if (index < 0 || index >= JUNO_TOTAL_COUNT) { text[0] = 0; return; }
    snprintf(text, 16, "%s", kJunoParams[index].name);
}

void Juno::getParameterDisplay(int32_t index, char* text) const
{
    junoFormatValue((int) index, getParameter(index), text, 16);
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                 */
/*                                                                           */
/* Per output sample: the LFO, then the six voices, then the chain they share  */
/* -- decimation, high-pass, amplifier level, chorus, output stage. The order  */
/* is the one on the block diagram, and the position of the amplifier level    */
/* before the chorus is deliberate: it is what drives the bucket-brigade lines */
/* and lets a loud patch distort them.                                        */
/* ------------------------------------------------------------------------ */
void Juno::processFloat(float* out_l, float* out_r, int frames)
{
    int done = 0;
    while (done < frames)
    {
        int chunk = frames - done;
        if (chunk > JUNO_BLOCK) chunk = JUNO_BLOCK;

        for (int i = 0; i < chunk; ++i)
        {
            /* The arpeggio clock, before the voices: a step that fires now
             * should be audible in this sample rather than the next. */
            if (arpOn_) {
                int on = -1, off = -1;
                arp_.tick(on, off);
                if (off >= 0) arpNoteOff(off);
                if (on  >= 0) arpNoteOn(on);
            }

            const float lfo = lfo_.process();

            float sum = 0.0f;
            for (int v = 0; v < JUNO_VOICES; ++v)
                sum += voice_[v].process(vp_, lfo);

            /* Six voices at once would otherwise run out of headroom before
             * the filter does. */
            sum *= 0.50f;

            for (int os = 0; os < JUNO_OVERSAMPLE; ++os)
                sum = dec_[2].process(dec_[1].process(dec_[0].process(sum)));

            sum = hpf_.process(sum);
            sum += hiss_.white() * JUNO_HISS_LEVEL;
            sum *= volume_;

            out_l[done + i] = sum;
            out_r[done + i] = sum;
        }

        if (!chorus_.isOff())
            chorus_.process(out_l + done, out_r + done, chunk);

        /* The master sits ahead of the limiter, so turning it up drives the
         * output stage rather than clipping past it. */
        for (int i = 0; i < chunk; ++i) {
            out_l[done + i] = junoLimit(dcL_.process(out_l[done + i]) * master_);
            out_r[done + i] = junoLimit(dcR_.process(out_r[done + i]) * master_);
        }

        done += chunk;
    }
}

void Juno::process(int16_t* outputs_r, int16_t* outputs_l)
{
    float l[JUNO_BLOCK], r[JUNO_BLOCK];
    processFloat(l, r, I2S_BUFFER_WORDS);

    for (int i = 0; i < I2S_BUFFER_WORDS; ++i) {
        float a = junoClamp(l[i], -1.0f, 0.999969f);
        float b = junoClamp(r[i], -1.0f, 0.999969f);
        outputs_l[i] = (int16_t) (a * 32767.0f);
        outputs_r[i] = (int16_t) (b * 32767.0f);
    }
}

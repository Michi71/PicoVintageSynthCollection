/*
  solina.cpp -- ARP Solina String Ensemble, top level
*/

#include "solina/solina.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------ */
/* Parameter ranges                                                          */
/* ------------------------------------------------------------------------ */
static inline float solinaLerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* Times scale exponentially, so the control travel feels sensible */
static inline float solinaTime(float t, float lo, float hi)
{
    return lo * powf(hi / lo, t);
}

/* Filter tuning: -20 .. +60 semitones (range taken from string-machine) */
static inline float solinaSemis(float t)
{
    return solinaLerp(-20.0f, 60.0f, t);
}

/* ------------------------------------------------------------------------ */
/* Factory programs                                                          */
/*                                                                           */
/* Registrations following the usual Solina combinations. The voicing is the  */
/* same in every program; in the original it is fixed component values and is */
/* exposed here only for trimming.                                           */
/* ------------------------------------------------------------------------ */
#define SOLINA_TONE_DEFAULTS \
    /* TONE_LOWPASS */ 0.3150f, \
    /* TONE_HIGHPASS*/ 0.4025f, \
    /* TONE_SHELF   */ 0.5600f, \
    /* FORMANT      */ 0.4550f, \
    /* SHAPER       */ 1.0000f

#define SOLINA_MOD_DEFAULTS \
    /* ENSEMBLE      */ 1.0000f, \
    /* TREMOLO_RATE  */ 0.4717f, /* 5.83 Hz */ \
    /* TREMOLO_DEPTH */ 0.1000f, \
    /* CHORUS_RATE   */ 0.4675f, /* 0.58 Hz */ \
    /* CHORUS_DEPTH  */ 0.9055f, \
    /* ENSEMBLE_TONE */ 0.2000f, \
    /* ENSEMBLE_WIDTH*/ 0.7000f, \
    /* PHASER        */ 0.0000f, \
    /* PHASER_RATE   */ 0.2500f, \
    /* PHASER_COLOR  */ 0.5000f

const SolinaProgram solinaPrograms[SOLINA_NPROGRAMS] = {
/*        CBass  Cello  Viola  Violin Trump  Horn   BassVol Cresc  Sust   Vol    Tune */
{ "Violin",
  { 0.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  0.70f, 0.18f, 0.35f, 0.80f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },

{ "Viola+Violin",
  { 0.0f,  0.0f,  1.0f,  1.0f,  0.0f,  0.0f,  0.70f, 0.22f, 0.40f, 0.72f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },

{ "Full Strings",
  { 1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  0.75f, 0.25f, 0.45f, 0.62f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },

{ "Cello+Viola",
  { 0.0f,  1.0f,  1.0f,  0.0f,  0.0f,  0.0f,  0.80f, 0.28f, 0.45f, 0.75f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },

{ "Brass",
  { 0.0f,  0.0f,  0.0f,  0.0f,  1.0f,  1.0f,  0.70f, 0.10f, 0.25f, 0.78f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },

{ "Strings+Brass",
  { 0.0f,  0.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.70f, 0.20f, 0.40f, 0.68f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },

{ "Contrabass",
  { 1.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.90f, 0.15f, 0.30f, 0.85f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },

{ "Slow Swell",
  { 0.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  0.70f, 0.62f, 0.70f, 0.70f, 0.50f,
    SOLINA_MOD_DEFAULTS, SOLINA_TONE_DEFAULTS } },
};

/* ------------------------------------------------------------------------ */
/* Output level                                                              */
/*                                                                           */
/* The summing busses carry band-limited sawtooths of +/-1 per key. The       */
/* factor is chosen so that normal playing stays comfortably below the        */
/* limiting threshold; dense clusters are then caught by the soft limiter of  */
/* the output stage (solinaSoftClip). In the original the Output Amplifier    */
/* does that at its supply rails.                                            */
/* ------------------------------------------------------------------------ */
#define SOLINA_OUTPUT_SCALE 0.115f

/* ------------------------------------------------------------------------ */
Solina::Solina()
{
    memset(bus8_,   0, sizeof(bus8_));
    memset(bus4_,   0, sizeof(bus4_));
    memset(bass8_,  0, sizeof(bass8_));
    memset(bass16_, 0, sizeof(bass16_));
    memset(mono_,   0, sizeof(mono_));
    memset(left_,   0, sizeof(left_));
    memset(right_,  0, sizeof(right_));

    setSampleRate((float) SAMPLING_RATE);
    setProgram(0);
}

void Solina::setSampleRate(float sampleRate)
{
    samplerate_ = sampleRate;

    divider_.init(sampleRate);
    keyboard_.init(sampleRate);
    registers_.init(sampleRate);
    ensemble_.init(sampleRate);
    phaser_.init(sampleRate);

    outDc_.init(sampleRate, 20.0f);
    correctionL_.init(sampleRate);
    correctionR_.init(sampleRate);
    correctionL_.setCutoff(12000.0f);
    correctionR_.setCutoff(12000.0f);

    applyAllParameters();
}

/* ------------------------------------------------------------------------ */
/* Parameter                                                                 */
/* ------------------------------------------------------------------------ */
void Solina::applyParameter(int32_t index, float value)
{
    switch (index) {
        case SOLINA_CONTRABASS: registers_.setContrabass(value != 0.0f); break;
        case SOLINA_CELLO:      registers_.setCello(value != 0.0f);      break;
        case SOLINA_VIOLA:      registers_.setViola(value != 0.0f);      break;
        case SOLINA_VIOLIN:     registers_.setViolin(value != 0.0f);     break;
        case SOLINA_TRUMPET:    registers_.setTrumpet(value != 0.0f);    break;
        case SOLINA_HORN:       registers_.setHorn(value != 0.0f);       break;

        case SOLINA_BASS_VOLUME: registers_.setBassVolume(value);        break;

        /* Sustain Circuits: attack 5 ms .. 1.5 s, decay 20 ms .. 4 s */
        case SOLINA_CRESCENDO:
            keyboard_.setCrescendo(solinaTime(value, 0.005f, 1.5f));
            break;
        case SOLINA_SUSTAIN:
            keyboard_.setSustain(solinaTime(value, 0.020f, 4.0f));
            break;

        case SOLINA_VOLUME:
            break;   /* evaluated in the renderer */

        /* Master oscillator tuning: +/- 1 semitone around centre */
        case SOLINA_TUNE:
            divider_.setTune((value * 2.0f - 1.0f) + bend_);
            break;

        case SOLINA_ENSEMBLE:
            ensemble_.setEnabled(value != 0.0f);
            break;
        case SOLINA_TREMOLO_RATE:
            ensemble_.setTremoloRate(solinaLerp(SOLINA_TREMOLO_HZ_MIN,
                                                SOLINA_TREMOLO_HZ_MAX, value));
            break;
        case SOLINA_TREMOLO_DEPTH:
            ensemble_.setTremoloDepth(value);
            break;
        case SOLINA_CHORUS_RATE:
            ensemble_.setChorusRate(solinaLerp(SOLINA_CHORUS_HZ_MIN,
                                               SOLINA_CHORUS_HZ_MAX, value));
            break;
        case SOLINA_CHORUS_DEPTH:
            ensemble_.setChorusDepth(value);
            break;
        /* 0.5 = schematic values (11.7 kHz / 5.9 kHz), 0 = an octave lower,
         * 1 = an octave higher */
        case SOLINA_ENSEMBLE_TONE:
            ensemble_.setReconScale(powf(2.0f, (value - 0.5f) * 2.0f));
            break;
        case SOLINA_ENSEMBLE_WIDTH:
            ensemble_.setWidth(value);
            break;

        /* Phaser (a Behringer addition) */
        case SOLINA_PHASER:
            phaser_.setEnabled(value != 0.0f);
            break;
        case SOLINA_PHASER_RATE:
            /* exponential, so the control travel feels even */
            phaser_.setRate(solinaTime(value, SOLINA_PHASER_HZ_MIN,
                                              SOLINA_PHASER_HZ_MAX));
            break;
        case SOLINA_PHASER_COLOR:
            phaser_.setColor(value);
            break;

        case SOLINA_TONE_LOWPASS:
        case SOLINA_TONE_HIGHPASS:
        case SOLINA_TONE_SHELF:
            registers_.setTone(solinaSemis(params_[SOLINA_TONE_LOWPASS]),
                               solinaSemis(params_[SOLINA_TONE_HIGHPASS]),
                               solinaSemis(params_[SOLINA_TONE_SHELF]),
                               6.0f);
            break;
        case SOLINA_FORMANT:
            registers_.setFormant(solinaSemis(value));
            break;
        case SOLINA_SHAPER:
            registers_.setShaper(solinaLerp(0.1f, 1.0f, value));
            break;

        default:
            break;
    }
}

void Solina::applyAllParameters()
{
    for (int32_t i = 0; i < SOLINA_PARAM_COUNT; ++i)
        applyParameter(i, params_[i]);
}

void Solina::setParameter(int32_t index, float value)
{
    if (index < 0 || index >= SOLINA_PARAM_COUNT)
        return;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    params_[index] = value;
    applyParameter(index, value);
}

float Solina::getParameter(int32_t index) const
{
    if (index < 0 || index >= SOLINA_PARAM_COUNT)
        return 0.0f;
    return params_[index];
}

static const char* solinaParamNames[SOLINA_PARAM_COUNT] = {
    "Contrabass", "Cello",      "Viola",      "Violin",
    "Trumpet",    "Horn",       "Bass Vol",   "Crescendo",
    "Sustain",    "Volume",     "Tune",       "Ensemble",
    "Trem Rate",  "Trem Depth", "Chor Rate",  "Chor Depth",
    "Ens Tone",   "Ens Width",  "Phaser",     "Phas Rate",
    "Phas Color", "Tone LP",    "Tone HP",    "Tone Shelf",
    "Formant",    "Shaper"
};

static bool solinaIsSwitch(int32_t i)
{
    return i <= SOLINA_HORN || i == SOLINA_ENSEMBLE || i == SOLINA_PHASER;
}

void Solina::getParameterName(int32_t index, char* text) const
{
    if (index < 0 || index >= SOLINA_PARAM_COUNT) { text[0] = '\0'; return; }
    snprintf(text, 16, "%s", solinaParamNames[index]);
}

void Solina::getParameterLabel(int32_t index, char* text) const
{
    if (solinaIsSwitch(index))          { snprintf(text, 8, "%s", "");    return; }
    switch (index) {
        case SOLINA_CRESCENDO:
        case SOLINA_SUSTAIN:            snprintf(text, 8, "%s", "s");     break;
        case SOLINA_TREMOLO_RATE:
        case SOLINA_CHORUS_RATE:
        case SOLINA_ENSEMBLE_TONE:
        case SOLINA_PHASER_RATE:        snprintf(text, 8, "%s", "Hz");    break;
        case SOLINA_TUNE:               snprintf(text, 8, "%s", "st");    break;
        case SOLINA_TONE_LOWPASS:
        case SOLINA_TONE_HIGHPASS:
        case SOLINA_TONE_SHELF:
        case SOLINA_FORMANT:            snprintf(text, 8, "%s", "st");    break;
        default:                        snprintf(text, 8, "%s", "%");     break;
    }
}

void Solina::getParameterDisplay(int32_t index, char* text) const
{
    if (index < 0 || index >= SOLINA_PARAM_COUNT) { text[0] = '\0'; return; }
    const float v = params_[index];

    if (solinaIsSwitch(index))
    {
        snprintf(text, 8, "%s", v != 0.0f ? "on" : "off");
        return;
    }

    switch (index) {
        case SOLINA_CRESCENDO:
            snprintf(text, 8, "%.2f", solinaTime(v, 0.005f, 1.5f)); break;
        case SOLINA_SUSTAIN:
            snprintf(text, 8, "%.2f", solinaTime(v, 0.020f, 4.0f)); break;
        case SOLINA_TREMOLO_RATE:
            snprintf(text, 8, "%.2f", solinaLerp(SOLINA_TREMOLO_HZ_MIN,
                                                 SOLINA_TREMOLO_HZ_MAX, v)); break;
        case SOLINA_CHORUS_RATE:
            snprintf(text, 8, "%.2f", solinaLerp(SOLINA_CHORUS_HZ_MIN,
                                                 SOLINA_CHORUS_HZ_MAX, v)); break;
        case SOLINA_TUNE:
            snprintf(text, 8, "%+.2f", v * 2.0f - 1.0f); break;
        case SOLINA_ENSEMBLE_TONE:
            snprintf(text, 8, "%.0f", SOLINA_RECON_F2
                     * powf(2.0f, (v - 0.5f) * 2.0f)); break;
        case SOLINA_PHASER_RATE:
            snprintf(text, 8, "%.2f", solinaTime(v, SOLINA_PHASER_HZ_MIN,
                                                    SOLINA_PHASER_HZ_MAX)); break;
        case SOLINA_TONE_LOWPASS:
        case SOLINA_TONE_HIGHPASS:
        case SOLINA_TONE_SHELF:
        case SOLINA_FORMANT:
            snprintf(text, 8, "%+.1f", solinaSemis(v)); break;
        default:
            snprintf(text, 8, "%d", (int) (v * 100.0f + 0.5f)); break;
    }
}

/* ------------------------------------------------------------------------ */
/* Programs                                                                  */
/* ------------------------------------------------------------------------ */
void Solina::setProgram(int32_t program)
{
    if (program < 0 || program >= SOLINA_NPROGRAMS)
        return;

    curProgram_ = program;
    const SolinaProgram& p = solinaPrograms[program];

    for (int32_t i = 0; i < SOLINA_PARAM_COUNT; ++i)
        params_[i] = p.param[i];

    snprintf(programName_, sizeof(programName_), "%s", p.name);
    applyAllParameters();
}

void Solina::getProgramName(char* name) const
{
    snprintf(name, sizeof(programName_), "%s", programName_);
}

void Solina::setProgramName(const char* name)
{
    snprintf(programName_, sizeof(programName_), "%s", name);
}

/* ------------------------------------------------------------------------ */
/* MIDI                                                                      */
/* ------------------------------------------------------------------------ */
void Solina::noteOn(int32_t note, int32_t velocity)
{
    if (velocity <= 0) { noteOff(note); return; }

    int n = (int) note + transpose_;
    while (n > 127) n -= 12;
    while (n < 0)   n += 12;

    /* The Solina is not velocity sensitive -- the gate circuit knows only
     * open and closed. */
    keyboard_.noteOn(n);
}

void Solina::noteOff(int32_t note)
{
    int n = (int) note + transpose_;
    while (n > 127) n -= 12;
    while (n < 0)   n += 12;

    keyboard_.noteOff(n);
}

/*
 * In the original, pitch bend acts on the master oscillator and therefore on
 * every note at once -- the same here, as a detuning of the divider chain.
 */
void Solina::setPitchBend(int32_t bend14)
{
    if (bend14 < 0)     bend14 = 0;
    if (bend14 > 16383) bend14 = 16383;

    bend_ = (((float) bend14 - 8192.0f) / 8192.0f) * 2.0f;   /* +/- 2 semitones */
    divider_.setTune((params_[SOLINA_TUNE] * 2.0f - 1.0f) + bend_);
}

bool Solina::processMidiController(uint8_t cc, uint8_t value)
{
    switch (cc) {
        case 0x07:  setVolume(value); return true;
        case 0x40:  keyboard_.setSustainPedal(value >= 64); return true;
        case 0x78:  resetVoices(); return true;
        case 0x79:  resetControllers(); return true;
        case 0x7b:  stopVoices(); return true;
        default:    return false;
    }
}

void Solina::resetVoices()
{
    keyboard_.reset();
    registers_.reset();
    ensemble_.reset();
    phaser_.reset();
    divider_.reset();
    outDc_.clear();
    correctionL_.clear();
    correctionR_.clear();
    applyAllParameters();
}

void Solina::stopVoices()
{
    keyboard_.allNotesOff();
}

void Solina::resetControllers()
{
    bend_ = 0.0f;
    keyboard_.setSustainPedal(false);
    divider_.setTune(params_[SOLINA_TUNE] * 2.0f - 1.0f);
}

void Solina::setVolume(uint8_t value)
{
    volume_ = (value > 127) ? 127 : value;
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------ */
void Solina::renderBlock(int frames)
{
    keyboard_.process(divider_, bus8_, bus4_, bass8_, bass16_, frames);
    registers_.process(bus8_, bus4_, bass8_, bass16_, mono_, frames);

    for (int i = 0; i < frames; ++i)
        mono_[i] = outDc_.process(mono_[i]);

    ensemble_.process(mono_, left_, right_, frames);

    /* Phaser as an insert behind the ensemble -- on the Behringer this is
     * brought out on its own "Phaser in/out" jacks. */
#if !SM_NO_PHASER
    phaser_.process(left_, right_, frames);
#endif

    /* Output Amplifier + Correction Filter */
    for (int i = 0; i < frames; ++i)
    {
        left_[i]  = correctionL_.process(left_[i]);
        right_[i] = correctionR_.process(right_[i]);
    }
}

void Solina::processFloat(float* out_l, float* out_r, int frames)
{
    const float gain = SOLINA_OUTPUT_SCALE
                       * params_[SOLINA_VOLUME]
                       * ((float) volume_ / 100.0f);

    while (frames > 0)
    {
        const int n = (frames > SOLINA_BLOCK) ? SOLINA_BLOCK : frames;

        renderBlock(n);

        for (int i = 0; i < n; ++i)
        {
            *out_l++ = solinaSoftClip(left_[i]  * gain, SOLINA_CLIP_THRESHOLD);
            *out_r++ = solinaSoftClip(right_[i] * gain, SOLINA_CLIP_THRESHOLD);
        }

        frames -= n;
    }
}

void Solina::process(int16_t* outputs_r, int16_t* outputs_l)
{
    float l[SOLINA_BLOCK], r[SOLINA_BLOCK];
    processFloat(l, r, SOLINA_BLOCK);

    for (int i = 0; i < SOLINA_BLOCK; ++i)
    {
        float a = l[i], b = r[i];
        if (a >  1.0f) a =  1.0f;
        if (a < -1.0f) a = -1.0f;
        if (b >  1.0f) b =  1.0f;
        if (b < -1.0f) b = -1.0f;

        outputs_l[i] = (int16_t) (a * 32767.0f);
        outputs_r[i] = (int16_t) (b * 32767.0f);
    }
}

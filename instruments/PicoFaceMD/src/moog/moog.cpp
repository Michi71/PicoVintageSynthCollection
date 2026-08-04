/*
  moog.cpp -- Minimoog Model D, top level
*/

#include "moog/moog.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

Moog::Moog()
{
    voice_.init(samplerate_);
    fx_.init(samplerate_);
    setProgram(0);
}

void Moog::setSampleRate(float sampleRate)
{
    if (sampleRate < 8000.0f) sampleRate = 8000.0f;
    samplerate_ = sampleRate;

    /* The voice rebuilds its filters and re-derives every cached value from
     * the parameters it is already holding, so the panel survives a sample
     * rate change. Writing the preset back over it here would throw away
     * whatever had been edited since. */
    voice_.setSampleRate(sampleRate);

    /* The effects rebuild their delay lines from scratch, which loses their
     * settings, so they are written back afterwards. */
    fx_.init(sampleRate);
    for (int i = MOOG_FX_SLOT_A; i < MOOG_PARAM_COUNT; ++i)
        fx_.setParameter(i, voice_.parameter(i));
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------ */
void Moog::processFloat(float* out_l, float* out_r, int frames)
{
    int done = 0;
    while (done < frames)
    {
        int chunk = frames - done;
        if (chunk > MOOG_BLOCK) chunk = MOOG_BLOCK;

        voice_.process(mono_, chunk);

        for (int i = 0; i < chunk; ++i) {
            out_l[done + i] = mono_[i];
            out_r[done + i] = mono_[i];
        }

        /* Skipped entirely when both slots are empty, which is how every
         * factory preset ships -- the dry instrument costs nothing for the
         * effects being compiled in. */
        if (!fx_.isBypassed())
            fx_.process(out_l + done, out_r + done, chunk);

        done += chunk;
    }
}

void Moog::process(int16_t* outputs_r, int16_t* outputs_l)
{
    float l[MOOG_BLOCK], r[MOOG_BLOCK];
    processFloat(l, r, I2S_BUFFER_WORDS);

    for (int i = 0; i < I2S_BUFFER_WORDS; ++i) {
        float a = l[i], b = r[i];
        if (a >  0.999969f) a =  0.999969f;
        if (a < -1.0f)      a = -1.0f;
        if (b >  0.999969f) b =  0.999969f;
        if (b < -1.0f)      b = -1.0f;
        outputs_l[i] = (int16_t) (a * 32767.0f);
        outputs_r[i] = (int16_t) (b * 32767.0f);
    }
}

/* ------------------------------------------------------------------------ */
/* MIDI                                                                      */
/* ------------------------------------------------------------------------ */
void Moog::noteOn(int32_t note, int32_t velocity)
{
    /* The keyboard of a Model D produces a gate, not a velocity: every key
     * sounds at the same level however it is struck. A note on with velocity
     * zero is a note off, as always. */
    if (velocity <= 0) { voice_.noteOff((int) note); return; }
    voice_.noteOn((int) note);
}

void Moog::noteOff(int32_t note)
{
    voice_.noteOff((int) note);
}

/*
 * Controllers that are not simply a panel parameter. Everything that is one
 * -- and that is most of them -- goes through the table in moog_params.cpp
 * and never reaches this function; MD_Midi resolves it there.
 *
 * Returns true if the controller was recognised.
 */
bool Moog::processMidiController(uint8_t cc, uint8_t value)
{
    switch (cc) {
        case 64:                       /* sustain pedal */
            voice_.sustainPedal(value >= 64);
            return true;

        case 120:                      /* all sound off */
            resetVoices();
            return true;

        case 123:                      /* all notes off */
            stopVoices();
            return true;

        case 121:                      /* reset all controllers */
            resetControllers();
            return true;

        default: break;
    }

    const int id = moogParamForCc(cc);
    if (id >= 0) {
        setParameter(id, (float) value / 127.0f);
        return true;
    }
    return false;
}

void Moog::setPitchBend(int32_t bend14)
{
    if (bend14 < 0)     bend14 = 0;
    if (bend14 > 16383) bend14 = 16383;
    /* 8192 is the centre; the two halves are not the same width, so they are
     * scaled separately rather than with a single factor. */
    const float n = (bend14 >= 8192)
                      ? (float) (bend14 - 8192) / 8191.0f
                      : (float) (bend14 - 8192) / 8192.0f;
    voice_.setPitchBend(n);
}

void Moog::resetVoices()
{
    voice_.reset();
    /* All sound off means all sound off, so the delay and reverb tails go
     * too. All notes off (stopVoices) deliberately leaves them ringing --
     * cutting a reverb dead when a sequencer sends CC 123 at the end of a bar
     * would be a bug, not tidiness. */
    fx_.reset();
}

void Moog::stopVoices()
{
    voice_.allNotesOff();
}

void Moog::resetControllers()
{
    voice_.sustainPedal(false);
    voice_.setPitchBend(0.0f);
    setParameter(MOOG_MOD_WHEEL, 0.0f);
}

void Moog::setVolume(uint8_t value)
{
    if (value > 127) value = 127;
    setParameter(MOOG_VOLUME, (float) value / 127.0f);
}

uint8_t Moog::getVolume() const
{
    return (uint8_t) (voice_.parameter(MOOG_VOLUME) * 127.0f + 0.5f);
}

/* ------------------------------------------------------------------------ */
/* Programs                                                                  */
/* ------------------------------------------------------------------------ */
void Moog::setProgram(int32_t program)
{
    if (program < 0 || program >= MOOG_NPROGRAMS) return;
    curProgram_ = program;

    /* A preset is a whole front panel, so every control is written. Nothing
     * is left over from the sound before it.
     *
     * Through setParameter and not straight into the voice: the voice holds
     * the shadow that the display and the settings record read, but the
     * effects section keeps its own state and has to be told as well. Writing
     * only the voice here left a preset showing "Delay" on screen while the
     * slot was in fact empty. */
    for (int i = 0; i < MOOG_PARAM_COUNT; ++i)
        setParameter(i, moogPrograms[program].param[i]);
}

void Moog::getProgramName(char* name) const
{
    if (!name) return;
    strncpy(name, moogPrograms[curProgram_].name,
            sizeof(moogPrograms[0].name));
    name[sizeof(moogPrograms[0].name) - 1] = 0;
}

/* ------------------------------------------------------------------------ */
/* Parameters                                                                */
/* ------------------------------------------------------------------------ */
void Moog::setParameter(int32_t index, float value)
{
    /* The voice keeps the shadow of every parameter, including the ones it
     * does not act on itself -- that is what getParameter, the preset writer
     * and the settings record all read from. The effects section then takes
     * the ones that belong to it. */
    voice_.setParameter((int) index, value);
    fx_.setParameter((int) index, value);
}

float Moog::getParameter(int32_t index) const
{
    return voice_.parameter((int) index);
}

void Moog::getParameterName(int32_t index, char* text) const
{
    if (!text) return;
    if (index < 0 || index >= MOOG_PARAM_COUNT) { text[0] = 0; return; }
    snprintf(text, 16, "%s", kMoogParams[index].name);
}

void Moog::getParameterDisplay(int32_t index, char* text) const
{
    moogFormatValue((int) index, getParameter(index), text, 16);
}

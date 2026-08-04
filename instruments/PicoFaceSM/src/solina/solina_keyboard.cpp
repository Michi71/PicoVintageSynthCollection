/*
  solina_keyboard.cpp -- Manual Circuit + Gate Circuit + Sustain Circuits
*/

#include "solina/solina_keyboard.h"

#include <math.h>
#include <string.h>

void SolinaKeyboard::init(float sampleRate)
{
    samplerate_ = sampleRate;
    setCrescendo(crescendo_);
    setSustain(sustainTime_);
    reset();
}

void SolinaKeyboard::reset()
{
    activeCount_ = 0;
    bassActive_  = false;
    bassNote_    = -1;
    bassEnv_     = 0.0f;
    pedal_       = false;
    memset(keyHeld_, 0, sizeof(keyHeld_));
}

/*
 * The sustain circuit charges a capacitor through a resistor -- exponential
 * rise and fall. kt corresponds to the 90% time constant (string-machine,
 * AHDSREnvelope::updateRates).
 */
static inline float solinaRcCoef(float seconds, float sampleRate)
{
    if (seconds <= 0.0f)
        return 1.0f;
    const float kt = 1.0f / 2.2f;
    return 1.0f - expf(-1.0f / (sampleRate * kt * seconds));
}

void SolinaKeyboard::setCrescendo(float seconds)
{
    crescendo_  = seconds;
    attackCoef_ = solinaRcCoef(seconds, samplerate_);
}

void SolinaKeyboard::setSustain(float seconds)
{
    sustainTime_ = seconds;
    releaseCoef_ = solinaRcCoef(seconds, samplerate_);
}

float SolinaKeyboard::groupCenterHz(int g)
{
    const int centre = SOLINA_KEY_FIRST
                       + g * SOLINA_KEYS_PER_GROUP
                       + SOLINA_KEYS_PER_GROUP / 2;
    return SOLINA_NOTE0_HZ * powf(2.0f, ((float) centre) / 12.0f);
}

int SolinaKeyboard::findActive(int note) const
{
    for (int i = 0; i < activeCount_; ++i)
        if (active_[i].note == note)
            return i;
    return -1;
}

/*
 * The releasing key that has decayed furthest. Keys still held, and keys held
 * by the pedal, are not eligible.
 */
int SolinaKeyboard::releaseCandidate() const
{
    int best = -1;
    float lowest = 0.0f;

    for (int i = 0; i < activeCount_; ++i)
    {
        if (active_[i].held || active_[i].sustained)
            continue;
        if (best < 0 || active_[i].env < lowest)
        {
            best = i;
            lowest = active_[i].env;
        }
    }
    return best;
}

int SolinaKeyboard::lowestHeldBassNote() const
{
    for (int n = SOLINA_KEY_FIRST; n <= SOLINA_BASS_LAST; ++n)
        if (keyHeld_[n])
            return n;
    return -1;
}

void SolinaKeyboard::noteOn(int note)
{
    if (note < 0 || note > 127)
        return;

    keyHeld_[note] = true;

    int i = findActive(note);
    if (i < 0)
    {
        if (activeCount_ < SOLINA_MAX_ACTIVE_KEYS)
        {
            i = activeCount_++;
            active_[i].env = 0.0f;
        }
        else
        {
            /*
             * List full. Releasing keys hold their slot until the envelope
             * has decayed -- with a long SUSTAIN or the pedal down that can
             * fill the buffer even though nowhere near 49 keys are pressed.
             * In that case the releasing key that has decayed furthest is
             * reused.
             *
             * Its envelope is carried over rather than zeroed: every key
             * decays with the same time constant, so the smallest value is
             * also the oldest one, and the level does not jump.
             *
             * If every gate really is taken, the new key stays silent -- the
             * original has only 49 gate circuits either.
             */
            i = releaseCandidate();
            if (i < 0)
                return;
        }
        active_[i].note  = (uint8_t) note;
        active_[i].group = (uint8_t) groupOf(note);
    }
    active_[i].held      = true;
    active_[i].sustained = false;

    /* Low-Tone Selection: lowest held note within the bass range */
    const int b = lowestHeldBassNote();
    if (b >= 0)
    {
        bassNote_   = b;
        bassActive_ = true;
    }
}

void SolinaKeyboard::noteOff(int note)
{
    if (note < 0 || note > 127)
        return;

    keyHeld_[note] = false;

    const int i = findActive(note);
    if (i >= 0)
    {
        if (pedal_)
            active_[i].sustained = true;
        else
            active_[i].held = false;
    }

    const int b = lowestHeldBassNote();
    if (b >= 0)
    {
        bassNote_   = b;
        bassActive_ = true;
    }
    else if (!pedal_)
        bassActive_ = false;
}

void SolinaKeyboard::allNotesOff()
{
    memset(keyHeld_, 0, sizeof(keyHeld_));
    for (int i = 0; i < activeCount_; ++i)
    {
        active_[i].held      = false;
        active_[i].sustained = false;
    }
    bassActive_ = false;
}

void SolinaKeyboard::setSustainPedal(bool on)
{
    pedal_ = on;
    if (on)
        return;

    for (int i = 0; i < activeCount_; ++i)
        if (active_[i].sustained)
        {
            active_[i].sustained = false;
            active_[i].held      = false;
        }

    if (lowestHeldBassNote() < 0)
        bassActive_ = false;
}

void SolinaKeyboard::retireFinished()
{
    int w = 0;
    for (int r = 0; r < activeCount_; ++r)
    {
        const Key& k = active_[r];
        const bool gate = k.held || k.sustained;
        if (gate || k.env > 1.0e-5f)
        {
            if (w != r)
                active_[w] = active_[r];
            ++w;
        }
    }
    activeCount_ = w;
}

void SolinaKeyboard::process(SolinaDivider& divider,
                             float bus8[SOLINA_NGROUPS][SOLINA_BLOCK],
                             float bus4[SOLINA_NGROUPS][SOLINA_BLOCK],
                             float* bass8, float* bass16,
                             int count)
{
    for (int g = 0; g < SOLINA_NGROUPS; ++g)
    {
        memset(bus8[g], 0, sizeof(float) * (size_t) count);
        memset(bus4[g], 0, sizeof(float) * (size_t) count);
    }
    memset(bass8,  0, sizeof(float) * (size_t) count);
    memset(bass16, 0, sizeof(float) * (size_t) count);

    const float ac = attackCoef_;
    const float rc = releaseCoef_;

    for (int i = 0; i < count; ++i)
    {
        divider.tick();

        for (int k = 0; k < activeCount_; ++k)
        {
            Key& key = active_[k];

            const bool gate = key.held || key.sustained;
            key.env += ((gate ? 1.0f : 0.0f) - key.env) * (gate ? ac : rc);

            const float env = key.env;
            if (env <= 1.0e-5f)
                continue;

            const int note = key.note;
            const int grp  = key.group;

            const float d8 = divider.step(note);
            if (d8 < 0.45f)
                bus8[grp][i] += solinaBlSaw(divider.phase(note), d8) * env;

            const int n4 = note + 12;
            if (n4 <= 127)
            {
                const float d4 = divider.step(n4);
                if (d4 < 0.45f)
                    bus4[grp][i] += solinaBlSaw(divider.phase(n4), d4) * env;
            }
        }

        /* Bass Circuit -- its own sustain circuit, monophonic */
        {
            const bool gate = bassActive_;
            bassEnv_ += ((gate ? 1.0f : 0.0f) - bassEnv_) * (gate ? ac : rc);

            if (bassEnv_ > 1.0e-5f && bassNote_ >= 12)
            {
                const float d8 = divider.step(bassNote_);
                if (d8 < 0.45f)
                    bass8[i] = solinaBlSaw(divider.phase(bassNote_), d8)
                               * bassEnv_;

                const int n16 = bassNote_ - 12;
                const float d16 = divider.step(n16);
                bass16[i] = solinaBlSaw(divider.phase(n16), d16) * bassEnv_;
            }
        }
    }

    retireFinished();
}

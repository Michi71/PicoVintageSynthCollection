/*
  solina_phaser.cpp -- phaser (an addition of the Behringer remake, not in
                      the original)
*/

#include "solina/solina_phaser.h"

#include <math.h>
#include <string.h>

void SolinaPhaser::init(float sampleRate)
{
    SolinaSineTable::init();

    samplerate_ = sampleRate;
    setRate(SOLINA_PHASER_HZ_MIN);
    setColor(0.5f);
    reset();
}

void SolinaPhaser::reset()
{
    memset(ch_, 0, sizeof(ch_));
    /* The right channel runs a quarter period offset -- that way the notch
     * positions move across the stereo image instead of in the centre. */
    ch_[0].phase = 0.0f;
    ch_[1].phase = 0.25f;
}

void SolinaPhaser::setRate(float hz)
{
    if (hz < SOLINA_PHASER_HZ_MIN) hz = SOLINA_PHASER_HZ_MIN;
    if (hz > SOLINA_PHASER_HZ_MAX) hz = SOLINA_PHASER_HZ_MAX;
    inc_ = hz / samplerate_;
}

void SolinaPhaser::setColor(float c)
{
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    /* Up to 0.7 -- beyond that the feedback gets touchy and whistles. */
    feedback_ = c * 0.7f;
}

/* First-order all-pass, cascaded:  y = a1*x + x1 - a1*y1 */
inline float SolinaPhaser::runStages(Channel& c, float in, float a1) const
{
    float x = in;
    for (int s = 0; s < SOLINA_PHASER_STAGES; ++s)
    {
        const float y = a1 * x + c.x1[s] - a1 * c.y1[s];
        c.x1[s] = x;
        c.y1[s] = y;
        x = y;
    }
    return x;
}

void SolinaPhaser::process(float* l, float* r, int count)
{
    if (!enabled_)
    {
        /* With the phaser off only the LFO keeps running, so that switching
         * it on does not produce a jump. */
        for (int i = 0; i < count; ++i)
            for (int k = 0; k < 2; ++k)
            {
                ch_[k].phase += inc_;
                if (ch_[k].phase >= 1.0f) ch_[k].phase -= 1.0f;
            }
        return;
    }

    float* buf[2] = { l, r };

    for (int k = 0; k < 2; ++k)
    {
        Channel& c = ch_[k];

        /*
         * Determine the corner frequency once per block, from the midpoint of
         * the LFO. The sweep runs exponentially from 200 Hz to 1600 Hz, three
         * octaves -- the usual range of a phaser.
         */
        const float mid = c.phase + inc_ * (float) count * 0.5f;
        const float lfo = SolinaSineTable::lookup(mid - (float) ((int) mid));
        const float oct = (lfo + 1.0f) * 0.5f;          /* 0..1 */
        float fc = SOLINA_PHASER_F_MIN
                   * powf(SOLINA_PHASER_F_MAX / SOLINA_PHASER_F_MIN, oct);

        const float nyq = 0.45f * samplerate_;
        if (fc > nyq) fc = nyq;

        const float t  = tanf((float) M_PI * fc / samplerate_);
        const float a1 = (t - 1.0f) / (t + 1.0f);

        float* p = buf[k];
        for (int i = 0; i < count; ++i)
        {
            const float dry = p[i];
            const float wet = runStages(c, dry + c.fb * feedback_, a1);
            c.fb = wet;

            /* Half dry, half wet -- that is what creates the notches. */
            p[i] = 0.5f * dry + 0.5f * wet;

            c.phase += inc_;
            if (c.phase >= 1.0f) c.phase -= 1.0f;
        }
    }
}

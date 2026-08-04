/*
  solina_registers.cpp -- Gate Output Circuit, Formant Circuit, Bass Circuit
*/

#include "solina/solina_registers.h"
#include "solina/solina_keyboard.h"

#include <math.h>
#include <string.h>

void SolinaRegisters::init(float sampleRate)
{
    samplerate_ = sampleRate;

    for (int g = 0; g < SOLINA_NGROUPS; ++g)
    {
        grp_[g].stringLp8.init(sampleRate);
        grp_[g].stringLp4.init(sampleRate);
        grp_[g].stringHp8.init(sampleRate);
        grp_[g].stringHp4.init(sampleRate);
        grp_[g].stringShelf8.init(sampleRate);
        grp_[g].stringShelf4.init(sampleRate);
        grp_[g].brassLp8.init(sampleRate);
        grp_[g].brassLp4.init(sampleRate);
    }

    bassLp_.init(sampleRate);
    bassDc_.init(sampleRate, 20.0f);
    shaper_.setAmount(1.0f);

    updateCutoffs();
    reset();
}

void SolinaRegisters::reset()
{
    for (int g = 0; g < SOLINA_NGROUPS; ++g)
    {
        grp_[g].stringLp8.clear();
        grp_[g].stringLp4.clear();
        grp_[g].stringHp8.clear();
        grp_[g].stringHp4.clear();
        grp_[g].stringShelf8.clear();
        grp_[g].stringShelf4.clear();
        grp_[g].brassLp8.clear();
        grp_[g].brassLp4.clear();
    }
    bassLp_.clear();
    bassDc_.clear();
}

void SolinaRegisters::setTone(float lowpassSemis, float highpassSemis,
                              float shelfSemis, float shelfDb)
{
    toneLpSemis_ = lowpassSemis;
    toneHpSemis_ = highpassSemis;
    shelfSemis_  = shelfSemis;
    shelfDb_     = shelfDb;
    updateCutoffs();
}

void SolinaRegisters::setFormant(float lowpassSemis)
{
    formantSemis_ = lowpassSemis;
    updateCutoffs();
}

void SolinaRegisters::setShaper(float amount)
{
    shaper_.setAmount(amount);
}

void SolinaRegisters::updateCutoffs()
{
    const float nyq = 0.45f * samplerate_;

    auto limit = [nyq](float f) {
        if (f < 10.0f)  f = 10.0f;
        if (f > nyq)    f = nyq;
        return f;
    };

    for (int g = 0; g < SOLINA_NGROUPS; ++g)
    {
        const float f8 = SolinaKeyboard::groupCenterHz(g);
        const float f4 = f8 * 2.0f;

        const float lp = powf(2.0f, toneLpSemis_ / 12.0f);
        const float hp = powf(2.0f, toneHpSemis_ / 12.0f);
        const float sh = powf(2.0f, shelfSemis_  / 12.0f);
        const float fm = powf(2.0f, formantSemis_ / 12.0f);

        grp_[g].stringLp8.setCutoff(limit(f8 * lp));
        grp_[g].stringHp8.setCutoff(limit(f8 * hp));
        grp_[g].stringShelf8.setHighShelf(limit(f8 * sh), shelfDb_, 0.7071f);

        grp_[g].stringLp4.setCutoff(limit(f4 * lp));
        grp_[g].stringHp4.setCutoff(limit(f4 * hp));
        grp_[g].stringShelf4.setHighShelf(limit(f4 * sh), shelfDb_, 0.7071f);

        grp_[g].brassLp8.setCutoff(limit(f8 * fm));
        grp_[g].brassLp4.setCutoff(limit(f4 * fm));
    }

    /* Bass Circuit: low-pass behind the clipper, fixed tuning */
    bassLp_.setCutoff(limit(320.0f));
}

void SolinaRegisters::process(const float bus8[SOLINA_NGROUPS][SOLINA_BLOCK],
                              const float bus4[SOLINA_NGROUPS][SOLINA_BLOCK],
                              const float* bass8, const float* bass16,
                              float* out, int count)
{
    memset(out, 0, sizeof(float) * (size_t) count);

    const bool needString = viola_ || violin_;
    const bool needBrass  = trumpet_ || horn_;

    for (int g = 0; g < SOLINA_NGROUPS; ++g)
    {
        GroupFilters& f = grp_[g];
        const float* in8 = bus8[g];
        const float* in4 = bus4[g];

        if (needString)
        {
            /* Gate Output Circuit: low-pass, high-pass, treble lift, then
             * the one-sided limiting of the gate circuit. */
            if (viola_)
                for (int i = 0; i < count; ++i)
                {
                    float x = f.stringLp8.process(in8[i]);
                    x = f.stringHp8.process(x);
                    x = f.stringShelf8.process(x);
                    out[i] += shaper_.process(x) * kGainString;
                }
            if (violin_)
                for (int i = 0; i < count; ++i)
                {
                    float x = f.stringLp4.process(in4[i]);
                    x = f.stringHp4.process(x);
                    x = f.stringShelf4.process(x);
                    out[i] += shaper_.process(x) * kGainString;
                }
        }

        if (needBrass)
        {
            /* Formant Circuit: tuned lower, without the treble lift */
            if (trumpet_)
                for (int i = 0; i < count; ++i)
                    out[i] += f.brassLp8.process(in8[i]) * kGainBrass;
            if (horn_)
                for (int i = 0; i < count; ++i)
                    out[i] += f.brassLp4.process(in4[i]) * kGainBrass;
        }
    }

    /* Bass Circuit: clipper (709) and low-pass TR1 */
    if (cello_ || contrabass_)
    {
        const float bv = bassVolume_ * kGainBass;
        for (int i = 0; i < count; ++i)
        {
            float x = 0.0f;
            if (contrabass_) x += bass16[i];
            if (cello_)      x += bass8[i];

            /* Clipper: hard limiting as in the original */
            x *= 2.0f;
            if (x >  1.0f) x =  1.0f;
            if (x < -1.0f) x = -1.0f;

            out[i] += bassDc_.process(bassLp_.process(x)) * bv;
        }
    }
}

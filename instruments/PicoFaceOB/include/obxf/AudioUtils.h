/*
 * OB-Xd was originally written by Vadim Filatov, and then a version
 * was released under the GPL3 at https://github.com/reales/OB-Xd.
 * Subsequently, the product was continued by DiscoDSP and the copyright
 * holders as an excellent closed source product.
 *
 * This repository is a successor to OB-Xd version 2.11.
 * Copyright 2013-2025 by the authors as indicated in the original release,
 * and subsequent authors as per GitHub transaction log.
 *
 * OB-Xf is released under the GNU General Public Licence v3 or later
 * (GPL-3.0-or-later). The license is found in the file "LICENSE"
 * in the root of this repository or at:
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Source code is available at https://github.com/surge-synthesizer/OB-Xf
 */
/*
 * PORTED FOR PicoFaceOB (RP2350). Changes against the OB-Xf original:
 *   - host includes (SynthEngine.h / Utils.h / Constants.h / juce_dsp) replaced by ObxfPort.h
 *   - double temporaries in the per-sample path -> float (8)
 *   - tan() -> ob_tan() (1)
 */

#ifndef OBXF_SRC_ENGINE_AUDIOUTILS_H
#define OBXF_SRC_ENGINE_AUDIOUTILS_H

#include "obxf/ObxfPort.h"


inline static float tpt_lp_unwarped(float &state, float input, float cutoff, float srInv)
{
    cutoff = (cutoff * srInv) * pi;

    float v = (input - state) * cutoff / (1.f + cutoff);
    float res = v + state;

    state = res + v;

    return res;
}

inline static float tpt_lp(float &state, float input, float cutoff, float srInv)
{
    cutoff = ob_tan(cutoff * srInv * pi);

    float v = (input - state) * cutoff / (1.f + cutoff);
    float res = v + state;

    state = res + v;

    return res;
};

inline static float tpt_process(float &state, float input, float cutoff)
{
    float v = (input - state) * cutoff / (1.f + cutoff);
    float res = v + state;

    state = res + v;

    return res;
}

inline static float tpt_process_scaled_cutoff(float &state, float input,
                                              float cutoff_over_onepluscutoff)
{
    float v = (input - state) * cutoff_over_onepluscutoff;
    float res = v + state;

    state = res + v;

    return res;
}

#endif // OBXF_SRC_ENGINE_AUDIOUTILS_H

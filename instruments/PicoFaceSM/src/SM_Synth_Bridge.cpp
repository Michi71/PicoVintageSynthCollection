/*
  SM_Synth_Bridge.cpp -- wiring the Solina engine to the audio subsystem
*/

#include "SM_Synth_Bridge.h"

#include "pico/stdlib.h"
#include "hardware/timer.h"

#ifndef RAM_HOT
#define RAM_HOT(f) __not_in_flash_func(f)
#endif

static inline uint32_t bridge_time_us_32()
{
    return time_us_32();
}

void SM_Synth_Bridge::init()
{
    solina_.setSampleRate((float) SAMPLING_RATE);
    solina_.setVolume(100);
    solina_.setProgram(2);   /* Full Strings */
}

/*
 * The I2S buffer expects interleaved stereo int32. The engine delivers float
 * and already limits softly (solinaSoftClip), so scaling is all that is
 * needed here -- a second limiter would only distort twice.
 */
void RAM_HOT(SM_Synth_Bridge::fill_buffer_i32)(int32_t* out, int length)
{
    const uint32_t t0 = bridge_time_us_32();

    float l[SOLINA_BLOCK];
    float r[SOLINA_BLOCK];

    int done = 0;
    while (done < length)
    {
        int chunk = length - done;
        if (chunk > SOLINA_BLOCK)
            chunk = SOLINA_BLOCK;

        solina_.processFloat(l, r, chunk);

        for (int i = 0; i < chunk; ++i)
        {
            /*
             * The I2S format is S32: the sample has to sit left-aligned in
             * the 32-bit word. Here it is scaled to 24 bits and shifted up by
             * 8 -- the master project uses 16 bits with << 16, and the PIO
             * shifts out 32 bits either way.
             *
             * Omitting this shift is exactly what produced the "no sound at
             * all" symptom: the output measured -111.5 dBFS instead of -15.2.
             *
             * The engine already limits softly to +/-1; the clamp is only a
             * guard against rounding.
             */
            int32_t dl = (int32_t) (l[i] * 8388607.0f);
            int32_t dr = (int32_t) (r[i] * 8388607.0f);
            if (dl >  8388607) dl =  8388607;
            if (dl < -8388608) dl = -8388608;
            if (dr >  8388607) dr =  8388607;
            if (dr < -8388608) dr = -8388608;

            out[(done + i) * 2 + 0] = dl << 8;
            out[(done + i) * 2 + 1] = dr << 8;
        }

        done += chunk;
    }

    /* Load readout: time spent against the time the block represents */
    const uint32_t used = bridge_time_us_32() - t0;
    const uint32_t avail = (uint32_t) ((1000000ull * (uint64_t) length) / SAMPLING_RATE);
    if (avail > 0)
    {
        const int pct = (int) ((used * 100u) / avail);
        if (pct > cpuPeak_)
            cpuPeak_ = pct;
    }
}

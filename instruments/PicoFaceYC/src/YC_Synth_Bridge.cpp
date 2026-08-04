

#include "YC_Synth_Bridge.h"
#include "pico/stdlib.h"

void YC_Synth_Bridge::init() {
    yc_engine_init(state_);
}

void RAM_HOT(YC_Synth_Bridge::fill_buffer)(float* buffer, int length) {
    uint32_t t0 = time_us_32();
    int framesRendered = 0;

    while (framesRendered < length) {
        int chunkLen = length - framesRendered;
        if (chunkLen > kChunkLen) chunkLen = kChunkLen;

        yc_engine_render_block(state_,
                               rotaryState_,
                               vibratoState_,
                               pstate_,
                               reverbState_,
                               scratchL_,
                               scratchR_,
                               (size_t)chunkLen);

        int offset = framesRendered * 2;
        for (int i = 0; i < chunkLen; ++i) {
            buffer[offset + (2 * i)]     = scratchL_[i];
            buffer[offset + (2 * i) + 1] = scratchR_[i];
        }

        framesRendered += chunkLen;
    }

    uint32_t elapsedUs = time_us_32() - t0;
    float budgetUs = (float)length * 1000000.0f / YC_SAMPLE_RATE;
    cpuLoadPercent_ = (elapsedUs / budgetUs) * 100.0f;
    if (cpuLoadPercent_ > cpuLoadPeakPercent_) {
        cpuLoadPeakPercent_ = cpuLoadPercent_;
    }

    if (cpuLoadPercent_ > 100.0f) {
        if (++overloadBlocks_ > 100) {   // ~145 ms dauerhafte Überlast -> Not-Aus
            yc_engine_all_notes_off(state_);
            overloadBlocks_ = 0;
        }
    } else {
        overloadBlocks_ = 0;
    }
}

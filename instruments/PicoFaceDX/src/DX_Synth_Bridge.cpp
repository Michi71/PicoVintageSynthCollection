#include "DX_Synth_Bridge.h"
#include "pico/stdlib.h"
#include "ram_hot.h"

void DX_Synth_Bridge::init() {
    synth_.init();
    fxHost_.init((float)SAMPLE_RATE);
    synth_.applyPatch(synth_.DigiChordPatch());
}

void RAM_HOT(DX_Synth_Bridge::fill_buffer_i32)(int32_t* out, int length) {
    uint32_t t0 = time_us_32();
    // Refresh cached parameters once per block
    synth_.updateCache();

    int framesRendered = 0;
    while (framesRendered < length) {
        // Determine chunk size, capped at DMA_BUFFER_LEN (64)
        int chunkLen = length - framesRendered;
        if (chunkLen > DMA_BUFFER_LEN) {
            chunkLen = DMA_BUFFER_LEN;
        }

        // Render planar audio into scratch buffers
        synth_.renderAudioBlock(scratchL_, scratchR_, chunkLen);

        // Post-mix effects (2 slots: Thru/Distortion/Touch Wah/Chorus/Flanger/Phaser/Delay/Reverb)
        fxHost_.process(scratchL_, scratchR_, chunkLen);

        // Fused pass: soft-clip + master volume + float->int32 + clamp +
        // interleave directly into the DMA output buffer (replaces the old
        // planar->dxBuf copy followed by a separate convert loop in the ISR).
        // Master volume sits AFTER the soft-clip on purpose: it is an output
        // attenuator, so the saturation character of a patch does not change
        // with the volume setting.
        int offset = framesRendered * 2;
        float mv = masterVolCur_;
        const float mvTarget = masterVolTarget_;
        for (int i = 0; i < chunkLen; ++i) {
            mv += (mvTarget - mv) * kMasterVolSlew;
            const float scale = mv * 32767.0f;
            int32_t dl = (int32_t)(softClipSample(scratchL_[i]) * scale);
            int32_t dr = (int32_t)(softClipSample(scratchR_[i]) * scale);
            if (dl < -32768) dl = -32768; else if (dl > 32767) dl = 32767;
            if (dr < -32768) dr = -32768; else if (dr > 32767) dr = 32767;
            out[offset + (2 * i)]     = dl << 16;
            out[offset + (2 * i) + 1] = dr << 16;
        }
        masterVolCur_ = mv;

        framesRendered += chunkLen;
    }

    uint32_t elapsedUs = time_us_32() - t0;
    float budgetUs = (float)length * 1000000.0f / (float)SAMPLE_RATE;
    cpuLoadPercent_ = (elapsedUs / budgetUs) * 100.0f;
    if (cpuLoadPercent_ > cpuLoadPeakPercent_) cpuLoadPeakPercent_ = cpuLoadPercent_;
}

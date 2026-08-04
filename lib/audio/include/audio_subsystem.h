#ifndef __AUDIO_SUBSYSTEM_H__
#define __AUDIO_SUBSYSTEM_H__


#define SAMPLES_PER_BUFFER 64  // MUST match DMA_BUFFER_LEN (LFO block timing)

#define USE_AUDIO_I2S 1
#include "audio_i2s.h"
#include <stdio.h>

audio_buffer_pool_t *init_audio(uint32_t sample_freq = 44100, uint buffer_count = 3);

// Thread-safe enough for DMA-IRQ context: just sets the field;
// audio_i2s.c picks up the new frequency at the next DMA start
// (compares producer_pool->format->sample_freq with shared_state.freq).
void audio_set_sample_freq(uint32_t sample_freq);

#endif // __AUDIO_SUBSYSTEM_H__
#include "audio_subsystem.h"

// This library is intentionally independent of the instrument configuration
// (project_config.h). The I2S pins below default to the values shared by all
// instruments; a board with different I2S wiring overrides both macros via
// the command line or on the audio target.
#ifndef PICO_AUDIO_I2S_DATA_PIN
#define PICO_AUDIO_I2S_DATA_PIN 26
#endif

#ifndef PICO_AUDIO_I2S_CLOCK_PIN_BASE
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#endif

static audio_format_t s_audio_format = {
    .sample_freq = 44100,
    .pcm_format = AUDIO_PCM_FORMAT_S32,
    .channel_count = (audio_channel_t)2};

audio_buffer_pool_t *init_audio(uint32_t sample_freq, uint buffer_count)
{
    s_audio_format.sample_freq = sample_freq;

    static audio_buffer_format_t producer_format = {
        .format = &s_audio_format,
        .sample_stride = 8};

    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format, (int)buffer_count,
                                                                 SAMPLES_PER_BUFFER); // todo correct size
    bool __unused ok;
    const audio_format_t *output_format;
#if USE_AUDIO_I2S
    audio_i2s_config_t config = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = 0,
        .pio_sm = 0};

    output_format = audio_i2s_setup(&s_audio_format, &s_audio_format, &config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    printf("PicoAudio: Audio output format: %d Hz, %d bit, %d channel\n",
           output_format->sample_freq,
           output_format->pcm_format == AUDIO_PCM_FORMAT_S16 ? 16 : 32,
           output_format->channel_count);

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    { // initial buffer data
        audio_buffer_t *buffer = take_audio_buffer(producer_pool, true);
        int32_t *samples = (int32_t *)buffer->buffer->bytes;

        printf("PicoAudio: Initializing audio buffer with %d samples\n",
               buffer->buffer->size / sizeof(int32_t));
        for (uint i = 0; i < buffer->max_sample_count; i++)
        {
            samples[i * 2 + 0] = 0;
            samples[i * 2 + 1] = 0;
        }
        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(producer_pool, buffer);
    }
    audio_i2s_set_enabled(true);
#elif USE_AUDIO_PWM
    output_format = audio_pwm_setup(&s_audio_format, -1, &default_mono_channel_config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }
    ok = audio_pwm_default_connect(producer_pool, false);
    assert(ok);
    audio_pwm_set_enabled(true);
#elif USE_AUDIO_SPDIF
    output_format = audio_spdif_setup(&s_audio_format, &audio_spdif_default_config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }
    // ok = audio_spdif_connect(producer_pool);
    ok = audio_spdif_connect(producer_pool);
    assert(ok);
    audio_spdif_set_enabled(true);
#endif
    return producer_pool;
}

void audio_set_sample_freq(uint32_t sample_freq)
{
    s_audio_format.sample_freq = sample_freq;
}

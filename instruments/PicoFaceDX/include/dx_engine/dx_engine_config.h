#pragma once
// Minimal engine configuration for the RP2350 port (replaces ESP32 config.h -- no GPIO/display/SD definitions here, those belong to the host project).
#define SAMPLE_RATE 44100
#define DMA_BUFFER_LEN 64
#define MAX_VOICES 8
#define MAX_VOICES_PER_NOTE 2
#define TIMING_CORRECTION 1.0f

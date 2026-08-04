

// include/yc_engine/yc_core.h
#pragma once
#include <stdint.h>
#include <cstddef>

// 16 Stimmen = sicherer RP2350-Kompromiss mit CPU-Reserve, musikalisch ausreichend für eine Combo-Orgel
constexpr int YC_MAX_VOICES = 16;
constexpr int YC_NUM_FOOTAGES = 9;
constexpr float YC_SAMPLE_RATE = 44100.0f;
constexpr int YC_ATTACK_SAMPLES = 88;
constexpr int YC_RELEASE_SAMPLES = 128;
constexpr float YC_2PI = 2.0f * 3.14159265358979323846f;

// Parameter-IDs fuer yc_engine_set_param() und IPC-Panel-Updates.
// Lebt hier (statt in ipc.h), damit die host-kompilierbare Engine sie nutzen
// kann; ipc.h inkludiert diesen Header, Werte bleiben identisch.
enum YcParamId : uint8_t {
    YC_PARAM_WAVE, YC_PARAM_OCTAVE, YC_PARAM_FOOTAGE_16, YC_PARAM_FOOTAGE_513,
    YC_PARAM_FOOTAGE_8, YC_PARAM_FOOTAGE_4, YC_PARAM_FOOTAGE_223, YC_PARAM_FOOTAGE_2,
    YC_PARAM_FOOTAGE_135, YC_PARAM_FOOTAGE_113, YC_PARAM_FOOTAGE_1, YC_PARAM_PERC_ON,
    YC_PARAM_PERC_TYPE, YC_PARAM_PERC_LENGTH, YC_PARAM_VIBCHO_SELECT,
    YC_PARAM_VIBCHO_DEPTH, YC_PARAM_DISTORTION, YC_PARAM_REVERB, YC_PARAM_VOLUME
};

struct yc_voice_t {
    bool active = false;
    bool sustained = false;
    bool releasing = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    float vel_gain = 0.0f;
    float base_freq = 0.0f; // Gecachte Grundfrequenz, einmalig bei Note-On berechnet statt pro Sample - Performance-Fix
    float phase = 0.0f; // Bleibt erhalten für potentielle zukünftige einfache Oszillator-Nutzung (z.B. LFO pro Voice), wird von tonegen nicht mehr primär genutzt.
    float footage_phase[YC_NUM_FOOTAGES] = {0.0f}; // Eine Phase pro Fusslage, da jede Fusslage unterschiedlich schnell durch die Wavetable läuft.
    float phase_inc[YC_NUM_FOOTAGES] = {0.0f}; // Gecachtes Phasen-Inkrement pro Fusslage, einmalig bei Note-On berechnet statt pro Sample - Performance-Fix.
    float amp = 0.0f;
};

struct yc_engine_state_t {
    yc_voice_t voices[YC_MAX_VOICES];
    uint8_t active_idx[YC_MAX_VOICES] = {0};
    uint8_t active_count = 0;
    uint8_t wave;
    int8_t octave;
    uint8_t footage[YC_NUM_FOOTAGES];
    uint8_t perc_on;
    uint8_t perc_type;
    uint8_t perc_length;
    uint8_t vibcho_select;
    uint8_t vibcho_depth;
    uint8_t rotary_speed;
    uint8_t distortion;
    uint8_t reverb;
    uint8_t volume;
    float vol_gain = 1.0f;
    bool sustain_held;
};

constexpr float YC_FOOTAGE_PITCH_RATIO[YC_NUM_FOOTAGES] = { 0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
constexpr int YC_WAVETABLE_SIZE = 2048;
constexpr int YC_NUM_WAVE_TYPES = 5;


// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "JV_Bridge.h"

#include <cmath>
#include <cstring>

#include "jv_calibration.h"

// The ROM blob is produced at configure time by tools/jv_extract/jv_make_blob.py
// and pulled in with .incbin; see instrument.cmake. Nothing ROM-derived is in
// the repository.
extern "C" {
extern const uint8_t jv_wave_blob[];
extern const uint8_t jv_wave_blob_end[];
extern const uint8_t jv_rom2_blob[];
extern const uint8_t jv_rom2_blob_end[];
}

void JV_Bridge::init() {
    const jv::RomView rom{
        jv_wave_blob, (size_t)(jv_wave_blob_end - jv_wave_blob),
        jv_rom2_blob, (size_t)(jv_rom2_blob_end - jv_rom2_blob)};
    engine_.init(rom, kSampleRate);
    engine_.selectPatch(1, 0);   // bank A, first patch
}

bool JV_Bridge::selectPatch(int bank, int index) {
    // Changing patch mid-note would leave sounding voices pointing at the old
    // tone bytes, which the engine copies at note-on -- so they are safe, but a
    // patch change on the JV silences the keyboard anyway.
    engine_.allNotesOff();
    const bool ok = engine_.selectPatch(bank, index);
    updateBend();   // the new patch may bend by a different amount
    return ok;
}

void JV_Bridge::setVolume(uint8_t percent) {
    if (percent > 100) percent = 100;
    // Perceptual rather than linear: squared taper, -inf at 0.
    const float x = percent * 0.01f;
    gain_ = x * x;
}

// The JV's own tone-level law, so a MIDI volume behaves like the machine's
// level controls rather than a generic square taper.
void JV_Bridge::setMidiVolume(uint8_t v) {
    if (v > 127) v = 127;
    if (v == 0) { midiGain_ = 0.0f; return; }
    const float x = v * 0.25f;
    const int i = (int)x;
    const float db = (i >= 31) ? JV_TVA_LEVEL_DB[31]
                              : JV_TVA_LEVEL_DB[i] +
                                (JV_TVA_LEVEL_DB[i + 1] - JV_TVA_LEVEL_DB[i]) * (x - (float)i);
    midiGain_ = (db <= -200.0f) ? 0.0f : powf(10.0f, db * (1.0f / 20.0f));
}

// Constant power, so a pan sweep does not dip in the middle. The manual puts 0
// at the left end, 64 centre and 127 right.
void JV_Bridge::setMidiPan(uint8_t v) {
    if (v > 127) v = 127;
    const float a = (float)v * (1.5707963f / 127.0f);
    panL_ = cosf(a) * 1.41421356f;
    panR_ = sinf(a) * 1.41421356f;
}

void JV_Bridge::setBendRangeOverride(int semis) {
    bendOverride_ = semis;
    updateBend();
}

void JV_Bridge::setRpnTuneCents(float cents) {
    rpnCents_ = cents;
    updatePitch();
}

void JV_Bridge::setMasterTune(int cents) {
    if (cents < -50) cents = -50;
    if (cents > 50) cents = 50;
    tuneCents_ = cents;
    updatePitch();
}

void JV_Bridge::setPitchBend(int16_t bend) {
    bend_ = bend;
    updateBend();
}

// The range comes from the patch, and up and down are separate: +-2 is only the
// common case. Re-derived on every patch change as well, since the wheel may
// already be off centre when the patch switches.
void JV_Bridge::updateBend() {
    float semis;
    if (bendOverride_ >= 0) semis = (float)bendOverride_;
    else semis = (bend_ >= 0) ? (float)engine_.bendUpSemis()
                              : -(float)engine_.bendDownSemis();
    bendRatio_ = powf(2.0f, (bend_ / 8192.0f) * semis / 12.0f);
    updatePitch();
}

void JV_Bridge::updatePitch() {
    engine_.setPitchTrim(bendRatio_ * powf(2.0f, (tuneCents_ + rpnCents_) / 1200.0f));
}

void JV_Bridge::fillBufferI32(int32_t* out, int frames) {
    int done = 0;
    while (done < frames) {
        int chunk = frames - done;
        if (chunk > kBlock) chunk = kBlock;
        engine_.render(bufL_, bufR_, chunk);

        for (int i = 0; i < chunk; ++i) {
            float l = bufL_[i] * gain_ * midiGain_ * panL_;
            float r = bufR_[i] * gain_ * midiGain_ * panR_;

            // Same rational soft clip the other instruments use: transparent
            // below the knee, no hard corner above it.
            const float a = 0.9f, range = 1.0f - a;
            if (l > a)       l =  a + range * (1.0f - 1.0f / (1.0f + (l - a) / range));
            else if (l < -a) l = -a - range * (1.0f - 1.0f / (1.0f + (-l - a) / range));
            if (r > a)       r =  a + range * (1.0f - 1.0f / (1.0f + (r - a) / range));
            else if (r < -a) r = -a - range * (1.0f - 1.0f / (1.0f + (-r - a) / range));

            int32_t dl = (int32_t)(l * 32767.0f);
            int32_t dr = (int32_t)(r * 32767.0f);
            if (dl >  32767) dl =  32767; else if (dl < -32768) dl = -32768;
            if (dr >  32767) dr =  32767; else if (dr < -32768) dr = -32768;

            // TWO int32 words per frame, one per channel, sample in the upper
            // half. The interface comment calls this "one int32 word per frame
            // (packed stereo)", which is wrong -- every instrument in the tree
            // writes it this way and the PIO expects it.
            out[2 * (done + i)]     = dl << 16;
            out[2 * (done + i) + 1] = dr << 16;
        }
        done += chunk;
    }
}

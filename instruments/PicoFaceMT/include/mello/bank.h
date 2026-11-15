// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// Sample bank layout. The tables themselves are generated from the compressed
// SF2 files by tools/sf2_to_cpp.py and live in data/.
#pragma once
#include <cstdint>

namespace mello {

// One key range played from one recording. Offsets are sample indices into
// Instrument::pcm. A looped zone plays start..loopEnd once and then repeats
// loopStart..loopEnd; pcm[loopEnd + k] equals pcm[loopStart + k] for the few
// samples interpolation reads ahead. loopStart == loopEnd marks a one-shot,
// which stops at end.
struct Zone {
    uint8_t  keyLo, keyHi;   // MIDI notes covered, inclusive
    uint8_t  root;           // MIDI note the recording sounds at
    int8_t   cents;          // pitch correction of the recording
    uint32_t start, end;
    uint32_t loopStart, loopEnd;
};

struct Instrument {
    const char*    name;       // as the tape set is called
    const char*    label;      // short form for a narrow display
    const int16_t* pcm;        // 16-bit mono
    const Zone*    zones;      // sorted by keyLo
    uint8_t        numZones;
    uint32_t       sampleRate;
};

extern const Instrument kBank[];
extern const int        kBankSize;

// The key click: one short recording, played untransposed at every key-down.
// kClickPcm holds kClickLength samples plus one more for interpolation;
// a length of zero means the set has none.
extern const int16_t  kClickPcm[];
extern const uint32_t kClickLength;
extern const uint32_t kClickRate;

}  // namespace mello

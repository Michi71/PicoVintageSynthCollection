// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_params.h -- every editable M1 parameter, described once: where its byte
// sits, its range, how it is shown. The edit pages, the SysEx parameter
// change and the display all work from these tables.
//
// Sources: the owner's manual, MIDI implementation TABLE 1 (program), TABLE 2
// (combination), TABLE 3 (global), *11 (effect parameters), TABLE 5/6 (edit
// pages); cross-checked against the OS's edit records (doc/ENGINE.md).
#ifndef M1_PARAMS_H
#define M1_PARAMS_H

#include <stddef.h>
#include <stdint.h>

namespace m1 {

enum class PK : uint8_t {
    U,        // unsigned byte, lo..hi
    S,        // signed byte, lo..hi
    Bit,      // one bit (aux = bit), Off/On
    Field,    // bits aux..aux+width-1 (width in hi-lo+1 range), lo..hi
    SwPol,    // EG time switch & polarity of segment aux: Off / + / -
    Word,     // 16-bit little-endian over two bytes, lo..hi
    Char,     // name character
    Note,     // MIDI note, C-1..G9
    Multi,    // multisound 0..99 (in a drum program: kit 1..4)
    Octave,   // -1..1 = 16' 8' 4'
    OscMode,  // Single / Double / Drums
    Wave,     // MG wave in bits 0-1
    Pan,      // 0..13: A, 9:1 .. 1:9, B, C, C+D, D
    TimPan,   // timbre pan: 0..13, bit 7 = INS (instrument pan of a drum kit)
    FxType,   // 0..33
    FxPan,    // Out 3/4 panpot: 0 off, 1..101 = R .. L
    Placement,// effect switch byte: bit 4 serial
    Program,  // 0..99 with name
    DrumSnd,  // 0..44 with name
    CombiType,
    Scale,
    Tune10,   // drum tune, signed, 10 cents per step
    Hz,       // effect LFO speed byte, 0..216
    Freq3,    // 0..2 with a label set (aux: 0 low shelf, 1 high shelf)
    Toggle,   // MIDI filter bits / timbre on (bit aux, inverted when hi < 0)
};

struct ParamDef {
    const char* name;
    uint8_t     offset;   // byte offset in the block the page edits
    PK          kind;
    int16_t     lo, hi;
    uint8_t     aux;
};

struct PageDef {
    const char*     title;
    const ParamDef* params;
    uint8_t         count;
    uint8_t         flags;    // kPageDoubleOnly, kPageFx1, kPageFx2, kPageTimbre
};

enum : uint8_t {
    kPageDoubleOnly = 1,   // OSC 2 pages: hidden in Single and Drums programs
    kPageFx1 = 2,          // parameter list depends on the effect 1 type
    kPageFx2 = 4,
    kPageTimbre = 8,       // repeated for timbres 1..8 (offset + 11 t)
    kPageNoDrums = 16,     // hidden in Drums programs
};

// Program: 143-byte block. Combination: 124 bytes. Global: 21 bytes. Kit
// instrument: 7 bytes.
const PageDef* programPages(int& count);
const PageDef* combiPages(int& count);
const PageDef* globalPages(int& count);
const PageDef* kitPages(int& count);

// The eight parameter bytes of an effect type; entries with a null name are
// unused or the high byte of a word.
const ParamDef* effectParams(int fxType);

// Effect-section block within a program (38) or combination (11).
enum : uint8_t { kProgFx = 38, kCombiFx = 11 };

// Raw value of a parameter (signed where the kind is signed).
int  paramGet(const ParamDef& p, const uint8_t* block);
// Clamped store; returns the value actually stored.
int  paramSet(const ParamDef& p, uint8_t* block, int value);
// Step by delta (encoder detents); returns true if the value changed.
bool paramStep(const ParamDef& p, uint8_t* block, int delta);
// Text for the display (fits 10 characters) and a 0..1 position (-1 none).
void paramFormat(const ParamDef& p, const uint8_t* block, char* out, size_t n, float* norm);

// Names the display needs from ROM data, set once at start.
void setNameSources(const char* const* multisoundNames, const char* const* drumNames,
                    const uint8_t* programs);

extern const char* const kFxNames[34];

} // namespace m1

#endif // M1_PARAMS_H

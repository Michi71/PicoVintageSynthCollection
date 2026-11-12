// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// vs_params.h -- the editable parameters of a Prophet VS program, as a flat
// list over the program's 116 fields (provs/preset_map.h has the field map).
//
// The list is grouped the way the VS's panel is: oscillators, filter, the
// three envelopes, the two LFOs, the modulation sources with their
// destination switches, chorus, voice and keyboard modes, pan, arpeggiator and
// the name. Every group holds an even number of entries, padded with a blank
// one where needed, so a panel that shows two values per screen never
// straddles two groups.
#ifndef VS_PARAMS_H
#define VS_PARAMS_H

#include <cstddef>
#include <cstdint>

namespace vs {

enum class Kind : uint8_t {
    Blank,    // padding, nothing to show
    Num,      // min..max, shown as a number
    Signed7,  // 7-bit two's complement in the field
    Signed8,  // 8-bit two's complement in the field
    Enum,     // min..max, shown through names[]
    Bit,      // one bit of the field (mask in 'bit'): Off / On
    Wave,     // wave number 0..127
    Note,     // MIDI note, shown as a note name
    Char,     // one character of the name
    Ms4,      // counts of 4 ms, shown in milliseconds
};

struct ParamDef {
    const char* name;            // nullptr for Kind::Blank
    uint8_t     field;           // index into the 116 fields
    Kind        kind;
    int16_t     min, max;
    const char* const* names;    // Kind::Enum
    uint8_t     bit;             // Kind::Bit: mask within the field
};

int paramCount();
const ParamDef& param(int index);

int groupCount();
const char* groupName(int group);
int groupFirst(int group);       // index of the group's first parameter (even)
int groupOf(int index);

// The names of all groups, for a list view.
const char* const* groupNames();

// Changes the value by 'delta' steps, clamped to its range. Returns true if
// the field changed.
bool paramStep(const ParamDef& p, uint8_t* fields, int delta);

// Formats the value into 'text'. 'norm' receives 0..1 for a value with a
// position and a negative number for one without (an enum, a switch).
void paramFormat(const ParamDef& p, const uint8_t* fields, char* text, size_t size, float* norm);

// --- Parameters over MIDI (owner's manual table 14-1, firmware @0x50AC) ----
// A parameter is selected by a 14-bit number: MSB 0 with the field number as
// LSB, or MSB 1 with a switch bit as LSB - 00-09 and 10-19 the destinations
// of LFO 1 and 2, 20-2E pressure, 30-34 velocity, 40-43 keyboard, 50-51
// filter envelope, 60-62 mod wheel. The data is not the value but the
// position of the VS's slider in eight bits: value = minimum + position *
// (number of values) / 256, and a switch is on from 128.
const ParamDef* paramByMidi(int number);
int paramMidiNumber(const ParamDef& p);          // -1 for a blank
bool paramSetSlider(const ParamDef& p, uint8_t* fields, int position);
int paramSlider(const ParamDef& p, const uint8_t* fields);   // the lowest position that gives the value

} // namespace vs

#endif // VS_PARAMS_H

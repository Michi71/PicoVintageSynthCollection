// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef __MIDI_INPUT_USB__H__
#define __MIDI_INPUT_USB__H__

#include <pico/stdlib.h>
#include "project_config.h"
#include "tusb.h"
#include <stdint.h>

class MIDIInputUSB {
public:
    using NoteOnCallback       = void (*)(uint8_t, uint8_t, uint8_t);
    using NoteOffCallback      = void (*)(uint8_t, uint8_t, uint8_t);
    using CCCallback           = void (*)(uint8_t, uint8_t, uint8_t);
    using ProgramChangeCallback = void (*)(uint8_t, uint8_t);
    using PitchBendCallback    = void (*)(uint16_t, uint8_t);
    using ChannelPressureCallback = void (*)(uint8_t, uint8_t);
    using RealtimeCallback     = void (*)(uint8_t);
    using SysExCallback        = void (*)(const uint8_t*, uint16_t);
    using ActivityCallback     = void (*)(void);

    MIDIInputUSB() = default;

    void process();

    void setNoteOnCallback(NoteOnCallback cb)            { MIDINoteOnCallback = cb; }
    void setNoteOffCallback(NoteOffCallback cb)          { MIDINoteOffCallback = cb; }
    void setCCCallback(CCCallback cb)                    { MIDICCCallback = cb; }
    void setProgramChangeCallback(ProgramChangeCallback cb) { MIDIProgramChangeCallback = cb; }
    void setPitchBendCallback(PitchBendCallback cb)      { MIDIPitchBendCallback = cb; }
    void setChannelPressureCallback(ChannelPressureCallback cb) { MIDIChannelPressureCallback = cb; }
    void setRealtimeCallback(RealtimeCallback cb)        { MIDIRealtimeCallback = cb; }
    void setSysExCallback(SysExCallback cb)              { MIDISysExCallback = cb; }
    void setActivityCallback(ActivityCallback cb)        { MIDIActivityCallback = cb; }

private:
    void sysexStart();
    void sysexAppend(const uint8_t* b, uint8_t n);
    void sysexFinish();

    NoteOnCallback       MIDINoteOnCallback       = nullptr;
    NoteOffCallback      MIDINoteOffCallback      = nullptr;
    CCCallback           MIDICCCallback           = nullptr;
    ProgramChangeCallback MIDIProgramChangeCallback = nullptr;
    PitchBendCallback    MIDIPitchBendCallback    = nullptr;
    ChannelPressureCallback MIDIChannelPressureCallback = nullptr;
    RealtimeCallback     MIDIRealtimeCallback     = nullptr;
    SysExCallback        MIDISysExCallback        = nullptr;
    ActivityCallback     MIDIActivityCallback     = nullptr;

    // 512 bytes. It was 256, sized for a reface DX common block of 51; the
    // D-50 raises the bar -- its own bulk dumps carry 256 DATA bytes per
    // message, which with the Roland header, address, checksum and F7 is a
    // 266-byte message, and an oversized one is dropped silently in
    // sysexFinish(). 512 leaves room above the largest thing any instrument
    // here sends or receives.
    uint8_t  _syx[512]     = {0};
    uint16_t _syxLen       = 0;
    bool     _syxActive    = false;
    bool     _syxOverflow  = false;
};

#endif // __MIDI_INPUT_USB__H__

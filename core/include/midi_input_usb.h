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
    RealtimeCallback     MIDIRealtimeCallback     = nullptr;
    SysExCallback        MIDISysExCallback        = nullptr;
    ActivityCallback     MIDIActivityCallback     = nullptr;

    uint8_t  _syx[64]      = {0};
    uint16_t _syxLen       = 0;
    bool     _syxActive    = false;
    bool     _syxOverflow  = false;
};

#endif // __MIDI_INPUT_USB__H__

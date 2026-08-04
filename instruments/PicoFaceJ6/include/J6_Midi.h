// J6_Midi: MIDI front end for the Roland Juno-60 emulation.
//
// Takes incoming events, filters them by receive channel and passes them on to
// the audio producer through the IPC ring in j6_ipc.h, where J6_Synth_Bridge
// consumes them.
//
// Controller numbers are not listed here. Every panel control carries its own
// CC in the table in juno_params.cpp, and this class resolves them through
// junoParamForCc() -- so adding a control to the panel gives it a controller
// number without anything in the MIDI front end changing. Only the handful of
// messages that are not panel controls (sustain, the mode messages, program
// change, pitch bend) are handled explicitly below.

#ifndef J6_MIDI_H
#define J6_MIDI_H

#include <stdint.h>

// paramId used to carry a Program Change / preset selection through IPC.
// Chosen above JUNO_PARAM_COUNT so it cannot collide with a real parameter.
constexpr uint8_t J6_PARAM_PROGRAM = 0x7F;

// Special rxChannel value meaning "accept all channels" (Omni mode).
constexpr uint8_t J6_MIDI_OMNI = 0x10;

class J6_Controller;   // display shadow; see setUiSink()

class J6_Midi {
public:
    // Initialize the receiver. rxChannel: 0..15 = channel 1..16,
    // J6_MIDI_OMNI (0x10) = Omni. Default is Omni.
    void init(uint8_t rxChannel = J6_MIDI_OMNI);

    // Where to mirror parameter changes so the display shows what a
    // controller just sent rather than what the encoder last set. Optional:
    // with no sink the engine still follows, only the screen does not.
    void setUiSink(J6_Controller* ui) { ui_ = ui; }

    // MIDI event entry points (channel is the raw MIDI channel 0..15).
    void onNoteOn(uint8_t note, uint8_t vel, uint8_t ch);
    void onNoteOff(uint8_t note, uint8_t vel, uint8_t ch);
    void onControlChange(uint8_t cc, uint8_t val, uint8_t ch);
    void onProgramChange(uint8_t program, uint8_t ch);
    void onPitchBend(uint16_t value, uint8_t ch); // 0..16383, center 8192

    // Receiver channel accessors. setRxChannel clamps to 0..J6_MIDI_OMNI.
    uint8_t getRxChannel() const;
    void    setRxChannel(uint8_t ch);

private:
    // Returns true if the given MIDI channel should be accepted.
    inline bool channelMatches(uint8_t ch) const {
        return (rxChannel_ == J6_MIDI_OMNI) || (rxChannel_ == ch);
    }

    uint8_t        rxChannel_ = J6_MIDI_OMNI;
    J6_Controller* ui_ = nullptr;
};

#endif // J6_MIDI_H

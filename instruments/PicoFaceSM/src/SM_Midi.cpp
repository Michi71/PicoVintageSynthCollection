// =====================================================================
// SM_Midi.cpp -- MIDI front end for PicoFaceSM
//
// Takes incoming events, filters them by receive channel and passes them on
// to the audio producer through the IPC ring.
//
// Recognised and implemented:
//   Note On / Note Off    keyboard C2..C6 (36..84); notes outside that range
//                         are folded in by octaves. Note On with velocity 0
//                         counts as Note Off.
//   CC 7   Volume
//   CC 64  Sustain pedal  >= 64 = on
//   CC 72  Sustain        decay time of the sustain circuit
//   CC 73  Crescendo      attack time of the sustain circuit
//   CC 80..85             register tabs Contrabass..Horn
//   CC 93  Ensemble       modulator circuits on/off
//   CC 95  Phaser         on/off (not in the original, from the Behringer
//                         remake)
//   CC 120 All Sound Off
//   CC 121 Reset All Ctrl pedal off, pitch bend to centre
//   CC 123 All Notes Off
//   Program Change        0..7, modulo above that
//   Pitch Bend            +/- 2 semitones, acts on the master oscillator
//
// Not implemented:
//   Velocity              the original's gate circuit knows only open/closed
//   CC 1  Modulation      the Solina has no playable vibrato
// =====================================================================

#include "SM_Midi.h"
#include "sm_ipc.h"
#include "solina/solina.h"

// ---------------------------------------------------------------------
// The Solina has 49 keys (C2..C6). Notes outside that range are folded in by
// octaves -- identically for Note On and Note Off, so that a pair always
// lands on the same key.
// ---------------------------------------------------------------------
static uint8_t foldToRange(uint8_t note)
{
    while (note < SOLINA_KEY_FIRST) note += 12;
    while (note > SOLINA_KEY_LAST)  note -= 12;
    return note;
}

void SM_Midi::init(uint8_t rxChannel)
{
    setRxChannel(rxChannel);
}

void SM_Midi::onNoteOn(uint8_t note, uint8_t vel, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    if (vel == 0) {
        ipc_send_note_off(foldToRange(note));
        return;
    }
    ipc_send_note_on(foldToRange(note), vel);
}

void SM_Midi::onNoteOff(uint8_t note, uint8_t vel, uint8_t ch)
{
    (void) vel;   // no release velocity
    if (!channelMatches(ch)) return;

    ipc_send_note_off(foldToRange(note));
}

void SM_Midi::onControlChange(uint8_t cc, uint8_t val, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    switch (cc) {
        case 7:     // Volume
        case 64:    // Sustain pedal
        case 120:   // All Sound Off
        case 123:   // All Notes Off
            ipc_send_cc(cc, val);
            break;

        // Register tabs on the general purpose controllers 80..85, so the
        // front panel can be driven remotely.
        case 80: ipc_send_param(SOLINA_CONTRABASS, val >= 64 ? 1000 : 0); break;
        case 81: ipc_send_param(SOLINA_CELLO,      val >= 64 ? 1000 : 0); break;
        case 82: ipc_send_param(SOLINA_VIOLA,      val >= 64 ? 1000 : 0); break;
        case 83: ipc_send_param(SOLINA_VIOLIN,     val >= 64 ? 1000 : 0); break;
        case 84: ipc_send_param(SOLINA_TRUMPET,    val >= 64 ? 1000 : 0); break;
        case 85: ipc_send_param(SOLINA_HORN,       val >= 64 ? 1000 : 0); break;

        // Ensemble on the usual chorus switch, phaser on CC 95
        case 93: ipc_send_param(SOLINA_ENSEMBLE,   val >= 64 ? 1000 : 0); break;
        case 95: ipc_send_param(SOLINA_PHASER,     val >= 64 ? 1000 : 0); break;

        // Envelope as continuous values (0..127 -> 0..1000 per mille)
        case 73: ipc_send_param(SOLINA_CRESCENDO, (uint16_t)(val * 1000 / 127)); break;
        case 72: ipc_send_param(SOLINA_SUSTAIN,   (uint16_t)(val * 1000 / 127)); break;

        case 121:   // Reset All Controllers
            ipc_send_cc(64, 0);
            ipc_send_pitch_bend(8192);
            break;

        default:
            break;
    }
}

void SM_Midi::onProgramChange(uint8_t program, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    ipc_send_param(SM_PARAM_PROGRAM,
                   (uint16_t) (program % SOLINA_NPROGRAMS));
}

void SM_Midi::onPitchBend(uint16_t value, uint8_t ch)
{
    if (!channelMatches(ch)) return;

    if (value > 16383) value = 16383;
    ipc_send_pitch_bend(value);
}

uint8_t SM_Midi::getRxChannel() const
{
    return rxChannel_;
}

void SM_Midi::setRxChannel(uint8_t ch)
{
    if (ch > SM_MIDI_OMNI)
        ch = SM_MIDI_OMNI;
    rxChannel_ = ch;
}

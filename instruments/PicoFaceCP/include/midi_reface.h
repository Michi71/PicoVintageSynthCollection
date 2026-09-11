// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/midi_reface.h
//
// PicoFaceCP — Yamaha reface CP compatible MIDI layer (Data List p.11-14). The
// reface dialect itself - SYSTEM block, active sensing, channel filter,
// exclusive framing, transmit - is picoface::RefaceMidiBase in the core; this
// is the CP on top of it:
//   * TG (Tone Generator) block (base 30 00 00)
//   * the CC map, panel CCs gated by the SYSTEM "MIDI Control" setting
//   * front-panel mirroring as CCs (txFxParam / txFxMode / txInstrument)
// Engine/FX mutations are forwarded through IPC helpers (ipc.h).
// Implementation: src/midi_reface.cpp
//
#ifndef __MIDI_REFACE_H__
#define __MIDI_REFACE_H__

#include <stdint.h>

#include "picoface/reface_midi.h"

class mdaEPiano;       // engine (Core 0 image; mutations via ipc.h)
class RefaceCpChain;   // FX chain (Core 0 image; mutations via ipc.h)

class RefaceMidi final : public picoface::RefaceMidiBase {
public:
  // The CP's own SYSTEM address; the shared ones are in the base.
  enum CpSysAddr : uint8_t {
    SYS_SUSTAIN_SEL    = 0x0B
  };

  // TG block indices (base address 30 00 00)
  enum TgAddr : uint8_t {
    TG_VOLUME       = 0x00,
    TG_WAVE_TYPE    = 0x02,
    TG_DRIVE        = 0x03,
    TG_FX1_TYPE     = 0x04,
    TG_FX1_DEPTH    = 0x05,
    TG_FX1_RATE     = 0x06,
    TG_FX2_TYPE     = 0x07,
    TG_FX2_DEPTH    = 0x08,
    TG_FX2_SPEED    = 0x09,
    TG_FX3_TYPE     = 0x0A,
    TG_FX3_DEPTH    = 0x0B,
    TG_FX3_TIME     = 0x0C,
    TG_REVERB_DEPTH = 0x0D,
    TG_BLOCK_SIZE   = 0x10
  };

  RefaceMidi();

  void init(mdaEPiano* ep, RefaceCpChain* fx);

  // Front panel -> MIDI OUT (CC), gated by MIDI Control setting.
  void txFxParam(uint8_t fxParam, float v01);  // FxParam id (ipc.h) -> CC 18/19/81/86/87/89/90/91
  void txFxMode(uint8_t fxMode, int mode);     // FxMode id (ipc.h) -> CC 17/85/88 (0/64/127)
  void txInstrument(int instr);                // -> CC 80 (0,25,51,76,102,127)
  void txProgram(int preset);                  // -> Program Change 0..7 (factory preset)

private:
  // RefaceMidiBase hooks
  void engineNoteOn(uint8_t note, uint8_t vel) override;
  void engineNoteOff(uint8_t note) override;
  void enginePitchBend(uint16_t bend14) override;
  void engineControlChange(uint8_t cc, uint8_t val) override;
  void engineProgramChange(uint8_t program) override;
  void engineAllSoundOff() override;
  void engineAllNotesOff() override;
  void engineMasterTune(uint16_t raw) override;
  int  uiOctave() const override;
  void applyModelSystemParam(uint8_t addr, uint8_t v) override;
  void applyParam(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) override;
  uint8_t readParam(uint8_t ah, uint8_t am, uint8_t al, uint8_t* out) override;
  void onDumpRequest(uint8_t ah, uint8_t am, uint8_t al) override;
  void onBulkBlock(uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint16_t len) override;

  void     resetControllers();                 // PB center, expression max, pedals off
  void     applyTgParam(uint8_t addr, uint8_t value);
  uint8_t  readTgParam(uint8_t addr) const;    // live values from _ep/_fx
  void     txTgBlock();
  static uint8_t ccZone3(uint8_t v);           // 0-42->0, 43-85->1, 86-127->2

  mdaEPiano*     _ep = nullptr;
  RefaceCpChain* _fx = nullptr;
  bool           _softPedal = false;           // CC67 state, scales note-on velocity
};

#endif // __MIDI_REFACE_H__

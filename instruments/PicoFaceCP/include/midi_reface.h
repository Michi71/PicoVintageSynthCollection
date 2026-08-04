// include/midi_reface.h
//
// PicoFaceCP — Yamaha reface CP compatible MIDI layer (RP2350, C++17).
// Implements the MIDI data format of the Yamaha reface CP (Data List p.11-14):
//   * SYSTEM common block (base 00 00 00)
//   * TG (Tone Generator) block (base 30 00 00)
//   * Bulk Dump / Dump Request SysEx
//   * Identity Reply, Active Sensing, front-panel CC transmit
// Runs on Core 1; engine/FX mutations are forwarded through IPC helpers (ipc.h).
// Implementation: src/midi_reface.cpp
//
#ifndef __MIDI_REFACE_H__
#define __MIDI_REFACE_H__

#include <stdint.h>

class mdaEPiano;       // engine (Core 0 image; mutations via ipc.h)
class RefaceCpChain;   // FX chain (Core 0 image; mutations via ipc.h)

class RefaceMidi {
public:
  // SYSTEM common block indices (reface CP MIDI spec, base address 00 00 00)
  enum SysAddr : uint8_t {
    SYS_TX_CH          = 0x00,
    SYS_RX_CH          = 0x01,
    SYS_TUNE_HH        = 0x02,
    SYS_TUNE_HL        = 0x03,
    SYS_TUNE_LH        = 0x04,
    SYS_TUNE_LL        = 0x05,
    SYS_LOCAL          = 0x06,
    SYS_TRANSPOSE      = 0x07,
    SYS_SUSTAIN_SEL    = 0x0B,
    SYS_AUTO_POWER_OFF = 0x0C,
    SYS_SPEAKER_OUT    = 0x0D,
    SYS_MIDI_CONTROL   = 0x0E,
    SYS_BLOCK_SIZE     = 0x20
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

  static constexpr uint8_t RX_CH_ALL = 0x10;

  void init(mdaEPiano* ep, RefaceCpChain* fx);

  // Active sensing TX (200ms) + RX timeout (350ms); call from Core-1 loop.
  void tick();

  // Transport RX entry points (wired to MIDIInputUSB callbacks).
  void onNoteOn(uint8_t note, uint8_t vel, uint8_t ch);
  void onNoteOff(uint8_t note, uint8_t vel, uint8_t ch);
  void onControlChange(uint8_t cc, uint8_t val, uint8_t ch);
  void onProgramChange(uint8_t program, uint8_t ch);  // PC 0..7 -> factory preset (deviation: original has no PC)
  void onPitchBend(uint16_t bend14, uint8_t ch);
  void onRealtime(uint8_t status);
  void onSysEx(const uint8_t* data, uint16_t len);
  void notifyActivity();

  // Front panel -> MIDI OUT (CC), gated by MIDI Control setting.
  void txFxParam(uint8_t fxParam, float v01);  // FxParam id (ipc.h) -> CC 18/19/81/86/87/89/90/91
  void txFxMode(uint8_t fxMode, int mode);     // FxMode id (ipc.h) -> CC 17/85/88 (0/64/127)
  void txInstrument(int instr);                // -> CC 80 (0,25,51,76,102,127)
  void txProgram(int preset);                  // -> Program Change 0..7 (factory preset)

  // Front-panel SYSTEM screen access to MIDI RX channel (same core, no IPC).
  uint8_t getRxChannel() const { return _sys[SYS_RX_CH]; }   // 0..15 = ch 1-16, RX_CH_ALL = omni
  void setRxChannel(uint8_t ch);   // clamps to 0..RX_CH_ALL

  void getSystemBlock(uint8_t* dst) const;   // copy SYS_BLOCK_SIZE (32) bytes of the SYSTEM common image (for persistence)
  void loadSystemBlock(const uint8_t* src);  // restore SYSTEM common image (32 bytes), sanitize, re-apply master tune

  bool midiControlEnabled() const { return _sys[SYS_MIDI_CONTROL] != 0; }

private:
  bool     channelOk(uint8_t ch) const;        // _sys[SYS_RX_CH]==RX_CH_ALL or match
  int      transposeNote(int note) const;      // + master transpose + UI octave, clamp 0..127
  void     allSoundOff();
  void     allNotesOff();
  void     resetControllers();                 // PB center, expression max, pedals off
  void     applyMasterTune();                  // 4-nibble tune -> engine fine tune param
  void     applySystemParam(uint8_t addr, const uint8_t* data, uint16_t len);
  void     applyTgParam(uint8_t addr, uint8_t value);
  uint8_t  readTgParam(uint8_t addr) const;    // live values from _ep/_fx
  void     txBytes(const uint8_t* b, uint16_t n);  // tud_midi_stream_write wrapper
  void     txCC(uint8_t cc, uint8_t val);
  void     txIdentityReply();
  void     txParamChange(uint8_t ah, uint8_t am, uint8_t al);  // current value(s)
  void     txBulkBlock(uint8_t ah, uint8_t am, uint8_t al,
                       const uint8_t* data, uint8_t len);
  void     handleYamahaSysEx(const uint8_t* d, uint16_t len);
  void     handleBulkDump(const uint8_t* d, uint16_t len);
  void     handleDumpRequest(uint8_t ah, uint8_t am, uint8_t al);
  static uint8_t ccZone3(uint8_t v);           // 0-42->0, 43-85->1, 86-127->2

  mdaEPiano*   _ep = nullptr;
  RefaceCpChain* _fx = nullptr;
  uint8_t  _sys[SYS_BLOCK_SIZE] = {};          // SYSTEM common image; defaults set in init()
  bool     _softPedal = false;                 // CC67 state, scales note-on velocity
  bool     _senseActive = false;               // FE received -> supervise 350ms timeout
  uint32_t _lastRxMs = 0;
  uint32_t _lastTxSenseMs = 0;
};

#endif // __MIDI_REFACE_H__

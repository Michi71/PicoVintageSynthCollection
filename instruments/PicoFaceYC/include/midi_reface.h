// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef MIDI_REFACE_H
#define MIDI_REFACE_H
#include <stdint.h>

class YC_Synth_Bridge;

class RefaceMidi {
public:
    static constexpr uint8_t RX_CH_ALL = 0x10;

    void init(YC_Synth_Bridge* bridge);
    void tick();
    void onNoteOn(uint8_t note, uint8_t vel, uint8_t ch);
    void onNoteOff(uint8_t note, uint8_t vel, uint8_t ch);
    void onControlChange(uint8_t cc, uint8_t val, uint8_t ch);
    void onPitchBend(uint16_t bend14, uint8_t ch);
    void onRealtime(uint8_t status);
    void onSysEx(const uint8_t* data, uint16_t len);
    void notifyActivity();
    bool midiControlEnabled() const { return _midiControlEnabled; }
    void setMidiControlEnabled(bool enabled) { _midiControlEnabled = enabled; }
    uint8_t getRxChannel() const { return _rxChannel; }
    void setRxChannel(uint8_t ch) { _rxChannel = (ch > RX_CH_ALL) ? RX_CH_ALL : ch; }
    void txCC(uint8_t cc, uint8_t val);
    void txPanelMirror(uint8_t param_id, uint8_t internalValue);
    void txRotaryMirror(uint8_t speed);
    void txParamChange(uint8_t addrLow, uint8_t value);
    void txParamRequestReply(uint8_t addrLow);

private:
    YC_Synth_Bridge* _bridge = nullptr;
    bool _midiControlEnabled = true;
    uint8_t _rxChannel = RX_CH_ALL;
    bool _senseActive = false;
    uint32_t _lastRxMs = 0;
    uint32_t _lastTxSenseMs = 0;

    static uint8_t quantize2(uint8_t val);
    static uint8_t quantize5(uint8_t val);
    static uint8_t quantize7(uint8_t val);
    static uint8_t quantizeRotary(uint8_t val);
    static uint8_t quantizeWave(uint8_t val);
    bool channelOk(uint8_t ch) const;
    void applyTgParam(uint8_t addrLow, uint8_t value);
    uint8_t readTgParam(uint8_t addrLow) const;

    void txIdentityReply();
    void txBytes(const uint8_t* b, uint16_t n);
};

#endif // MIDI_REFACE_H

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "RD_Controller.h"
#include "rd_cc_map.h"
#include "midi_serial.h"

// RD_Controller shadow array holds UI units:
//   - continuous params: percent 0..100
//   - toggle params: 0/1
// The single wire-conversion point is toWire()/sendParam() at send time.
// Engine bridge normalizes wire value /255 with >=0.5 == ON, so toggles
// MUST be sent as 0 or 255 and percent maps 0->0, 100->255 via (v*255+50)/100.

// Default parameter shadow values.
// UI units: percent 0..100; toggles 0/1.
static constexpr uint8_t kDefVolume       = 80;
static constexpr uint8_t kDefChorusOn     = 0;
static constexpr uint8_t kDefChorusRate   = 40;
static constexpr uint8_t kDefChorusDepth  = 50;
static constexpr uint8_t kDefTremOn       = 0;
static constexpr uint8_t kDefTremRate     = 50;
static constexpr uint8_t kDefTremDepth    = 50;
static constexpr uint8_t kDefBass         = 50;
static constexpr uint8_t kDefTreble       = 50;
static constexpr uint8_t kDefDacFilterOn  = 1;
static constexpr uint8_t kDefPhaserOn      = 0;
static constexpr uint8_t kDefPhaserRate    = 50;
static constexpr uint8_t kDefPhaserDepth   = 50;

// Step a percent value by delta (1% per detent), clamped to 0..100.
static uint8_t stepPct(uint8_t v, int delta) {
    int n = (int)v + delta;
    if (n < 0) n = 0;
    if (n > 100) n = 100;
    return (uint8_t)n;
}

// True for toggle params (stored as 0/1 in shadow).
static bool isToggleParam(uint8_t id) {
    return id == RD_PARAM_CHORUS_ON ||
           id == RD_PARAM_TREM_ON ||
           id == RD_PARAM_PHASER_ON ||
           id == RD_PARAM_DAC_FILTER_ON;
}

// Convert UI value to engine wire format (0..255).
// Toggles: 0/1 -> 0/255. Percent -> (v*255+50)/100 so 0->0, 100->255.
// Bridge normalizes wire/255 and treats >=0.5 as ON.
static uint8_t toWire(uint8_t id, uint8_t v) {
    if (isToggleParam(id)) {
        return v ? 255 : 0;
    }
    return (uint8_t)(((int)v * 255 + 50) / 100);
}

// Send a shadow UI value to the engine, converting to wire format.
// Transmit channel for the panel mirror, kept in step with midiCh_.
static uint8_t s_txChannel = 0;

static void sendParam(uint8_t id, uint8_t uiVal) {
    ipc_send_dx_param(id, toWire(id, uiVal));

    // Mirror the edit as a Control Change, the way the reface instruments do.
    // Only the encoder handlers reach this function; importSettings() sends
    // directly, so a restore at boot stays silent.
    if (id >= RD_PARAM_COUNT) return;
    const uint8_t cc = kRdCc[id];
    if (cc == RD_CC_NONE) return;
    // UI units are percent 0..100 for continuous params, 0/1 for toggles.
    const uint8_t val = isToggleParam(id) ? (uiVal ? 127 : 0)
                                          : (uint8_t)(((int)uiVal * 127 + 50) / 100);
    midiSerial().sendControlChange(s_txChannel, cc, val);
}

// Page name table.
static const char *const kPageNames[(int)RdPage::COUNT] = {
    "PATCH", "CHORUS", "TREMOLO", "PHASER", "EQ", "VOICES", "TUNE", "SYS"
};

RD_Controller::RD_Controller(RD_Midi &midi)
    : midi_(midi)
{
    page_ = RdPage::PATCH;
    instrument_ = 0;
    midiCh_ = 16; // Omni by default (matches RD_MIDI_OMNI)

    for (uint16_t i = 0; i < RD_PARAM_COUNT; ++i) shadow_[i] = 0;

    shadow_[RD_PARAM_VOLUME]        = kDefVolume;
    shadow_[RD_PARAM_CHORUS_ON]     = kDefChorusOn;
    shadow_[RD_PARAM_CHORUS_RATE]   = kDefChorusRate;
    shadow_[RD_PARAM_CHORUS_DEPTH]  = kDefChorusDepth;
    shadow_[RD_PARAM_TREM_ON]       = kDefTremOn;
    shadow_[RD_PARAM_TREM_RATE]     = kDefTremRate;
    shadow_[RD_PARAM_TREM_DEPTH]    = kDefTremDepth;
    shadow_[RD_PARAM_BASS]          = kDefBass;
    shadow_[RD_PARAM_TREBLE]        = kDefTreble;
    shadow_[RD_PARAM_DAC_FILTER_ON] = kDefDacFilterOn;
    shadow_[RD_PARAM_PHASER_ON]      = kDefPhaserOn;
    shadow_[RD_PARAM_PHASER_RATE]    = kDefPhaserRate;
    shadow_[RD_PARAM_PHASER_DEPTH]   = kDefPhaserDepth;
}

RdPage RD_Controller::advancePage(RdPage current, int delta) {
    int n = static_cast<int>(current) + delta;
    int m = static_cast<int>(RdPage::COUNT);
    // Positive modulo, handles negative inputs.
    n %= m;
    if (n < 0) n += m;
    return static_cast<RdPage>(n);
}

void RD_Controller::onEncoder1(int delta) {
    page_ = advancePage(page_, delta);
}

void RD_Controller::onEncoder2(int delta) {
    switch (page_) {
        case RdPage::PATCH: {
            int v = (int)instrument_ + delta;
            if (v < 0) v = 0;
            if (v > 15) v = 15;
            instrument_ = (uint8_t)v;
            ipc_send_dx_param(RD_PARAM_INSTRUMENT, instrument_);
            break;
        }
        case RdPage::CHORUS: {
            uint8_t depth = stepPct(shadow_[RD_PARAM_CHORUS_DEPTH], delta);
            shadow_[RD_PARAM_CHORUS_DEPTH] = depth;
            sendParam(RD_PARAM_CHORUS_DEPTH, depth);
            uint8_t on = (depth == 0) ? 0 : 1;
            shadow_[RD_PARAM_CHORUS_ON] = on;
            sendParam(RD_PARAM_CHORUS_ON, on);
            break;
        }
        case RdPage::TREMOLO: {
            uint8_t depth = stepPct(shadow_[RD_PARAM_TREM_DEPTH], delta);
            shadow_[RD_PARAM_TREM_DEPTH] = depth;
            sendParam(RD_PARAM_TREM_DEPTH, depth);
            uint8_t on = (depth == 0) ? 0 : 1;
            shadow_[RD_PARAM_TREM_ON] = on;
            sendParam(RD_PARAM_TREM_ON, on);
            break;
        }
        case RdPage::PHASER: {
            uint8_t depth = stepPct(shadow_[RD_PARAM_PHASER_DEPTH], delta);
            shadow_[RD_PARAM_PHASER_DEPTH] = depth;
            sendParam(RD_PARAM_PHASER_DEPTH, depth);
            uint8_t on = (depth == 0) ? 0 : 1;
            shadow_[RD_PARAM_PHASER_ON] = on;
            sendParam(RD_PARAM_PHASER_ON, on);
            break;
        }
        case RdPage::EQ: {
            uint8_t bass = stepPct(shadow_[RD_PARAM_BASS], delta);
            shadow_[RD_PARAM_BASS] = bass;
            sendParam(RD_PARAM_BASS, bass);
            break;
        }
        case RdPage::VOICES: {
            int vm = (int)voiceMode_ + delta;
            if (vm < 0) vm = 0;
            if (vm > 4) vm = 4;
            voiceMode_ = (uint8_t)vm;
            ipc_send_dx_param(RD_PARAM_VOICE_MODE, voiceMode_); // raw value, NOT percent
            break;
        }
        case RdPage::TUNE: {
            int t = (int)masterTune_ + delta;
            if (t < -50) t = -50;
            else if (t > 50) t = 50;
            masterTune_ = (int8_t)t;
            ipc_send_dx_param(RD_PARAM_MASTER_TUNE, (uint16_t)(masterTune_ + 50)); // cents+50, raw
            break;
        }
        case RdPage::SYS: {
            if (delta != 0) {
                uint8_t f = (shadow_[RD_PARAM_DAC_FILTER_ON] == 0) ? 1 : 0;
                shadow_[RD_PARAM_DAC_FILTER_ON] = f;
                sendParam(RD_PARAM_DAC_FILTER_ON, f);
            }
            break;
        }
        default:
            break;
    }
}

void RD_Controller::onEncoder3(int delta) {
    switch (page_) {
        case RdPage::PATCH: {
            uint8_t vol = stepPct(shadow_[RD_PARAM_VOLUME], delta);
            shadow_[RD_PARAM_VOLUME] = vol;
            sendParam(RD_PARAM_VOLUME, vol);
            break;
        }
        case RdPage::CHORUS: {
            uint8_t rate = stepPct(shadow_[RD_PARAM_CHORUS_RATE], delta);
            shadow_[RD_PARAM_CHORUS_RATE] = rate;
            sendParam(RD_PARAM_CHORUS_RATE, rate);
            break;
        }
        case RdPage::TREMOLO: {
            uint8_t rate = stepPct(shadow_[RD_PARAM_TREM_RATE], delta);
            shadow_[RD_PARAM_TREM_RATE] = rate;
            sendParam(RD_PARAM_TREM_RATE, rate);
            break;
        }
        case RdPage::PHASER: {
            uint8_t rate = stepPct(shadow_[RD_PARAM_PHASER_RATE], delta);
            shadow_[RD_PARAM_PHASER_RATE] = rate;
            sendParam(RD_PARAM_PHASER_RATE, rate);
            break;
        }
        case RdPage::EQ: {
            uint8_t tr = stepPct(shadow_[RD_PARAM_TREBLE], delta);
            shadow_[RD_PARAM_TREBLE] = tr;
            sendParam(RD_PARAM_TREBLE, tr);
            break;
        }
        case RdPage::VOICES: {
            // Line B is a live read-only display; no adjustment here.
            break;
        }
        case RdPage::TUNE: {
            // Line B shows the derived A4 frequency; no second parameter.
            break;
        }
        case RdPage::SYS: {
            int v = (int)midiCh_ + delta;
            if (v < 0) v = 0;
            if (v > 16) v = 16;
            midiCh_ = (uint8_t)v;
            midi_.setRxChannel(midiCh_); // 0..15, 0x10 = Omni
            // Omni is a receive setting; transmit falls back to channel 1.
            s_txChannel = (midiCh_ == RD_MIDI_OMNI) ? 0 : midiCh_;
            break;
        }
        default:
            break;
    }
}

RdPage RD_Controller::currentPage() const {
    return page_;
}

const char *RD_Controller::pageName() const {
    return kPageNames[(int)page_];
}

const char *RD_Controller::param2Name() const {
    switch (page_) {
        case RdPage::PATCH:   return "Instr";
        case RdPage::CHORUS:  return "Depth";
        case RdPage::TREMOLO: return "Depth";
        case RdPage::PHASER:  return "Depth";
        case RdPage::EQ:      return "Bass";
        case RdPage::VOICES:  return "Voices";
        case RdPage::TUNE:    return "Tune";
        case RdPage::SYS:     return "DACFlt";
        default:              return "";
    }
}

const char *RD_Controller::param3Name() const {
    switch (page_) {
        case RdPage::PATCH:   return "Volume";
        case RdPage::CHORUS:  return "Rate";
        case RdPage::TREMOLO: return "Rate";
        case RdPage::PHASER:  return "Rate";
        case RdPage::EQ:      return "Treble";
        case RdPage::VOICES:  return "Active";
        case RdPage::TUNE:    return "";
        case RdPage::SYS:     return "MidiCh";
        default:              return "";
    }
}

uint8_t RD_Controller::param2Value() const {
    switch (page_) {
        case RdPage::PATCH:   return instrument_;
        case RdPage::CHORUS:  return shadow_[RD_PARAM_CHORUS_DEPTH];
        case RdPage::TREMOLO: return shadow_[RD_PARAM_TREM_DEPTH];
        case RdPage::PHASER:  return shadow_[RD_PARAM_PHASER_DEPTH];
        case RdPage::EQ:      return shadow_[RD_PARAM_BASS];
        case RdPage::VOICES:  return voiceMode_;
        case RdPage::TUNE:    return (uint8_t)(masterTune_ + 50); // cents+50 (0..100)
        case RdPage::SYS:     return shadow_[RD_PARAM_DAC_FILTER_ON];
        default:              return 0;
    }
}

uint8_t RD_Controller::param3Value() const {
    switch (page_) {
        case RdPage::PATCH:   return shadow_[RD_PARAM_VOLUME];
        case RdPage::CHORUS:  return shadow_[RD_PARAM_CHORUS_RATE];
        case RdPage::TREMOLO: return shadow_[RD_PARAM_TREM_RATE];
        case RdPage::PHASER:  return shadow_[RD_PARAM_PHASER_RATE];
        case RdPage::EQ:      return shadow_[RD_PARAM_TREBLE];
        case RdPage::VOICES:  return 0; // line B is rendered live from the bridge
        case RdPage::TUNE:    return 0; // line B is derived (A4 frequency)
        case RdPage::SYS:     return midiCh_;
        default:              return 0;
    }
}

void RD_Controller::exportSettings(RdSettingsV1& s) const
{
    s.instrument  = instrument_;
    s.volume      = shadow_[RD_PARAM_VOLUME];
    s.chorusOn    = shadow_[RD_PARAM_CHORUS_ON];
    s.chorusRate  = shadow_[RD_PARAM_CHORUS_RATE];
    s.chorusDepth = shadow_[RD_PARAM_CHORUS_DEPTH];
    s.tremOn      = shadow_[RD_PARAM_TREM_ON];
    s.tremRate    = shadow_[RD_PARAM_TREM_RATE];
    s.tremDepth   = shadow_[RD_PARAM_TREM_DEPTH];
    s.phaserOn    = shadow_[RD_PARAM_PHASER_ON];
    s.phaserRate  = shadow_[RD_PARAM_PHASER_RATE];
    s.phaserDepth = shadow_[RD_PARAM_PHASER_DEPTH];
    s.bass        = shadow_[RD_PARAM_BASS];
    s.treble      = shadow_[RD_PARAM_TREBLE];
    s.dacOn       = shadow_[RD_PARAM_DAC_FILTER_ON];
    s.midiCh      = midiCh_;
    s.voiceMode   = voiceMode_;
    s.masterTune  = masterTune_;
}

// NOTE: this deliberately bypasses sendParam(). That helper mirrors every
// value as a Control Change, which is right for a panel edit but wrong for
// a restore at boot - it would blast the whole parameter set down the DIN
// socket. The other three instruments already restore this way.
void RD_Controller::importSettings(const RdSettingsV1& s)
{
    // Defensive clamping -- a corrupted/foreign record must never escape.
    auto clampPct = [](uint8_t v) -> uint8_t { return (v > 100u) ? 100u : v; };
    auto clampBit = [](uint8_t v) -> uint8_t { return (v != 0u) ? 1u : 0u; };

    instrument_        = (s.instrument > 15u) ? 0u : s.instrument;
    midiCh_            = (s.midiCh > 16u)    ? 16u : s.midiCh;
    voiceMode_         = (s.voiceMode > 4u)  ? 4u  : s.voiceMode;
    masterTune_        = (s.masterTune < -50 || s.masterTune > 50) ? 0 : s.masterTune;

    shadow_[RD_PARAM_VOLUME]        = clampPct(s.volume);
    shadow_[RD_PARAM_CHORUS_ON]     = clampBit(s.chorusOn);
    shadow_[RD_PARAM_CHORUS_RATE]   = clampPct(s.chorusRate);
    shadow_[RD_PARAM_CHORUS_DEPTH]  = clampPct(s.chorusDepth);
    shadow_[RD_PARAM_TREM_ON]       = clampBit(s.tremOn);
    shadow_[RD_PARAM_TREM_RATE]     = clampPct(s.tremRate);
    shadow_[RD_PARAM_TREM_DEPTH]    = clampPct(s.tremDepth);
    shadow_[RD_PARAM_PHASER_ON]     = clampBit(s.phaserOn);
    shadow_[RD_PARAM_PHASER_RATE]   = clampPct(s.phaserRate);
    shadow_[RD_PARAM_PHASER_DEPTH]  = clampPct(s.phaserDepth);
    shadow_[RD_PARAM_BASS]          = clampPct(s.bass);
    shadow_[RD_PARAM_TREBLE]        = clampPct(s.treble);
    shadow_[RD_PARAM_DAC_FILTER_ON] = clampBit(s.dacOn);

    // Push the full restored state to the engine.
    ipc_send_dx_param(RD_PARAM_INSTRUMENT, instrument_);

    ipc_send_dx_param(RD_PARAM_VOLUME,        toWire(RD_PARAM_VOLUME, shadow_[RD_PARAM_VOLUME]));
    ipc_send_dx_param(RD_PARAM_CHORUS_RATE,   toWire(RD_PARAM_CHORUS_RATE, shadow_[RD_PARAM_CHORUS_RATE]));
    ipc_send_dx_param(RD_PARAM_CHORUS_DEPTH,  toWire(RD_PARAM_CHORUS_DEPTH, shadow_[RD_PARAM_CHORUS_DEPTH]));
    ipc_send_dx_param(RD_PARAM_TREM_RATE,     toWire(RD_PARAM_TREM_RATE, shadow_[RD_PARAM_TREM_RATE]));
    ipc_send_dx_param(RD_PARAM_TREM_DEPTH,    toWire(RD_PARAM_TREM_DEPTH, shadow_[RD_PARAM_TREM_DEPTH]));
    ipc_send_dx_param(RD_PARAM_BASS,          toWire(RD_PARAM_BASS, shadow_[RD_PARAM_BASS]));
    ipc_send_dx_param(RD_PARAM_TREBLE,        toWire(RD_PARAM_TREBLE, shadow_[RD_PARAM_TREBLE]));
    ipc_send_dx_param(RD_PARAM_PHASER_RATE,   toWire(RD_PARAM_PHASER_RATE, shadow_[RD_PARAM_PHASER_RATE]));
    ipc_send_dx_param(RD_PARAM_PHASER_DEPTH,  toWire(RD_PARAM_PHASER_DEPTH, shadow_[RD_PARAM_PHASER_DEPTH]));

    ipc_send_dx_param(RD_PARAM_CHORUS_ON,     toWire(RD_PARAM_CHORUS_ON, shadow_[RD_PARAM_CHORUS_ON]));
    ipc_send_dx_param(RD_PARAM_TREM_ON,       toWire(RD_PARAM_TREM_ON, shadow_[RD_PARAM_TREM_ON]));
    ipc_send_dx_param(RD_PARAM_PHASER_ON,     toWire(RD_PARAM_PHASER_ON, shadow_[RD_PARAM_PHASER_ON]));
    ipc_send_dx_param(RD_PARAM_DAC_FILTER_ON, toWire(RD_PARAM_DAC_FILTER_ON, shadow_[RD_PARAM_DAC_FILTER_ON]));

    ipc_send_dx_param(RD_PARAM_VOICE_MODE,   voiceMode_);
    ipc_send_dx_param(RD_PARAM_MASTER_TUNE,  (uint16_t)(masterTune_ + 50));

    midi_.setRxChannel(midiCh_);
    s_txChannel = (midiCh_ == RD_MIDI_OMNI) ? 0 : midiCh_;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// settings_autosave.h - "virtual potentiometer memory": a snapshot of the
// instrument's state is taken every 250 ms, and once it differs from what is
// in flash and has held still for two seconds, it is written as one veeprom
// record.
//
// This is what the reface ports use instead of the core's edit-triggered save
// (settingsSize() > 0): the snapshot catches changes that arrive over MIDI
// too - a voice loaded from an editor survives the power cycle - and the
// record holds a whole patch. Three instruments each carried this loop in
// their own settings.cpp until it moved here; they still own the record
// layout and the gather function.

#ifndef PICOFACE_SETTINGS_AUTOSAVE_H
#define PICOFACE_SETTINGS_AUTOSAVE_H

#include <stdint.h>
#include <string.h>

#include "veeprom.h"

namespace picoface {

template <typename Record>
class SettingsAutosave {
public:
    static constexpr uint32_t kPollMs   = 250;
    static constexpr uint32_t kStableMs = 2000;

    // Call from uiTick(). gather(Record&) fills in the current state; the
    // record is zeroed first so padding never makes two equal states differ.
    template <typename Gather>
    void task(uint32_t nowMs, uint16_t version, Gather&& gather) {
        if (nowMs - lastPollMs_ < kPollMs) return;
        lastPollMs_ = nowMs;

        Record cur;
        memset(&cur, 0, sizeof cur);
        gather(cur);

        if (!baseline_) {                        // first poll: what is in place now is what flash has
            lastSaved_ = cur;
            baseline_ = true;
            return;
        }
        if (memcmp(&cur, &lastSaved_, sizeof cur) == 0) {
            pendingActive_ = false;
            return;
        }
        if (!pendingActive_ || memcmp(&cur, &pending_, sizeof cur) != 0) {
            pending_ = cur;                      // new or still-moving state: restart the clock
            pendingSinceMs_ = nowMs;
            pendingActive_ = true;
            return;
        }
        if (nowMs - pendingSinceMs_ >= kStableMs) {
            if (veeprom_save(&pending_, (uint16_t) sizeof pending_, version)) lastSaved_ = pending_;
            pendingActive_ = false;
        }
    }

private:
    Record   lastSaved_{};
    Record   pending_{};
    bool     baseline_       = false;
    bool     pendingActive_  = false;
    uint32_t pendingSinceMs_ = 0;
    uint32_t lastPollMs_     = 0;
};

} // namespace picoface

#endif // PICOFACE_SETTINGS_AUTOSAVE_H

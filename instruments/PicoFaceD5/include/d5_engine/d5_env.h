// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The D-50's envelope shape, shared by TVA and TVF: three timed levels, a
// sustain the note holds at, and a release to an end level. The panel calls
// them T1..T5 and L1..L3; the parameter ranges are in the MIDI implementation
// chart under "Each partial block".
#pragma once

#include <cstdint>

namespace d5 {

struct Env5Spec {
    float t[5] = {0.004f, 0.10f, 0.20f, 0.30f, 0.40f};   // seconds
    float l[3] = {1.0f, 0.85f, 0.7f};
    float sustain = 0.6f;
    float end = 0.0f;
};

class Env5 {
public:
    void start(const Env5Spec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        level_ = 0.0f;
        held_ = true;
        arm(0, spec_.l[0]);
    }

    void release() {
        if (held_) {
            held_ = false;
            arm(4, spec_.end);
        }
    }

    bool finished() const { return !held_ && seg_ >= 5; }
    float level() const { return level_ < 0.0f ? 0.0f : level_; }

    float next() {
        if (remaining_ > 0) {
            level_ += step_;
            --remaining_;
        } else if (held_ && seg_ < 3) {
            arm(seg_ + 1, seg_ + 1 < 3 ? spec_.l[seg_ + 1] : spec_.sustain);
        } else if (held_ && seg_ == 3) {
            level_ = spec_.sustain;          // hold until release
        } else if (!held_ && seg_ == 4) {
            seg_ = 5;
            level_ = spec_.end;
        }
        return level();
    }

private:
    void arm(int seg, float target) {
        seg_ = seg;
        remaining_ = static_cast<int32_t>(spec_.t[seg] * sr_);
        step_ = remaining_ > 0 ? (target - level_) / remaining_ : 0.0f;
        if (remaining_ <= 0) level_ = target;
    }

    Env5Spec spec_{};
    float sr_ = 32000.0f;
    float level_ = 0.0f;
    float step_ = 0.0f;
    int32_t remaining_ = 0;
    int seg_ = 0;
    bool held_ = false;
};

}  // namespace d5

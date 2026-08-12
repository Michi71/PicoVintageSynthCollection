// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The D-50's envelope shape, shared by TVA and TVF: three timed levels, a
// sustain the note holds at, and a release to an end level. The panel calls
// them T1..T5 and L1..L3; the parameter ranges are in the MIDI implementation
// chart under "Each partial block".
#pragma once

#include <cmath>
#include <cstdint>

#include "d5_engine/d5_hot.h"

namespace d5 {

struct Env5Spec {
    float t[5] = {0.004f, 0.10f, 0.20f, 0.30f, 0.40f};   // seconds
    float l[3] = {1.0f, 0.85f, 0.7f};
    float sustain = 0.6f;
    float end = 0.0f;
    // The LA envelopes run linear in decibels, not in amplitude: a single
    // note of the reference recording decays at a constant -34 dB/s, which
    // an amplitude-linear segment cannot do -- it holds energy up and then
    // dives. TVA envelopes set this; TVF keeps linear segments because its
    // output feeds a cutoff that is already exponential.
    bool log_segments = false;
    // Per-segment decay RATES in dB/s, used for falling log segments when
    // non-zero. The LA chip's time bytes set rates, not durations -- the
    // proof is Horn Section, whose measured release of -37.8 dB/s equals
    // its byte through the fitted map, while duration semantics predicted
    // -108 -- and munt implements the MT-32 sibling the same way. Rising
    // segments keep durations.
    float r[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
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
    float level() const {
        if (spec_.log_segments && level_ <= 1.05e-3f && remaining_ <= 0) return 0.0f;
        return level_ < 0.0f ? 0.0f : level_;
    }

    // Advance n samples at once -- the control-rate path. Segment changes
    // land on block edges; the shortest documented segment (4 ms) still
    // spans eight blocks, so nothing audible is lost.
    float next_n(int32_t n) {
        while (n > 0) {
            if (remaining_ > 0) {
                const int32_t k = remaining_ < n ? remaining_ : n;
                if (seg_log_) {
                    for (int32_t j = 0; j < k; ++j) level_ *= factor_;
                } else {
                    level_ += step_ * k;
                }
                remaining_ -= k;
                n -= k;
            } else if (held_ && seg_ < 3) {
                arm(seg_ + 1, seg_ + 1 < 3 ? spec_.l[seg_ + 1] : spec_.sustain);
            } else if (held_) {
                level_ = spec_.sustain;
                break;
            } else if (seg_ == 4) {
                seg_ = 5;
                level_ = spec_.end;
                break;
            } else {
                break;
            }
        }
        return level();
    }

    float D5_HOT_TAG(d5_env_next, next)() {
        if (remaining_ > 0) {
            if (seg_log_) level_ *= factor_;
            else level_ += step_;
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
        if (remaining_ <= 0) { level_ = target; step_ = 0.0f; factor_ = 1.0f; return; }
        // Rate semantics for linear falling segments (the TVF): duration
        // follows from the level distance and the per-second rate.
        if (!spec_.log_segments && spec_.r[seg] > 0.0f && target < level_) {
            remaining_ = static_cast<int32_t>((level_ - target) / spec_.r[seg] * sr_);
            if (remaining_ < 1) remaining_ = 1;
        }
        step_ = (target - level_) / remaining_;
        // Log-linear glide for FALLING segments only: a decay at constant
        // dB/s is what the reference recording shows, but a rise in the log
        // domain spends most of its time inaudibly near the floor -- a
        // two-second pad swell would be silent for its first half. Attacks
        // keep the linear ramp.
        seg_log_ = spec_.log_segments && target < level_;
        // -60 dB, not -96: the last segment glides to "zero" through this
        // floor, and the deeper it lies the steeper that dive reads in dB/s.
        // The reference recording puts the whole body decay near -34 dB/s;
        // -60 keeps the final segment in that neighbourhood.
        const float kFloor = 1.0e-3f;
        if (!seg_log_) { factor_ = 1.0f; return; }
        const float from = level_ < kFloor ? kFloor : level_;
        const float to = target < kFloor ? kFloor : target;
        if (spec_.r[seg] > 0.0f) {
            // Rate semantics: duration follows from the distance in dB.
            const float dist_db = 20.0f * std::log10(from / to);
            remaining_ = static_cast<int32_t>(dist_db / spec_.r[seg] * sr_);
            if (remaining_ < 1) remaining_ = 1;
            step_ = (target - level_) / remaining_;
        }
        factor_ = std::pow(to / from, 1.0f / static_cast<float>(remaining_));
        if (level_ < kFloor) level_ = kFloor;
    }

    Env5Spec spec_{};
    float sr_ = 32000.0f;
    float level_ = 0.0f;
    float step_ = 0.0f;
    float factor_ = 1.0f;
    bool seg_log_ = false;
    int32_t remaining_ = 0;
    int seg_ = 0;
    bool held_ = false;
};

}  // namespace d5

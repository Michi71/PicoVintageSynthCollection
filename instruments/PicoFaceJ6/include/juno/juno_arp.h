/*
  juno_arp.h -- the arpeggiator

  Not a piece of signal path: it sits between the keyboard and the voice
  allocator, holds the keys that are down, and hands the allocator one note at
  a time on its own clock.

  Specifications page: ARPEGGIO RATE 1.5 .. 50 Hz. Panel Board A has the on/off
  switch, a MODE switch and a RANGE switch; the rear panel has an ARPEGGIO
  CLOCK input, which there is no socket for here.

  Two things about it are worth stating because they decide how it feels:

    it plays a gate, not a continuous note
      The arpeggio clock produces a pulse per step. Each step therefore
      retriggers the contours, which is the whole point -- with the gate held
      open across steps it would sound like one note sliding around instead of
      a sequence.

    HOLD latches the keys, not the sound
      Let go of the chord with HOLD on and the arpeggio keeps running from the
      keys that were down. Play a new key while it is latched and that key
      joins the pattern, which is how a Juno is actually used: you build the
      figure up under your hands.
*/

#ifndef JUNO_ARP_H
#define JUNO_ARP_H

#include "juno_defs.h"
#include "juno_dsp.h"

enum JunoArpMode {
    JUNO_ARP_UP = 0,
    JUNO_ARP_UPDOWN,
    JUNO_ARP_DOWN
};

class JunoArp
{
public:
    void init(float sampleRate)
    {
        sr_ = sampleRate;
        setRate(0.3f);
        clear();
    }

    void setSampleRate(float sampleRate) { sr_ = sampleRate; setRate(ratePanel_); }

    void setRate(float v)
    {
        ratePanel_ = junoClamp(v, 0.0f, 1.0f);
        const float hz = JUNO_ARP_HZ_MIN *
                         powf(JUNO_ARP_HZ_MAX / JUNO_ARP_HZ_MIN, ratePanel_);
        inc_ = hz / sr_;
    }

    void setMode(int m)  { mode_  = m; }
    void setRange(int r) { range_ = (r < 1) ? 1 : (r > JUNO_ARP_RANGES ? JUNO_ARP_RANGES : r); }

    void clear()
    {
        count_  = 0;
        step_   = 0;
        phase_  = 0.0f;
        playing_ = -1;
        gateOpen_ = false;
    }

    int  heldCount() const { return count_; }
    int  sounding()  const { return playing_; }

    /* --- Keys ----------------------------------------------------------- */
    /*
     * Kept sorted, because the pattern is defined by pitch order and not by
     * the order the keys went down. Returns true if the key was added.
     */
    bool keyDown(int note)
    {
        for (int i = 0; i < count_; ++i) if (key_[i] == note) return false;
        if (count_ >= JUNO_ARP_MAX_KEYS) return false;

        int i = count_;
        while (i > 0 && key_[i - 1] > note) { key_[i] = key_[i - 1]; --i; }
        key_[i] = note;
        ++count_;
        return true;
    }

    void keyUp(int note)
    {
        for (int i = 0; i < count_; ++i) {
            if (key_[i] == note) {
                for (int j = i; j < count_ - 1; ++j) key_[j] = key_[j + 1];
                --count_;
                return;
            }
        }
    }

    /* --- Clock ---------------------------------------------------------- */
    /*
     * One sample of the arpeggio clock.
     *
     * noteOn is set to a note number when a step begins, noteOff when the gate
     * for the previous step closes; both are -1 otherwise. The caller passes
     * them straight to the voice allocator, which is what keeps the
     * arpeggiator free of any knowledge about voices.
     */
    void tick(int& noteOn, int& noteOff)
    {
        noteOn = noteOff = -1;

        if (count_ <= 0) {
            /* Last key released with HOLD off: close whatever is sounding
             * rather than leaving it stuck. */
            if (playing_ >= 0) { noteOff = playing_; playing_ = -1; }
            gateOpen_ = false;
            phase_    = 0.0f;
            step_     = 0;
            return;
        }

        /* Gate closes partway through the step, so the next one retriggers. */
        if (gateOpen_ && phase_ >= JUNO_ARP_GATE) {
            if (playing_ >= 0) { noteOff = playing_; playing_ = -1; }
            gateOpen_ = false;
        }

        phase_ += inc_;
        if (phase_ < 1.0f) return;

        phase_ -= 1.0f;

        /* A step boundary with the gate somehow still open -- can happen when
         * the rate is turned up far enough that the step is shorter than the
         * gate was. Close it first so the allocator never sees two note-ons
         * for one card. */
        if (playing_ >= 0) { noteOff = playing_; playing_ = -1; }

        const int total = patternLength();
        if (step_ >= total) step_ = 0;

        noteOn    = noteAt(step_);
        playing_  = noteOn;
        gateOpen_ = true;

        if (++step_ >= total) step_ = 0;
    }

private:
    int patternLength() const
    {
        const int n = count_ * range_;
        if (mode_ != JUNO_ARP_UPDOWN) return n;
        /* Up then back down without sounding either end twice. One key over
         * one octave has nowhere to go, so it stays a single step. */
        return (n <= 1) ? 1 : (2 * n - 2);
    }

    int noteAt(int step) const
    {
        const int n = count_ * range_;
        int idx;

        switch (mode_) {
            case JUNO_ARP_DOWN:
                idx = n - 1 - (step % n);
                break;
            case JUNO_ARP_UPDOWN:
                if (n <= 1) { idx = 0; break; }
                idx = (step < n) ? step : (2 * n - 2 - step);
                break;
            case JUNO_ARP_UP:
            default:
                idx = step % n;
                break;
        }

        if (idx < 0) idx = 0;
        if (idx >= n) idx = n - 1;

        const int oct = idx / count_;
        const int k   = idx % count_;
        int note = key_[k] + 12 * oct;
        if (note > 127) note = 127;
        return note;
    }

    int   key_[JUNO_ARP_MAX_KEYS] = {};
    int   count_    = 0;
    int   step_     = 0;
    int   playing_  = -1;
    bool  gateOpen_ = false;
    int   mode_     = JUNO_ARP_UP;
    int   range_    = 1;
    float phase_    = 0.0f;
    float inc_      = 0.0f;
    float ratePanel_= 0.3f;
    float sr_       = (float) SAMPLING_RATE;
};

#endif /* JUNO_ARP_H */

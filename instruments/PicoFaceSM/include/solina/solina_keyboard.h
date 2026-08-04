/*
  solina_keyboard.h -- Manual Circuit + Gate Circuit + Sustain Circuits

  Original (schematic, sheet 015.0212):
      Ten TDA470 chips provide a 4' and an 8' gate for every key. The
      "Sustain Circuits" charge and discharge a capacitor through the front
      panel controls CRESCENDO (attack) and SUSTAIN (decay); the result
      drives the gate. There is therefore no voice allocation -- every key
      has its own gate and all 49 can sound at once.

      The gate outputs run onto five busses, one per keyboard group
      (Panel A). Each group sees its own RC network in the Gate Output
      Circuit -- a keyboard split of the timbre. That is why the summing here
      happens per group and footage rather than per note.

      The Bass Circuit has its own "Low-Tone Selection" with lowest-note
      priority and its own sustain circuit.
*/

#ifndef SOLINA_KEYBOARD_H
#define SOLINA_KEYBOARD_H

#include "solina_defs.h"
#include "solina_divider.h"

class SolinaKeyboard
{
public:
    void init(float sampleRate);
    void reset();

    void noteOn(int note);
    void noteOff(int note);
    void allNotesOff();
    void setSustainPedal(bool on);

    /* Front panel: CRESCENDO = attack time, SUSTAIN = decay time (seconds) */
    void setCrescendo(float seconds);
    void setSustain(float seconds);

    /*
     * Renders one block.
     *   bus8/bus4     [group][sample] -- the manual's summing busses
     *   bass8/bass16                  -- Bass Circuit, monophonic
     */
    void process(SolinaDivider& divider,
                 float bus8[SOLINA_NGROUPS][SOLINA_BLOCK],
                 float bus4[SOLINA_NGROUPS][SOLINA_BLOCK],
                 float* bass8, float* bass16,
                 int count);

    bool anyActive() const { return activeCount_ > 0 || bassActive_; }

    static int groupOf(int note)
    {
        int g = (note - SOLINA_KEY_FIRST) / SOLINA_KEYS_PER_GROUP;
        if (g < 0) g = 0;
        if (g >= SOLINA_NGROUPS) g = SOLINA_NGROUPS - 1;
        return g;
    }

    /* Centre frequency of a keyboard group (used to tune the filters) */
    static float groupCenterHz(int g);

private:
    struct Key {
        uint8_t note;
        uint8_t group;
        bool    held;
        bool    sustained;
        float   env;
    };

    void   retireFinished();
    int    findActive(int note) const;
    int    releaseCandidate() const;
    int    lowestHeldBassNote() const;

    float  samplerate_ = 44100.0f;
    float  attackCoef_ = 0.01f;
    float  releaseCoef_ = 0.001f;
    float  crescendo_ = 0.06f;
    float  sustainTime_ = 0.30f;
    bool   pedal_ = false;

    Key    active_[SOLINA_MAX_ACTIVE_KEYS] = {};
    int    activeCount_ = 0;

    /* Bass Circuit: a single note with lowest-note priority */
    bool   bassActive_ = false;
    int    bassNote_ = -1;
    float  bassEnv_ = 0.0f;

    /* Key state, needed for the lowest-note selection */
    bool   keyHeld_[128] = {};
};

#endif /* SOLINA_KEYBOARD_H */

/*
  SM_Controller.cpp -- front panel logic for PicoFaceSM
*/

#include "SM_Controller.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Page layout                                                               */
/*                                                                           */
/* First the front panel of the original (registers, envelope, ensemble),     */
/* then the fine tuning, which in the instrument is component values and      */
/* trimmers. Reordering means moving a line.                                  */
/* ------------------------------------------------------------------------ */
struct SmPage {
    const char* name;
    int16_t     a;
    int16_t     b;
};

static const SmPage kPages[] = {
    { "PRESET",   SM_UI_PROGRAM,        SOLINA_VOLUME        },
    { "STRINGS",  SOLINA_VIOLA,         SOLINA_VIOLIN        },
    { "BRASS",    SOLINA_TRUMPET,       SOLINA_HORN          },
    { "BASS",     SOLINA_CONTRABASS,    SOLINA_CELLO         },
    { "BASS VOL", SOLINA_BASS_VOLUME,   SOLINA_TUNE          },
    { "ENVELOPE", SOLINA_CRESCENDO,     SOLINA_SUSTAIN       },
    { "ENSEMBLE", SOLINA_ENSEMBLE,      SOLINA_ENSEMBLE_TONE },
    { "MOD DEPTH",SOLINA_TREMOLO_DEPTH, SOLINA_CHORUS_DEPTH  },
    { "MOD RATE", SOLINA_TREMOLO_RATE,  SOLINA_CHORUS_RATE   },
    { "PHASER",   SOLINA_PHASER,        SOLINA_PHASER_RATE   },
    { "PHAS COL", SOLINA_PHASER_COLOR,  SOLINA_SHAPER        },
    { "TONE",     SOLINA_TONE_LOWPASS,  SOLINA_TONE_HIGHPASS },
    { "COLOUR",   SOLINA_TONE_SHELF,    SOLINA_FORMANT       },
    { "SYS",      SM_UI_MIDICH,         SOLINA_ENSEMBLE_WIDTH },
};

static const int kPageCount = (int) (sizeof(kPages) / sizeof(kPages[0]));

/* Display names of the engine parameters; the SM_UI_* entries follow at the
 * end.
 *
 * An empty name means the value describes itself and is shown without a
 * label. That is the case for the program -- the header already says PRESET,
 * and "Program 6 Strings+Brass" would be 25 characters, twice the width of
 * the line (font 8x13B, 15 characters from x=4). */
static const char* kNames[SM_UI_COUNT] = {
    "Contrabass", "Cello",      "Viola",      "Violin",
    "Trumpet",    "Horn",       "Bass Vol",   "Crescendo",
    "Sustain",    "Volume",     "Tune",       "Ensemble",
    "Trem Rate",  "Trem Depth", "Chor Rate",  "Chor Depth",
    "Ens Tone",   "Ens Width",  "Phaser",     "Phas Rate",
    "Phas Color", "Tone LP",    "Tone HP",    "Tone Shelf",
    "Formant",    "Shaper",     "",           "MIDI Ch"
};

/* Switch parameters toggle between 0 and 1, everything else moves in steps
 * of 1 % (see adjust()). */
static bool isSwitch(int id)
{
    return id <= SOLINA_HORN || id == SOLINA_ENSEMBLE || id == SOLINA_PHASER;
}

/* ------------------------------------------------------------------------ */
SM_Controller::SM_Controller(SM_Midi& midi)
    : midi_(midi)
{
    /* Take the shadow copies from the startup program */
    syncFromProgram(program_);
}

void SM_Controller::syncFromProgram(int32_t program)
{
    if (program < 0 || program >= SOLINA_NPROGRAMS)
        return;
    program_ = program;
    for (int i = 0; i < SOLINA_PARAM_COUNT; ++i)
        shadow_[i] = solinaPrograms[program].param[i];
}

int SM_Controller::pageCount() const { return kPageCount; }

const char* SM_Controller::pageName() const { return kPages[page_].name; }

int SM_Controller::paramIdOf(int slot) const
{
    return slot == 0 ? kPages[page_].a : kPages[page_].b;
}

const char* SM_Controller::paramAName() const { return kNames[paramIdOf(0)]; }
const char* SM_Controller::paramBName() const { return kNames[paramIdOf(1)]; }

void SM_Controller::onEncoder1(int delta)
{
    if (delta == 0) return;
    page_ = (page_ + delta) % kPageCount;
    if (page_ < 0) page_ += kPageCount;
}

void SM_Controller::onEncoder2(int delta) { adjust(0, delta); }
void SM_Controller::onEncoder3(int delta) { adjust(1, delta); }

bool SM_Controller::homePage()
{
    if (page_ == 0)
        return false;
    page_ = 0;
    return true;
}

void SM_Controller::sendParam(int id, float v)
{
    /* Per mille across the IPC ring; the audio side divides by 1000 again. */
    uint16_t q = (uint16_t) (v * 1000.0f + 0.5f);
    if (q > 1000) q = 1000;
    ipc_send_param((uint8_t) id, q);
}

void SM_Controller::adjust(int slot, int delta)
{
    if (delta == 0) return;

    const int id = paramIdOf(slot);

    if (id == SM_UI_PROGRAM)
    {
        int32_t p = (program_ + delta) % SOLINA_NPROGRAMS;
        if (p < 0) p += SOLINA_NPROGRAMS;
        syncFromProgram(p);
        ipc_send_param(SM_PARAM_PROGRAM, (uint16_t) p);
        return;
    }

    if (id == SM_UI_MIDICH)
    {
        int c = (int) midiCh_ + delta;
        if (c < 0) c = 0;
        if (c > SM_MIDI_OMNI) c = SM_MIDI_OMNI;
        midiCh_ = (uint8_t) c;
        midi_.setRxChannel(midiCh_);
        return;
    }

    if (isSwitch(id))
    {
        shadow_[id] = (shadow_[id] != 0.0f) ? 0.0f : 1.0f;
    }
    else
    {
        /*
         * Work in whole percent rather than adding an amount. The preset
         * values do not sit on the grid ("Contrabass" has volume 0.827), so
         * round values would otherwise be unreachable -- from 83 you would
         * only ever get 81, 79, 77. The first click snaps onto the percent
         * grid, after which every whole value is reachable.
         */
        int pct = (int) (shadow_[id] * 100.0f + 0.5f);
        pct += delta;
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        shadow_[id] = (float) pct / 100.0f;
    }
    sendParam(id, shadow_[id]);
}

/* ------------------------------------------------------------------------ */
/* Display                                                                   */
/* ------------------------------------------------------------------------ */
void SM_Controller::formatValue(int id, char* dst, size_t n) const
{
    if (id == SM_UI_PROGRAM)
    {
        snprintf(dst, n, "%d %s", (int) program_ + 1,
                 solinaPrograms[program_].name);
        return;
    }
    if (id == SM_UI_MIDICH)
    {
        if (midiCh_ >= SM_MIDI_OMNI) snprintf(dst, n, "Omni");
        else                          snprintf(dst, n, "%d", (int) midiCh_ + 1);
        return;
    }
    if (isSwitch(id))
    {
        snprintf(dst, n, "%s", shadow_[id] != 0.0f ? "on" : "off");
        return;
    }
    snprintf(dst, n, "%d%%", (int) (shadow_[id] * 100.0f + 0.5f));
}

void SM_Controller::paramAText(char* dst, size_t n) const
{
    formatValue(paramIdOf(0), dst, n);
}

void SM_Controller::paramBText(char* dst, size_t n) const
{
    formatValue(paramIdOf(1), dst, n);
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                               */
/* ------------------------------------------------------------------------ */
void SM_Controller::exportSettings(SmSettingsV1& s) const
{
    s.program = (uint8_t) program_;
    s.midiCh  = midiCh_;
    for (int i = 0; i < SOLINA_PARAM_COUNT; ++i)
        s.param[i] = (uint16_t) (shadow_[i] * 1000.0f + 0.5f);
}

void SM_Controller::importSettings(const SmSettingsV1& s)
{
    program_ = (s.program < SOLINA_NPROGRAMS) ? s.program : 0;
    midiCh_  = (s.midiCh <= SM_MIDI_OMNI) ? s.midiCh : SM_MIDI_OMNI;
    midi_.setRxChannel(midiCh_);

    /* Set the program first, then lay the deviating parameters on top --
     * that keeps the order the same as when operating the panel. */
    ipc_send_param(SM_PARAM_PROGRAM, (uint16_t) program_);

    for (int i = 0; i < SOLINA_PARAM_COUNT; ++i)
    {
        uint16_t q = s.param[i];
        if (q > 1000) q = 1000;
        shadow_[i] = (float) q / 1000.0f;
        ipc_send_param((uint8_t) i, q);
    }
}

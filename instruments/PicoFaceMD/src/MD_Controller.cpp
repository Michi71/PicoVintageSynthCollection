/*
  MD_Controller.cpp -- front panel logic for PicoFaceMD
*/

#include "MD_Controller.h"
#include "midi_serial.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Page layout                                                               */
/*                                                                           */
/* The sections are the ones printed on the instrument, in the order they sit */
/* on the panel: Controllers, Oscillator Bank, Mixer, Modifiers, Output. The  */
/* preset list comes first because that is where playing starts, and the two  */
/* sections after Output hold what the original has as trimmers and wiring    */
/* rather than as knobs.                                                      */
/*                                                                            */
/* Page names carry their section, so that the header alone says where you    */
/* are -- "Volume 8.5" on a page called MIXER would not tell you which of the  */
/* four volume controls is under the encoder.                                 */
/* ------------------------------------------------------------------------ */
struct MdPage {
    const char* name;
    int16_t     a;
    int16_t     b;
};

struct MdSection {
    const char*   name;
    const MdPage* pages;
    uint8_t       count;
};

static const MdPage kPresetPages[] = {
    { "PRESET",      MD_UI_PROGRAM,      MD_UI_NONE          },
};

static const MdPage kControllerPages[] = {
    { "CTL TUNE",    MOOG_TUNE,          MOOG_BEND_RANGE     },
    { "CTL GLIDE",   MOOG_GLIDE,         MOOG_GLIDE_ON       },
    { "CTL MOD",     MOOG_MOD_MIX,       MOOG_MOD_WHEEL      },
    { "CTL MOD SW",  MOOG_OSC_MOD,       MOOG_OSC3_CTRL      },
};

static const MdPage kOscillatorPages[] = {
    { "OSC 1",       MOOG_OSC1_RANGE,    MOOG_OSC1_WAVE      },
    { "OSC 2",       MOOG_OSC2_RANGE,    MOOG_OSC2_WAVE      },
    { "OSC 3",       MOOG_OSC3_RANGE,    MOOG_OSC3_WAVE      },
    { "OSC DETUNE",  MOOG_OSC2_FREQ,     MOOG_OSC3_FREQ      },
};

static const MdPage kMixerPages[] = {
    { "MIX OSC 1",   MOOG_OSC1_VOL,      MOOG_OSC1_ON        },
    { "MIX OSC 2",   MOOG_OSC2_VOL,      MOOG_OSC2_ON        },
    { "MIX OSC 3",   MOOG_OSC3_VOL,      MOOG_OSC3_ON        },
    { "MIX NOISE",   MOOG_NOISE_VOL,     MOOG_NOISE_ON       },
    { "MIX COLOUR",  MOOG_NOISE_COLOR,   MD_UI_NONE          },
    { "MIX FEEDB",   MOOG_FEEDBACK_VOL,  MOOG_FEEDBACK_ON    },
};

static const MdPage kModifierPages[] = {
    { "MOD FILTER",  MOOG_CUTOFF,        MOOG_EMPHASIS       },
    { "MOD CONTOUR", MOOG_CONTOUR_AMT,   MOOG_FILTER_MOD     },
    { "MOD KEYBRD",  MOOG_KB_CTRL_1,     MOOG_KB_CTRL_2      },
    { "MOD FIL ENV", MOOG_FILT_ATTACK,   MOOG_FILT_DECAY     },
    { "MOD FIL ENV", MOOG_FILT_SUSTAIN,  MOOG_DECAY_SW       },
    { "MOD AMP ENV", MOOG_LOUD_ATTACK,   MOOG_LOUD_DECAY     },
    { "MOD AMP ENV", MOOG_LOUD_SUSTAIN,  MD_UI_NONE          },
};

static const MdPage kOutputPages[] = {
    { "OUTPUT",      MOOG_VOLUME,        MOOG_A440           },
};

/* Two slots and three effects; the signal runs A then B, so the order is
 * chosen here rather than fixed. Each effect keeps its own settings whether
 * or not a slot is currently pointing at it. */
static const MdPage kEffectPages[] = {
    { "FX SLOTS",    MOOG_FX_SLOT_A,     MOOG_FX_SLOT_B      },
    { "FX CHORUS",   MOOG_CHORUS_RATE,   MOOG_CHORUS_DEPTH   },
    { "FX CHORUS",   MOOG_CHORUS_MIX,    MOOG_CHORUS_FB      },
    { "FX DELAY",    MOOG_DELAY_TIME,    MOOG_DELAY_FB       },
    { "FX DELAY",    MOOG_DELAY_MIX,     MOOG_DELAY_TONE     },
    { "FX REVERB",   MOOG_REVERB_SIZE,   MOOG_REVERB_DAMP    },
    { "FX REVERB",   MOOG_REVERB_MIX,    MOOG_REVERB_WIDTH   },
};

static const MdPage kVintagePages[] = {
    { "VTG DRIFT",   MOOG_DRIFT,         MOOG_DRIVE          },
    { "VTG TONE",    MOOG_TONE,          MD_UI_NONE          },
    { "VTG KEYS",    MOOG_NOTE_PRIORITY, MOOG_TRIGGER        },
};

static const MdPage kSystemPages[] = {
    { "SYS MIDI",    MD_UI_MIDICH,       MOOG_TRANSPOSE      },
};

#define SECTION(n, p) { n, p, (uint8_t) (sizeof(p) / sizeof(p[0])) }

static const MdSection kSections[] = {
    SECTION("PRESET",      kPresetPages),
    SECTION("CONTROLLERS", kControllerPages),
    SECTION("OSCILLATOR",  kOscillatorPages),
    SECTION("MIXER",       kMixerPages),
    SECTION("MODIFIERS",   kModifierPages),
    SECTION("OUTPUT",      kOutputPages),
    SECTION("EFFECTS",     kEffectPages),
    SECTION("VINTAGE",     kVintagePages),
    SECTION("SYSTEM",      kSystemPages),
};

#undef SECTION

static const int kSectionCount = (int) (sizeof(kSections) / sizeof(kSections[0]));

/* ------------------------------------------------------------------------ */
MD_Controller::MD_Controller(MD_Midi& midi)
    : midi_(midi)
{
    syncFromProgram(program_);
}

void MD_Controller::syncFromProgram(int32_t program)
{
    if (program < 0 || program >= MOOG_NPROGRAMS)
        return;
    program_ = program;
    for (int i = 0; i < MOOG_PARAM_COUNT; ++i)
        shadow_[i] = moogPrograms[program].param[i];
}

int MD_Controller::pageCount() const
{
    return (int) kSections[section_].count;
}

int MD_Controller::paramIdOf(int slot) const
{
    const MdPage& p = kSections[section_].pages[page_];
    return slot == 0 ? p.a : p.b;
}

const char* MD_Controller::nameOf(int id) const
{
    if (id >= 0 && id < MOOG_PARAM_COUNT)
        return kMoogParams[id].name;
    if (id == MD_UI_MIDICH)  return "Channel";
    if (id == MD_UI_PROGRAM) return "";      /* the value names itself */
    return "";
}

/* ------------------------------------------------------------------------ */
/* Navigation                                                                */
/* ------------------------------------------------------------------------ */
void MD_Controller::onEncoder1(int delta)
{
    if (delta == 0) return;

    if (!inSection_) {
        section_ = (section_ + delta) % kSectionCount;
        if (section_ < 0) section_ += kSectionCount;
        page_ = 0;
    } else {
        const int n = pageCount();
        page_ = (page_ + delta) % n;
        if (page_ < 0) page_ += n;
    }
}

void MD_Controller::onEncoder2(int delta)
{
    /* Nothing is editable while the section list is up. Turning the value
     * encoders there would otherwise change a parameter belonging to a page
     * that is not even on screen. */
    if (!inSection_) return;
    adjust(0, delta);
}

void MD_Controller::onEncoder3(int delta)
{
    if (!inSection_) return;
    adjust(1, delta);
}

bool MD_Controller::onSelectButton()
{
    inSection_ = !inSection_;
    if (inSection_) page_ = 0;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Editing                                                                   */
/* ------------------------------------------------------------------------ */
void MD_Controller::sendParam(int id, float v)
{
    /* Per mille across the IPC ring; the audio side divides by 1000 again. */
    int q = (int) (v * 1000.0f + 0.5f);
    if (q < 0)    q = 0;
    if (q > 1000) q = 1000;
    ipc_send_param((uint8_t) id, (uint16_t) q);

    /* Mirror the edit as a Control Change, the way the original front
       panel does. Only the encoder path reaches this function - a value
       that arrived over MIDI lands in onMidiParam() instead, so this
       cannot loop back on itself. */
    if (id < 0 || id >= MOOG_PARAM_COUNT) return;

    /* not every panel control has a controller number */
    const uint8_t cc = kMoogParams[id].cc;
    if (cc == 0xFF) return;

    /* Omni is a receive setting; transmit falls back to channel 1 */
    const uint8_t txCh = (midiCh_ == MD_MIDI_OMNI) ? 0 : midiCh_;

    const uint8_t val = (uint8_t)((q * 127 + 500) / 1000);
    midiSerial().sendControlChange(txCh, cc, val);
}

void MD_Controller::adjust(int slot, int delta)
{
    if (delta == 0) return;

    const int id = paramIdOf(slot);

    if (id == MD_UI_NONE)
        return;

    if (id == MD_UI_PROGRAM) {
        int32_t p = (program_ + delta) % MOOG_NPROGRAMS;
        if (p < 0) p += MOOG_NPROGRAMS;
        syncFromProgram(p);
        ipc_send_param(MD_PARAM_PROGRAM, (uint16_t) p);
        return;
    }

    if (id == MD_UI_MIDICH) {
        int c = (int) midiCh_ + delta;
        if (c < 0)            c = 0;
        if (c > MD_MIDI_OMNI) c = MD_MIDI_OMNI;
        midiCh_ = (uint8_t) c;
        midi_.setRxChannel(midiCh_);
        return;
    }

    if (id < 0 || id >= MOOG_PARAM_COUNT)
        return;

    const MoogParamDesc& d = kMoogParams[id];

    if (d.type == MOOG_T_SWITCH || d.type == MOOG_T_ENUM) {
        /* One click, one position -- including for switches, where turning
         * right means on and left means off. A toggle would make the
         * direction of the encoder meaningless. */
        const int steps = (d.steps < 2) ? 2 : d.steps;
        int s = moogParamStep(shadow_[id], steps) + delta;
        if (s < 0)       s = 0;
        if (s >= steps)  s = steps - 1;
        shadow_[id] = moogParamFromStep(s, steps);
    } else {
        /*
         * Work in whole percent rather than adding an amount. Preset values
         * do not sit on the grid (the cutoff of "Fat Bass" is 0.30 but that
         * of "Reso Sweep" is 0.12), so without snapping, round values would
         * be unreachable from some presets. The first click lands on the
         * grid, after which every step of a tenth of a panel unit is
         * reachable.
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
MdViewKind MD_Controller::viewKind() const
{
    if (!inSection_)
        return MD_VIEW_LIST;
    /* The preset section is a list as well -- picking a sound by scrolling
     * names beats reading one name at a time. */
    return isPresetList() ? MD_VIEW_LIST : MD_VIEW_PAGE;
}

const char* MD_Controller::title() const
{
    if (!inSection_)
        return "MENU";
    return kSections[section_].pages[page_].name;
}

void MD_Controller::counterText(char* dst, size_t n) const
{
    if (!inSection_)
        snprintf(dst, n, "%d/%d", section_ + 1, kSectionCount);
    else if (isPresetList())
        snprintf(dst, n, "%d/%d", (int) program_ + 1, MOOG_NPROGRAMS);
    else
        snprintf(dst, n, "%d/%d", page_ + 1, pageCount());
}

void MD_Controller::formatValue(int id, char* dst, size_t n) const
{
    if (id == MD_UI_NONE) {
        snprintf(dst, n, "--");
        return;
    }
    if (id == MD_UI_PROGRAM) {
        snprintf(dst, n, "%d %s", (int) program_ + 1,
                 moogPrograms[program_].name);
        return;
    }
    if (id == MD_UI_MIDICH) {
        if (midiCh_ >= MD_MIDI_OMNI) snprintf(dst, n, "Omni");
        else                         snprintf(dst, n, "%d", (int) midiCh_ + 1);
        return;
    }
    moogFormatValue(id, shadow_[id], dst, n);
}

const char* MD_Controller::paramAName() const { return nameOf(paramIdOf(0)); }
const char* MD_Controller::paramBName() const { return nameOf(paramIdOf(1)); }

void MD_Controller::paramAText(char* dst, size_t n) const
{
    formatValue(paramIdOf(0), dst, n);
}

void MD_Controller::paramBText(char* dst, size_t n) const
{
    formatValue(paramIdOf(1), dst, n);
}

/* --- List view ---------------------------------------------------------- */
/* Two lists share one renderer: the section menu at the top level, and the
 * preset list inside the PRESET section. Which one is meant is decided by the
 * page rather than by the level, so that a second list page elsewhere would
 * not silently start showing presets. */
bool MD_Controller::isPresetList() const
{
    return inSection_ && paramIdOf(0) == MD_UI_PROGRAM;
}

int MD_Controller::listCount() const
{
    return isPresetList() ? MOOG_NPROGRAMS : kSectionCount;
}

int MD_Controller::listCursor() const
{
    return isPresetList() ? (int) program_ : section_;
}

void MD_Controller::listEntry(int index, char* dst, size_t n) const
{
    if (!dst || n == 0) return;

    if (isPresetList()) {
        if (index < 0 || index >= MOOG_NPROGRAMS) { dst[0] = 0; return; }
        snprintf(dst, n, "%2d %s", index + 1, moogPrograms[index].name);
        return;
    }

    if (index < 0 || index >= kSectionCount) { dst[0] = 0; return; }
    snprintf(dst, n, "%s", kSections[index].name);
}

/* ------------------------------------------------------------------------ */
/* Kept in step with MIDI                                                    */
/* ------------------------------------------------------------------------ */
void MD_Controller::onMidiParam(int id, uint16_t perMille)
{
    if (id < 0 || id >= MOOG_PARAM_COUNT) return;
    if (perMille > 1000) perMille = 1000;
    shadow_[id] = (float) perMille / 1000.0f;
}

void MD_Controller::onMidiProgram(int32_t program)
{
    syncFromProgram(program);
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                               */
/* ------------------------------------------------------------------------ */
void MD_Controller::exportSettings(MdSettingsV1& s) const
{
    s.program = (uint8_t) program_;
    s.midiCh  = midiCh_;
    for (int i = 0; i < MOOG_PARAM_COUNT; ++i)
        s.param[i] = (uint16_t) (shadow_[i] * 1000.0f + 0.5f);
}

void MD_Controller::importSettings(const MdSettingsV1& s)
{
    program_ = (s.program < MOOG_NPROGRAMS) ? s.program : 0;
    midiCh_  = (s.midiCh <= MD_MIDI_OMNI) ? s.midiCh : MD_MIDI_OMNI;
    midi_.setRxChannel(midiCh_);

    /* Set the preset first, then lay the deviating parameters on top -- the
     * same order as operating the panel. */
    ipc_send_param(MD_PARAM_PROGRAM, (uint16_t) program_);

    for (int i = 0; i < MOOG_PARAM_COUNT; ++i) {
        uint16_t q = s.param[i];
        if (q > 1000) q = 1000;
        shadow_[i] = (float) q / 1000.0f;
        ipc_send_param((uint8_t) i, q);
    }
}

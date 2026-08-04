// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  J6_Controller.cpp -- front panel logic for PicoFaceJ6
*/

#include "J6_Controller.h"
#include "midi_serial.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Page layout                                                               */
/*                                                                           */
/* The sections are the ones silkscreened on the instrument, in the order they */
/* sit on the panel: LFO, DCO, HPF, VCF, VCA, ENV, CHORUS. The patch list      */
/* comes first because that is where playing starts, and SYSTEM last for the   */
/* handful of things a Juno has on its rear panel or not at all.               */
/*                                                                           */
/* Page names carry their section, so the header alone says where you are --   */
/* "LFO 4.0" on a page called DCO would not tell you whether that is the       */
/* oscillator's share of the LFO or the filter's, and both exist.              */
/* ------------------------------------------------------------------------ */
struct J6Page {
    const char* name;
    int16_t     a;
    int16_t     b;
};

struct J6Section {
    const char*      name;
    const J6Page*    pages;
    uint8_t          count;
};

/*
 * The characters a name can be built from, in the order the encoder steps
 * through them: space first, then upper case, lower case, digits and a little
 * punctuation. The 48 factory names use only letters and spaces, so anything
 * beyond that is for names of your own.
 */
static const char kNameChars[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-+'/&#";

static const J6Page kPatchPages[] = {
    { "PATCH",       J6_UI_PROGRAM,      J6_UI_NONE          },
    { "PATCH NAME",  J6_UI_NAME_POS,     J6_UI_NAME_CHAR     },
    { "PATCH WRITE", J6_UI_WRITE_SLOT,   J6_UI_WRITE_ACTION  },
};

static const J6Page kLfoPages[] = {
    { "LFO",         JUNO_LFO_RATE,      JUNO_LFO_DELAY      },
};

static const J6Page kDcoPages[] = {
    { "DCO RANGE",   JUNO_DCO_RANGE,     JUNO_DCO_LFO        },
    { "DCO WAVE",    JUNO_DCO_SAW,       JUNO_DCO_PULSE      },
    { "DCO PWM",     JUNO_DCO_PWM,       JUNO_DCO_PWM_MODE   },
    { "DCO SUB",     JUNO_DCO_SUB,       JUNO_DCO_SUB_LEVEL  },
    { "DCO NOISE",   JUNO_DCO_NOISE,     J6_UI_NONE          },
};

static const J6Page kHpfPages[] = {
    { "HPF",         JUNO_HPF,           J6_UI_NONE          },
};

static const J6Page kVcfPages[] = {
    { "VCF",         JUNO_VCF_FREQ,      JUNO_VCF_RES        },
    { "VCF ENV",     JUNO_VCF_ENV,       JUNO_VCF_POLARITY   },
    { "VCF MOD",     JUNO_VCF_LFO,       JUNO_VCF_KYBD       },
};

static const J6Page kVcaPages[] = {
    { "VCA",         JUNO_VCA_LEVEL,     JUNO_VCA_MODE       },
};

static const J6Page kEnvPages[] = {
    { "ENV A/D",     JUNO_ENV_ATTACK,    JUNO_ENV_DECAY      },
    { "ENV S/R",     JUNO_ENV_SUSTAIN,   JUNO_ENV_RELEASE    },
};

static const J6Page kChorusPages[] = {
    { "CHORUS",      JUNO_CHORUS,        J6_UI_NONE          },
};

/* The master volume. Not part of a patch, which is the whole point -- it is
 * there to even out patches that sit at different levels, and a patch
 * parameter would be overwritten by the next patch change. */
static const J6Page kOutputPages[] = {
    { "OUTPUT",      JUNO_MASTER,        J6_UI_NONE          },
};

/* Arpeggiator. Instrument settings like the master volume, so a patch change
 * leaves the figure running. */
static const J6Page kArpPages[] = {
    { "ARP",         JUNO_ARP_ON,        JUNO_HOLD           },
    { "ARP PATTERN", JUNO_ARP_MODE,      JUNO_ARP_RANGE      },
    { "ARP RATE",    JUNO_ARP_RATE,      J6_UI_NONE          },
};

static const J6Page kSystemPages[] = {
    { "SYS MIDI",    J6_UI_MIDICH,       JUNO_TRANSPOSE      },
    { "SYS TUNE",    JUNO_TUNE,          JUNO_BEND_RANGE     },
    { "SYS LFO",     JUNO_LFO_TRIG,      J6_UI_NONE          },
};

#define SECTION(n, p) { n, p, (uint8_t) (sizeof(p) / sizeof(p[0])) }

static const J6Section kSections[] = {
    SECTION("PATCH",  kPatchPages),
    SECTION("LFO",    kLfoPages),
    SECTION("DCO",    kDcoPages),
    SECTION("HPF",    kHpfPages),
    SECTION("VCF",    kVcfPages),
    SECTION("VCA",    kVcaPages),
    SECTION("ENV",    kEnvPages),
    SECTION("CHORUS", kChorusPages),
    SECTION("ARP",    kArpPages),
    SECTION("OUTPUT", kOutputPages),
    SECTION("SYSTEM", kSystemPages),
};

#undef SECTION

static const int kSectionCount = (int) (sizeof(kSections) / sizeof(kSections[0]));

/* 48 factory sounds first, then the 56 user memories. A fixed layout, so a
 * Program Change always means the same thing -- with the list shortened to the
 * occupied memories the numbering would shift under a sequencer every time a
 * patch was written. 104 fits inside Program Change's 128. */
#define J6_FACTORY_PATCHES  JUNO_NPROGRAMS
#define J6_ALL_PATCHES      (J6_FACTORY_PATCHES + J6_USER_PATCHES)

static bool isUserSlot(int32_t p) { return p >= J6_FACTORY_PATCHES; }
static int  userSlotOf(int32_t p) { return (int) (p - J6_FACTORY_PATCHES); }

/* ------------------------------------------------------------------------ */
J6_Controller::J6_Controller(J6_Midi& midi)
    : midi_(midi)
{
    /* The instrument half is not part of any patch, so it needs its defaults
     * from the same place the engine takes them. */
    for (int i = JUNO_PARAM_COUNT; i < JUNO_TOTAL_COUNT; ++i)
        shadow_[i] = junoInstrumentDefault(i);

    syncFromProgram(program_);
}

void J6_Controller::syncFromProgram(int32_t program)
{
    if (program < 0 || program >= J6_ALL_PATCHES)
        return;

    program_ = program;

    if (!isUserSlot(program)) {
        for (int i = 0; i < JUNO_PARAM_COUNT; ++i)
            shadow_[i] = junoPrograms[program].param[i];
        rememberSoundName(junoPrograms[program].name);
        markBaseline();
        return;
    }

    /* A free memory has nothing to recall, so the sound is left alone and the
     * display says so. Moving onto it is still allowed: that is how you get to
     * it in order to write. */
    J6UserPatch up;
    if (!j6_patch_read(userSlotOf(program), up))
        return;

    for (int i = 0; i < JUNO_PARAM_COUNT; ++i) {
        uint16_t q = up.param[i];
        if (q > 1000) q = 1000;
        shadow_[i] = (float) q / 1000.0f;
    }
    rememberSoundName(up.name);
    markBaseline();
}

/*
 * Only ever called where a sound is really taken on. An empty name becomes
 * "Init": a sound with no traceable origin is better labelled as such than
 * given the name of something it did not come from.
 */
void J6_Controller::rememberSoundName(const char* nm)
{
    snprintf(soundName_, sizeof(soundName_), "%s", (nm && nm[0]) ? nm : "Init");
}

/* Reads the name as a fixed field of J6_NAME_EDIT_LEN characters: past the end
 * of the string it is spaces, so the cursor can be moved onto empty positions
 * of a short name. */
char J6_Controller::nameCharAt(int pos) const
{
    if (pos < 0 || pos >= J6_NAME_EDIT_LEN) return ' ';
    const size_t len = strlen(soundName_);
    return (pos < (int) len) ? soundName_[pos] : ' ';
}

void J6_Controller::setNameCharAt(int pos, char c)
{
    if (pos < 0 || pos >= J6_NAME_EDIT_LEN) return;

    char buf[J6_NAME_EDIT_LEN + 1];
    for (int i = 0; i < J6_NAME_EDIT_LEN; ++i) buf[i] = nameCharAt(i);
    buf[pos] = c;

    /* Trailing spaces are not part of the name -- keeping them would push the
     * useful characters out of a twelve-byte field. Leading and interior ones
     * stay, because "Space Sound" needs them. */
    int end = J6_NAME_EDIT_LEN;
    while (end > 0 && buf[end - 1] == ' ') --end;
    buf[end] = 0;

    snprintf(soundName_, sizeof(soundName_), "%s", buf);
}

/*
 * Push a user memory into the engine. Factory sounds go over as a single
 * program change and the engine reads its own table; a user memory has no
 * table on that side, so its parameters go one at a time -- the same route
 * importSettings uses.
 */
void J6_Controller::loadUserPatch(int slot)
{
    J6UserPatch up;
    if (!j6_patch_read(slot, up)) return;

    for (int i = 0; i < JUNO_PARAM_COUNT; ++i) {
        uint16_t q = up.param[i];
        if (q > 1000) q = 1000;
        ipc_send_param((uint8_t) i, q);
    }
}

int J6_Controller::pageCount() const
{
    return (int) kSections[section_].count;
}

int J6_Controller::paramIdOf(int slot) const
{
    const J6Page& p = kSections[section_].pages[page_];
    return slot == 0 ? p.a : p.b;
}

const char* J6_Controller::nameOf(int id) const
{
    if (id >= 0 && id < JUNO_TOTAL_COUNT)
        return kJunoParams[id].name;
    if (id == J6_UI_MIDICH)  return "Channel";
    if (id == J6_UI_PROGRAM)    return "";   /* the value names itself */
    /*
     * No label either: "U56 " and an eleven-character name are exactly the
     * fifteen a line holds, and a "To " in front of them cut the name off on
     * the device. The page is titled PATCH WRITE and the line below carries the
     * arrow, so the two read as destination and what goes into it anyway.
     */
    if (id == J6_UI_WRITE_SLOT) return "";
    /* No label: the action line carries the outgoing name, and eleven
     * characters plus the arrow already need thirteen of a line's fifteen. */
    if (id == J6_UI_WRITE_ACTION) return "";
    /* The name gets the whole line: an empty label makes the value fill it, and
     * eleven characters plus the cursor brackets need thirteen of the fifteen a
     * line holds. */
    if (id == J6_UI_NAME_POS)  return "";
    if (id == J6_UI_NAME_CHAR) return "Char";
    return "";
}

/* ------------------------------------------------------------------------ */
/* Navigation                                                                */
/* ------------------------------------------------------------------------ */
/*
 * Any input other than the write button itself makes the last write result
 * stale, so it is cleared here rather than only where it was set. Leaving it
 * to the two write-page encoders was not enough: naming a sound happens on
 * another page, so coming back showed "written" from the previous write and
 * covered up the name that was about to be stored.
 */
void J6_Controller::clearWriteInfo()
{
    writeInfo_ = 0;
}

void J6_Controller::onEncoder1(int delta)
{
    if (delta == 0) return;
    clearWriteInfo();

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

void J6_Controller::onEncoder2(int delta)
{
    /* Nothing is editable while the section list is up. Turning the value
     * encoders there would otherwise change a parameter belonging to a page
     * that is not even on screen. */
    if (!inSection_) return;
    clearWriteInfo();
    adjust(0, delta);
}

void J6_Controller::onEncoder3(int delta)
{
    if (!inSection_) return;
    clearWriteInfo();
    adjust(1, delta);
}

bool J6_Controller::onSelectButton()
{
    clearWriteInfo();
    inSection_ = !inSection_;
    if (inSection_) page_ = 0;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Editing                                                                   */
/* ------------------------------------------------------------------------ */
void J6_Controller::sendParam(int id, float v)
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
    if (id < 0 || id >= JUNO_TOTAL_COUNT) return;

    /* not every panel control has a controller number */
    const uint8_t cc = kJunoParams[id].cc;
    if (cc == 0xFF) return;

    /* Omni is a receive setting; transmit falls back to channel 1 */
    const uint8_t txCh = (midiCh_ == J6_MIDI_OMNI) ? 0 : midiCh_;

    const uint8_t val = (uint8_t)((q * 127 + 500) / 1000);
    midiSerial().sendControlChange(txCh, cc, val);
}

void J6_Controller::adjust(int slot, int delta)
{
    if (delta == 0) return;

    const int id = paramIdOf(slot);

    if (id == J6_UI_NONE)
        return;

    if (id == J6_UI_PROGRAM) {
        int32_t p = (program_ + delta) % J6_ALL_PATCHES;
        if (p < 0) p += J6_ALL_PATCHES;
        syncFromProgram(p);

        if (isUserSlot(p)) {
            if (j6_patch_valid(userSlotOf(p))) loadUserPatch(userSlotOf(p));
            /* An empty memory leaves the sound alone -- see syncFromProgram. */
        } else {
            ipc_send_param(J6_PARAM_PROGRAM, (uint16_t) p);
        }
        return;
    }

    if (id == J6_UI_WRITE_SLOT) {
        writeSlot_ = (writeSlot_ + delta) % J6_USER_PATCHES;
        if (writeSlot_ < 0) writeSlot_ += J6_USER_PATCHES;
        return;
    }

    if (id == J6_UI_NAME_POS) {
        namePos_ = (namePos_ + delta) % J6_NAME_EDIT_LEN;
        if (namePos_ < 0) namePos_ += J6_NAME_EDIT_LEN;
        return;
    }

    if (id == J6_UI_NAME_CHAR) {
        const int  count = (int) sizeof(kNameChars) - 1;   /* without the NUL */
        const char cur   = nameCharAt(namePos_);
        const char* at   = strchr(kNameChars, cur);
        /* A stored name could hold something outside the set -- start from the
         * space rather than refusing to move. */
        int idx = at ? (int) (at - kNameChars) : 0;
        idx = (idx + delta) % count;
        if (idx < 0) idx += count;
        setNameCharAt(namePos_, kNameChars[idx]);
        return;
    }

    if (id == J6_UI_WRITE_ACTION) {
        if (delta) writeErase_ = !writeErase_;
        return;
    }

    if (id == J6_UI_MIDICH) {
        int c = (int) midiCh_ + delta;
        if (c < 0)            c = 0;
        if (c > J6_MIDI_OMNI) c = J6_MIDI_OMNI;
        midiCh_ = (uint8_t) c;
        midi_.setRxChannel(midiCh_);
        return;
    }

    if (id < 0 || id >= JUNO_TOTAL_COUNT)
        return;

    const JunoParamDesc& d = kJunoParams[id];

    if (d.type == JUNO_T_SWITCH || d.type == JUNO_T_ENUM) {
        /* One click, one position -- including for switches, where turning
         * right means on and left means off. A toggle would make the
         * direction of the encoder meaningless. */
        const int steps = (d.steps < 2) ? 2 : d.steps;
        int s = junoParamStep(shadow_[id], steps) + delta;
        if (s < 0)       s = 0;
        if (s >= steps)  s = steps - 1;
        shadow_[id] = junoParamFromStep(s, steps);
    } else {
        /*
         * Work in whole percent rather than adding an amount. Preset values
         * do not sit on the grid (the cutoff of "Strings I" is 0.70 but that
         * of "Organ I" is 0.40), so without snapping, round values would
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
J6ViewKind J6_Controller::viewKind() const
{
    if (!inSection_)
        return J6_VIEW_LIST;
    /* The preset section is a list as well -- picking a sound by scrolling
     * names beats reading one name at a time. */
    return isPresetList() ? J6_VIEW_LIST : J6_VIEW_PAGE;
}

const char* J6_Controller::title() const
{
    return inSection_ ? kSections[section_].pages[page_].name : "MENU";
}

bool J6_Controller::isEdited() const
{
    for (int i = 0; i < JUNO_PARAM_COUNT; ++i)
        if (quantise(shadow_[i]) != quantise(baseline_[i])) return true;
    return false;
}

void J6_Controller::markBaseline()
{
    for (int i = 0; i < JUNO_PARAM_COUNT; ++i) baseline_[i] = shadow_[i];
}

void J6_Controller::counterText(char* dst, size_t n) const
{
    if (!inSection_)
        snprintf(dst, n, "%d/%d", section_ + 1, kSectionCount);
    else if (isPresetList())
        snprintf(dst, n, "%d/%d", (int) program_ + 1, J6_ALL_PATCHES);
    else
        snprintf(dst, n, "%d/%d", page_ + 1, pageCount());
}

void J6_Controller::formatValue(int id, char* dst, size_t n) const
{
    if (id == J6_UI_NONE) {
        snprintf(dst, n, "--");
        return;
    }
    if (id == J6_UI_PROGRAM) {
        patchLabel((int) program_, dst, n);
        return;
    }
    if (id == J6_UI_WRITE_SLOT) {
        snprintf(dst, n, "U%02d %s", writeSlot_ + 1,
                 j6_patch_valid(writeSlot_) ? j6_patch_name(writeSlot_)
                                            : "-free-");
        return;
    }
    if (id == J6_UI_WRITE_ACTION) {
        /*
         * Shows the outcome once, then goes back to the action as soon as
         * either encoder is turned.
         *
         * In write mode this is the name that is about to be stored, not the
         * word "Write". The line above shows the destination and what it holds
         * now, so the two together read "into U01, which holds Init, goes
         * Test". Showing only the destination's own name meant a rename could
         * not be confirmed anywhere before writing -- the one place the name
         * appeared still said the old one, which reads as the rename not having
         * taken.
         */
        switch (writeInfo_) {
            case 1:  snprintf(dst, n, "written");  break;
            case 2:  snprintf(dst, n, "freed");    break;
            case 3:  snprintf(dst, n, "FAILED");   break;
            default:
                if (writeErase_) snprintf(dst, n, "Erase >");
                else             snprintf(dst, n, "%s >",
                                          soundName_[0] ? soundName_ : "Init");
                break;
        }
        return;
    }
    if (id == J6_UI_NAME_POS) {
        /*
         * The name with the character under the cursor in brackets. Trailing
         * spaces are dropped, but only past the cursor -- the bracket keeps the
         * ones before it, so moving out to position 11 on a short name shows
         * where you are instead of collapsing back to the text.
         */
        char out[J6_NAME_EDIT_LEN + 3];
        int  o = 0;
        for (int i = 0; i < J6_NAME_EDIT_LEN; ++i) {
            const char ch = nameCharAt(i);
            if (i == namePos_) { out[o++] = '['; out[o++] = ch; out[o++] = ']'; }
            else               { out[o++] = ch; }
        }
        while (o > 0 && out[o - 1] == ' ') --o;
        out[o] = 0;
        snprintf(dst, n, "%s", out);
        return;
    }
    if (id == J6_UI_NAME_CHAR) {
        const char ch = nameCharAt(namePos_);
        if (ch == ' ') snprintf(dst, n, "space");
        else           snprintf(dst, n, "%c", ch);
        return;
    }
    if (id == J6_UI_MIDICH) {
        if (midiCh_ >= J6_MIDI_OMNI) snprintf(dst, n, "Omni");
        else                         snprintf(dst, n, "%d", (int) midiCh_ + 1);
        return;
    }
    junoFormatValue(id, shadow_[id], dst, n);
}

const char* J6_Controller::paramAName() const { return nameOf(paramIdOf(0)); }
const char* J6_Controller::paramBName() const { return nameOf(paramIdOf(1)); }

void J6_Controller::paramAText(char* dst, size_t n) const
{
    formatValue(paramIdOf(0), dst, n);
}

void J6_Controller::paramBText(char* dst, size_t n) const
{
    formatValue(paramIdOf(1), dst, n);
}

/* --- List view ---------------------------------------------------------- */
/* Two lists share one renderer: the section menu at the top level, and the
 * preset list inside the PRESET section. Which one is meant is decided by the
 * page rather than by the level, so that a second list page elsewhere would
 * not silently start showing presets. */
bool J6_Controller::isPresetList() const
{
    return inSection_ && paramIdOf(0) == J6_UI_PROGRAM;
}

int J6_Controller::listCount() const
{
    return isPresetList() ? J6_ALL_PATCHES : kSectionCount;
}

int J6_Controller::listCursor() const
{
    return isPresetList() ? (int) program_ : section_;
}

/*
 * How a sound is named in the list and in the header: factory sounds by their
 * number, user memories as U01..U56 with the stored name or "-free-".
 *
 * A function of its own rather than part of listEntry, because the patch
 * number is also shown on pages that are not the list -- and reading it out of
 * listEntry made the text depend on which page happened to be open.
 */
void J6_Controller::patchLabel(int index, char* dst, size_t n) const
{
    if (!dst || n == 0) return;
    if (index < 0 || index >= J6_ALL_PATCHES) { dst[0] = 0; return; }

    if (!isUserSlot(index)) {
        snprintf(dst, n, "%2d %s", index + 1, junoPrograms[index].name);
        return;
    }

    const int slot = userSlotOf(index);
    snprintf(dst, n, "U%02d %s", slot + 1,
             j6_patch_valid(slot) ? j6_patch_name(slot) : "-free-");
}

void J6_Controller::listEntry(int index, char* dst, size_t n) const
{
    if (!dst || n == 0) return;

    if (isPresetList()) { patchLabel(index, dst, n); return; }

    if (index < 0 || index >= kSectionCount) { dst[0] = 0; return; }
    snprintf(dst, n, "%s", kSections[index].name);
}

/* ------------------------------------------------------------------------ */
/* Writing a patch                                                           */
/* ------------------------------------------------------------------------ */
void J6_Controller::useFirstFreeSlot()
{
    for (int i = 0; i < J6_USER_PATCHES; ++i) {
        if (!j6_patch_valid(i)) { writeSlot_ = i; return; }
    }
    writeSlot_ = 0;         /* all 56 taken -- start at the top */
}

bool J6_Controller::onWritePage() const
{
    return inSection_ && paramIdOf(0) == J6_UI_WRITE_SLOT;
}

bool J6_Controller::runPatchAction()
{
    if (!onWritePage()) return false;

    if (writeErase_) {
        /*
         * Freeing does not touch the sound that is playing, even when the
         * memory being freed is the one that was loaded. The patch list entry
         * turns back into "-free-", which is the honest thing to show: the
         * sound is still there, its stored copy is not.
         */
        const bool ok = j6_patch_erase(writeSlot_);
        writeInfo_ = ok ? 2 : 3;
        return ok;
    }

    J6UserPatch up = {};
    up.magic = J6_PATCH_MAGIC;

    /*
     * Only the patch half. The arpeggiator and the master volume are
     * instrument settings, and storing them in a sound would mean recalling it
     * silenced the instrument or stopped the arpeggio.
     */
    for (int i = 0; i < JUNO_PARAM_COUNT; ++i)
        up.param[i] = (uint16_t) (shadow_[i] * 1000.0f + 0.5f);

    /*
     * The name comes from the sound that is loaded, so a memory says what it
     * grew out of. There is no way to type one with three encoders, and "U03
     * Strings I" is more use than "U03".
     *
     * From soundName_ and not from program_: browsing the list onto a free
     * memory moves program_ without loading anything, and asking that empty
     * memory for a name is what used to produce "Init".
     */
    /* Blanking every character is a legitimate edit, but a memory with no name
     * at all reads as broken in the list, so it falls back to "Init". */
    snprintf(up.name, sizeof(up.name), "%s",
             soundName_[0] ? soundName_ : "Init");

    const bool ok = j6_patch_write(writeSlot_, up);
    /* Saved is no longer edited -- the memory now holds exactly this. */
    if (ok) markBaseline();
    writeInfo_ = ok ? 1 : 3;
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Kept in step with MIDI                                                    */
/* ------------------------------------------------------------------------ */
void J6_Controller::onMidiParam(int id, uint16_t perMille)
{
    if (id < 0 || id >= JUNO_TOTAL_COUNT) return;
    if (perMille > 1000) perMille = 1000;
    shadow_[id] = (float) perMille / 1000.0f;
}

void J6_Controller::onMidiProgram(int32_t program)
{
    syncFromProgram(program);
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                               */
/* ------------------------------------------------------------------------ */
void J6_Controller::exportSettings(J6SettingsV1& s) const
{
    s.program = (uint8_t) program_;
    s.midiCh  = midiCh_;
    for (int i = 0; i < JUNO_TOTAL_COUNT; ++i)
        s.param[i] = (uint16_t) (shadow_[i] * 1000.0f + 0.5f);
}

void J6_Controller::importSettings(const J6SettingsV1& s)
{
    program_ = (s.program < J6_ALL_PATCHES) ? s.program : 0;

    /*
     * The lineage is not part of the stored settings, so it is worked out from
     * the restored patch number. Left on a free memory at power-off there is
     * nothing to work it out from -- the sound comes back, its origin does not,
     * and "Init" says exactly that rather than inventing a name.
     */
    if (!isUserSlot(program_))
        rememberSoundName(junoPrograms[program_].name);
    else if (j6_patch_valid(userSlotOf(program_)))
        rememberSoundName(j6_patch_name(userSlotOf(program_)));
    else
        rememberSoundName(nullptr);

    midiCh_  = (s.midiCh <= J6_MIDI_OMNI) ? s.midiCh : J6_MIDI_OMNI;
    midi_.setRxChannel(midiCh_);

    /*
     * Set the factory sound first, then lay the stored parameters on top -- the
     * same order as operating the panel. A user memory has no number the engine
     * knows, so nothing is sent for it: the parameter sweep below carries the
     * whole sound either way, and sending a program change the engine would
     * silently drop only invites someone to wonder later why it is there.
     */
    if (!isUserSlot(program_))
        ipc_send_param(J6_PARAM_PROGRAM, (uint16_t) program_);

    for (int i = 0; i < JUNO_TOTAL_COUNT; ++i) {
        uint16_t q = s.param[i];
        if (q > 1000) q = 1000;
        shadow_[i] = (float) q / 1000.0f;
        ipc_send_param((uint8_t) i, q);
    }

    /*
     * The baseline is the patch the settings name, not the settings themselves,
     * so edits that were never written come back still marked. Left on a free
     * memory there is no patch to compare against -- the sound is then taken as
     * it stands rather than lighting a marker nothing could clear.
     */
    if (!isUserSlot(program_)) {
        for (int i = 0; i < JUNO_PARAM_COUNT; ++i)
            baseline_[i] = junoPrograms[program_].param[i];
        return;
    }

    J6UserPatch up;
    if (!j6_patch_read(userSlotOf(program_), up)) { markBaseline(); return; }

    for (int i = 0; i < JUNO_PARAM_COUNT; ++i) {
        uint16_t q = up.param[i];
        if (q > 1000) q = 1000;
        baseline_[i] = (float) q / 1000.0f;
    }
}

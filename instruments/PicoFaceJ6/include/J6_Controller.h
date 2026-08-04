// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  J6_Controller.h -- front panel logic for PicoFaceJ6

  A two-level menu, and the sections are the ones printed on the instrument:

      PATCH        48 factory sounds and 56 user memories, plus pages for
                   naming a sound and writing it to one of them
      LFO          rate and delay
      DCO          range, waveforms, sub, noise, pulse width
      HPF          the four-position high-pass
      VCF          cutoff, resonance, contour amount and polarity, LFO, key
      VCA          level and whether the amplifier follows the contour or a gate
      ENV          attack, decay, sustain, release
      CHORUS       off / I / II / I+II
      ARP          on, hold, pattern, range, rate
      OUTPUT       master volume
      SYSTEM       receive channel, tune, bend range, LFO trigger, transpose

  The last two sections are instrument settings rather than patch parameters, so
  a patch change leaves the arpeggio running and the volume where it was.

  Three encoders:

      encoder 1   the section, at the top level; the page within it below
      button 1    down into a section, and back out again
      encoder 2   the left-hand parameter of the page
      encoder 3   the right-hand parameter
      button 3    carries out the action on the PATCH WRITE page, and does
                  nothing anywhere else

  The page layout is a table in J6_Controller.cpp -- reordering it means moving
  a line there, not changing code.

  The controller only holds shadow copies for the display; the single source of
  truth is the engine on the audio side, reached through the IPC ring.
*/

#ifndef J6_CONTROLLER_H
#define J6_CONTROLLER_H

#include <cstdint>
#include <cstddef>
#include "juno/juno.h"
#include "J6_Midi.h"
#include "j6_ipc.h"
#include "j6_settings.h"
#include "j6_patchstore.h"

/*
 * Pseudo parameter IDs, above everything the engine knows about.
 *
 * JUNO_TOTAL_COUNT and not JUNO_PARAM_COUNT. When the parameter list was split
 * into a patch half and an instrument half these were still counting from the
 * end of the patch half, so J6_UI_PROGRAM landed on top of JUNO_ARP_ON,
 * J6_UI_MIDICH on JUNO_ARP_MODE and J6_UI_NONE on JUNO_ARP_RANGE. Selecting
 * ARP in the menu then brought up the patch list, and every page with an empty
 * slot quietly edited the arpeggiator's range instead of showing nothing.
 */
enum {
    J6_UI_PROGRAM = JUNO_TOTAL_COUNT,  /* patch 0..JUNO_NPROGRAMS-1         */
    J6_UI_MIDICH,                       /* receive channel 0..15, 16 = omni  */
    J6_UI_WRITE_SLOT,                   /* destination for a patch write     */
    J6_UI_WRITE_ACTION,                 /* the name to write, or free it      */
    J6_UI_NAME_POS,                     /* which character is being edited    */
    J6_UI_NAME_CHAR,                    /* and what it is set to              */
    J6_UI_NONE,                         /* empty slot on a page              */
    J6_UI_COUNT
};

/* The collision above was silent because both sides were valid ids. This makes
 * a repeat a compile error instead. */
static_assert((int) J6_UI_PROGRAM >= (int) JUNO_TOTAL_COUNT,
              "menu pseudo-parameters overlap the engine parameters");

/* How the current screen wants to be drawn. */
enum J6ViewKind {
    J6_VIEW_PAGE = 0,   /* header plus two labelled values */
    J6_VIEW_LIST        /* header plus three rows with a cursor */
};

class J6_Controller
{
public:
    explicit J6_Controller(J6_Midi& midi);

    /* --- Input ---------------------------------------------------------- */
    void onEncoder1(int delta);   /* section, or page within a section */
    void onEncoder2(int delta);   /* parameter A */
    void onEncoder3(int delta);   /* parameter B */

    /* Push button of the first encoder: into the section under the cursor,
     * or back out to the section list. Returns true if anything changed. */
    bool onSelectButton();

    /* --- Display -------------------------------------------------------- */
    J6ViewKind  viewKind() const;
    const char* title() const;                       /* header, left  */

    /*
     * True when the sound differs from the patch it was loaded from, i.e. when
     * writing it would produce something other than what is stored. The display
     * puts a '*' in front of the title while it holds, because the patch list
     * replaces the sound on every detent -- without it there is no sign that
     * there is anything to lose.
     *
     * Reported, not rendered: title() stays the plain page name, so anything
     * matching on it does not have to know about the marker.
     */
    bool isEdited() const;
    void        counterText(char* dst, size_t n) const; /* header, right */

    /* J6_VIEW_PAGE */
    const char* paramAName() const;
    const char* paramBName() const;
    void        paramAText(char* dst, size_t n) const;
    void        paramBText(char* dst, size_t n) const;

    /* J6_VIEW_LIST */
    int  listCount() const;
    int  listCursor() const;
    void listEntry(int index, char* dst, size_t n) const;

    uint8_t midiChannel() const { return midiCh_; }

    /*
     * Carry out what the PATCH WRITE page has selected: store the current
     * settings in the chosen user memory, or free that memory again. Bound to
     * the push button of the third encoder, and only reachable from that page --
     * either one erases a flash sector and stops the audio for a moment, so it
     * must not be possible to trigger by accident.
     */
    bool runPatchAction();

    /* True while the PATCH WRITE page is showing, so the main loop knows
     * whether the button means anything. */
    bool onWritePage() const;

    /*
     * Point the write destination at the first free memory. Called once the
     * store has been read, which is after this object is constructed.
     *
     * It exists to remove the reason to go looking: the patch list is the only
     * other place the memories appear, and browsing it loads every entry the
     * cursor passes over -- so hunting for a free slot there replaces the very
     * sound that was about to be saved.
     */
    void useFirstFreeSlot();

    /* --- Persistence ----------------------------------------------------- */
    void exportSettings(J6SettingsV1& s) const;
    void importSettings(const J6SettingsV1& s);

    /* --- Kept in step with what arrives over MIDI ------------------------ */
    /* So that the display shows the value a controller just sent rather than
     * the one the encoder last set. */
    void onMidiParam(int id, uint16_t perMille);
    void onMidiProgram(int32_t program);

    void syncFromProgram(int32_t program);

private:
    void  adjust(int slot, int delta);       /* slot 0 = A, 1 = B */
    int   paramIdOf(int slot) const;
    void  sendParam(int id, float v);
    void  formatValue(int id, char* dst, size_t n) const;
    void  loadUserPatch(int slot);
    void  rememberSoundName(const char* nm);
    void  clearWriteInfo();
    void  markBaseline();
    static uint16_t quantise(float v) { return (uint16_t) (v * 1000.0f + 0.5f); }
    char  nameCharAt(int pos) const;
    void  setNameCharAt(int pos, char c);
    void  patchLabel(int index, char* dst, size_t n) const;
    const char* nameOf(int id) const;
    int   pageCount() const;
    bool  isPresetList() const;

    J6_Midi& midi_;

    int      section_ = 0;      /* cursor in the section list */
    int      page_    = 0;      /* page within the section    */
    bool     inSection_ = false;/* false = section list, true = a page */

    uint8_t  midiCh_  = J6_MIDI_OMNI;
    /*
     * The name of the sound that is actually loaded -- not of the list entry
     * the cursor happens to be on. Those are two different things: moving onto
     * a free memory deliberately loads nothing, so the sound keeps playing while
     * program_ has already moved. Taking the name from program_ meant asking an
     * empty memory what it was called, which is how every patch saved after
     * browsing for a free slot ended up named "Init".
     */
    char     soundName_[J6_PATCH_NAME_LEN] = "Init";
    int      namePos_ = 0;      /* cursor on the PATCH NAME page         */

    int      writeSlot_   = 0;  /* destination on the PATCH WRITE page   */
    bool     writeErase_  = false; /* false = store, true = free it      */
    int      writeInfo_   = 0;  /* 0 idle, 1 written, 2 freed, 3 failed  */
    int32_t  program_ = 0;

    /* The patch as it was loaded, for the edited marker. Compared per mille,
     * the resolution a patch is actually stored at: equal there means writing
     * would give back the same patch, which is exactly what "unedited" means. */
    float    baseline_[JUNO_PARAM_COUNT] = {};

    /* Shadow copies of the engine parameters, 0..1 */
    /* Patch parameters and instrument settings alike; syncFromProgram only
     * overwrites the first JUNO_PARAM_COUNT of them. */
    float    shadow_[JUNO_TOTAL_COUNT] = {};
};

#endif /* J6_CONTROLLER_H */

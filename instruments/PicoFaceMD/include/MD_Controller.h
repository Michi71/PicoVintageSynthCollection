/*
  MD_Controller.h -- front panel logic for PicoFaceMD

  The Model D has forty-odd controls. Laid out flat, as the Solina was, that
  is nearly thirty screens to page through to reach the filter. So the panel
  is a two-level menu instead, and the sections are the ones printed on the
  instrument:

      PRESET       the preset list
      CONTROLLERS  tune, glide, modulation mix, the two mod switches
      OSCILLATOR   range, waveform and frequency of the three oscillators
      MIXER        volume and switch per source, plus the feedback path
      MODIFIERS    filter and both contour generators
      OUTPUT       main volume and the A-440 tone
      VINTAGE      drift, drive and tone -- trimmers in the original
      SYSTEM       receive channel and transposition

  Three encoders as in the master project:

      encoder 1   the section, at the top level; the page within it below
      button 1    down into a section, and back out again
      encoder 2   the left-hand parameter of the page
      encoder 3   the right-hand parameter

  The page layout is a table in MD_Controller.cpp -- reordering it means
  moving a line there, not changing code.

  The controller only holds shadow copies for the display; the single source
  of truth is the engine on the audio side, reached through the IPC ring.
*/

#ifndef MD_CONTROLLER_H
#define MD_CONTROLLER_H

#include <cstdint>
#include <cstddef>
#include "moog/moog.h"
#include "MD_Midi.h"
#include "md_ipc.h"
#include "md_settings.h"

/* Pseudo parameter IDs above the engine parameters */
enum {
    MD_UI_PROGRAM = MOOG_PARAM_COUNT,  /* preset 0..MOOG_NPROGRAMS-1        */
    MD_UI_MIDICH,                       /* receive channel 0..15, 16 = omni  */
    MD_UI_NONE,                         /* empty slot on a page              */
    MD_UI_COUNT
};

/* How the current screen wants to be drawn. */
enum MdViewKind {
    MD_VIEW_PAGE = 0,   /* header plus two labelled values */
    MD_VIEW_LIST        /* header plus three rows with a cursor */
};

class MD_Controller
{
public:
    explicit MD_Controller(MD_Midi& midi);

    /* --- Input ---------------------------------------------------------- */
    void onEncoder1(int delta);   /* section, or page within a section */
    void onEncoder2(int delta);   /* parameter A */
    void onEncoder3(int delta);   /* parameter B */

    /* Push button of the first encoder: into the section under the cursor,
     * or back out to the section list. Returns true if anything changed. */
    bool onSelectButton();

    /* --- Display -------------------------------------------------------- */
    MdViewKind  viewKind() const;
    const char* title() const;                       /* header, left  */
    void        counterText(char* dst, size_t n) const; /* header, right */

    /* MD_VIEW_PAGE */
    const char* paramAName() const;
    const char* paramBName() const;
    void        paramAText(char* dst, size_t n) const;
    void        paramBText(char* dst, size_t n) const;

    /* MD_VIEW_LIST */
    int  listCount() const;
    int  listCursor() const;
    void listEntry(int index, char* dst, size_t n) const;

    uint8_t midiChannel() const { return midiCh_; }

    /* --- Persistence ----------------------------------------------------- */
    void exportSettings(MdSettingsV1& s) const;
    void importSettings(const MdSettingsV1& s);

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
    const char* nameOf(int id) const;
    int   pageCount() const;
    bool  isPresetList() const;

    MD_Midi& midi_;

    int      section_ = 0;      /* cursor in the section list */
    int      page_    = 0;      /* page within the section    */
    bool     inSection_ = false;/* false = section list, true = a page */

    uint8_t  midiCh_  = MD_MIDI_OMNI;
    int32_t  program_ = 0;

    /* Shadow copies of the engine parameters, 0..1 */
    float    shadow_[MOOG_PARAM_COUNT] = {};
};

#endif /* MD_CONTROLLER_H */

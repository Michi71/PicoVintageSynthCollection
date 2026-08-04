/*
  SM_Controller.h -- front panel logic for PicoFaceSM

  Three encoders as in the master project: the first pages through the
  screens, the other two operate the two parameters of the current page.

  The page layout is a table in SM_Controller.cpp -- reordering it means
  moving a line there, not changing code.

  The controller only holds shadow copies for the display; the single source
  of truth is the engine on the audio side, reached through the IPC ring.
*/

#ifndef SM_CONTROLLER_H
#define SM_CONTROLLER_H

#include <cstdint>
#include "solina/solina.h"
#include "SM_Midi.h"
#include "sm_ipc.h"
#include "sm_settings.h"

/* Pseudo parameter IDs above the engine parameters */
enum {
    SM_UI_PROGRAM = SOLINA_PARAM_COUNT,   /* factory program 0..7 */
    SM_UI_MIDICH,                          /* receive channel 0..15, 16 = omni */
    SM_UI_COUNT
};

class SM_Controller
{
public:
    explicit SM_Controller(SM_Midi& midi);

    void onEncoder1(int delta);   /* page */
    void onEncoder2(int delta);   /* parameter A */
    void onEncoder3(int delta);   /* parameter B */

    /* Push button of the first encoder: back to the first page.
     * Returns true if that actually changed anything. */
    bool homePage();

    int         currentPage() const { return page_; }
    int         pageCount() const;
    const char* pageName() const;

    const char* paramAName() const;
    const char* paramBName() const;
    void        paramAText(char* dst, size_t n) const;
    void        paramBText(char* dst, size_t n) const;

    uint8_t     midiChannel() const { return midiCh_; }

    /* Persistence */
    void exportSettings(SmSettingsV1& s) const;
    void importSettings(const SmSettingsV1& s);

    /* Bring the shadow copies up to date after a MIDI program change */
    void syncFromProgram(int32_t program);

private:
    void  adjust(int slot, int delta);       /* slot 0 = A, 1 = B */
    int   paramIdOf(int slot) const;
    void  sendParam(int id, float v);
    void  formatValue(int id, char* dst, size_t n) const;

    SM_Midi& midi_;
    int      page_ = 0;
    uint8_t  midiCh_ = SM_MIDI_OMNI;
    int32_t  program_ = 2;

    /* Shadow copies of the engine parameters, 0..1 */
    float    shadow_[SOLINA_PARAM_COUNT] = {};
};

#endif /* SM_CONTROLLER_H */

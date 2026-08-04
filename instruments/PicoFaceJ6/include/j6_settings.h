/*
  j6_settings.h -- persisted front panel state

  Payload for the veeprom append log. What is stored is the preset, the
  receive channel and every engine parameter in per mille (0..1000), which
  keeps the record small and stable across versions.

  Raise J6_SETTINGS_VERSION on any layout change -- veeprom discards records
  whose version does not match.
*/

#ifndef J6_SETTINGS_H
#define J6_SETTINGS_H

#include <stdint.h>
#include "juno/juno.h"

/* Version 3: the arpeggiator arrived, and the master volume moved from a field
 * of its own into the parameter array -- the array now covers the instrument
 * settings as well as the patch, so both are stored the same way.
 * Version 2: master volume added. It is stored alongside the patch parameters
 * rather than among them, because it is an instrument setting and a patch
 * change must not overwrite it.
 * Version 1: first layout for this project. The arpeggiator will add
 * parameters and raise this again. */
#define J6_SETTINGS_VERSION 3u

struct __attribute__((packed)) J6SettingsV1 {
    uint8_t  program;                      /* 0..JUNO_NPROGRAMS-1     */
    uint8_t  midiCh;                       /* 0..15, 16 = omni        */
    uint16_t param[JUNO_TOTAL_COUNT];      /* 0..1000 per mille each  */
};

/* veeprom carries 240 bytes of payload per record. This guard catches the
 * case where the parameter list outgrows that limit. */
static_assert(sizeof(J6SettingsV1) <= 240,
              "J6SettingsV1 no longer fits into a veeprom record");

#endif /* J6_SETTINGS_H */

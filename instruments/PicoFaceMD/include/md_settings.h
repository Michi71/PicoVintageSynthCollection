/*
  md_settings.h -- persisted front panel state

  Payload for the veeprom append log. What is stored is the preset, the
  receive channel and every engine parameter in per mille (0..1000), which
  keeps the record small and stable across versions.

  Raise MD_SETTINGS_VERSION on any layout change -- veeprom discards records
  whose version does not match.
*/

#ifndef MD_SETTINGS_H
#define MD_SETTINGS_H

#include <stdint.h>
#include "moog/moog.h"

/* Version 4: the engine changed from the ARP Solina to the Minimoog Model D.
 * Nothing about the old records is salvageable -- different parameters, a
 * different count, different meanings -- so the version number is what stops
 * a board flashed over an earlier build from loading nonsense. */
/* Version 5: the effects section added fourteen parameters, which shifts
 * nothing but lengthens the record. */
#define MD_SETTINGS_VERSION 5u

struct __attribute__((packed)) MdSettingsV1 {
    uint8_t  program;                      /* 0..MOOG_NPROGRAMS-1     */
    uint8_t  midiCh;                       /* 0..15, 16 = omni        */
    uint16_t param[MOOG_PARAM_COUNT];      /* 0..1000 per mille each  */
};

/* veeprom carries 240 bytes of payload per record. This guard catches the
 * case where the parameter list outgrows that limit. */
static_assert(sizeof(MdSettingsV1) <= 240,
              "MdSettingsV1 no longer fits into a veeprom record");

#endif /* MD_SETTINGS_H */

/*
  sm_settings.h -- persisted front panel state

  Payload for the veeprom append log. What is stored is the factory program,
  the receive channel and every engine parameter in per mille (0..1000), which
  keeps the record small and stable across versions.

  Raise SM_SETTINGS_VERSION on any layout change -- veeprom discards records
  whose version does not match.
*/

#ifndef SM_SETTINGS_H
#define SM_SETTINGS_H

#include <stdint.h>
#include "solina/solina.h"

#define SM_SETTINGS_VERSION 3u

/* Version 3: ensemble stereo width added.
 * Version 2: the parameter list gained the phaser (three entries), which
 * shifts every index from SOLINA_TONE_LOWPASS onwards. Older records are
 * discarded by veeprom on the strength of the version number. */
struct __attribute__((packed)) SmSettingsV1 {
    uint8_t  program;                        /* 0..7                  */
    uint8_t  midiCh;                         /* 0..15, 16 = omni      */
    uint16_t param[SOLINA_PARAM_COUNT];      /* 0..1000 per mille ea. */
};

/* veeprom carries 240 bytes of payload per record. This guard catches the
 * case where the parameter list outgrows that limit. */
static_assert(sizeof(SmSettingsV1) <= 240,
              "SmSettingsV1 no longer fits into a veeprom record");

#endif /* SM_SETTINGS_H */

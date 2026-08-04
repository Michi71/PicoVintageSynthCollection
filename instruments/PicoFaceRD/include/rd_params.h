#ifndef RD_PARAMS_H
#define RD_PARAMS_H

#include <stdint.h>

/*
 * shared parameter IDs between UI controller on Core 1 and audio engine on Core 0 of a Roland MKS-20/MK-80 emulation;
 * values travel over IPC as uint16 0..255 and are normalized to float 0..1 as val/255.0f on the receiving side;
 * RD_PARAM_INSTRUMENT=0x7F lives separately in RD_Midi.h
 */
enum RdParamId : uint8_t { RD_PARAM_VOLUME=0, RD_PARAM_CHORUS_ON, RD_PARAM_CHORUS_RATE, RD_PARAM_CHORUS_DEPTH, RD_PARAM_TREM_ON, RD_PARAM_TREM_RATE, RD_PARAM_TREM_DEPTH, RD_PARAM_BASS, RD_PARAM_TREBLE, RD_PARAM_DAC_FILTER_ON, RD_PARAM_PHASER_ON, RD_PARAM_PHASER_RATE, RD_PARAM_PHASER_DEPTH, RD_PARAM_VOICE_MODE, RD_PARAM_MASTER_TUNE, RD_PARAM_COUNT };

#endif // RD_PARAMS_H

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
 * settings.cpp - Persistent settings management for PicoFaceCP firmware.
 *
 * This module handles saving and restoring synthesizer and effects parameters
 * to virtual EEPROM. Restore happens in one pass from init().
 *
 * Autosave is polled from uiTick(). It uses a debounce policy: changes must
 * remain stable for 2 seconds before being written to flash, preventing
 * excessive writes during continuous parameter sweeps.
 */

#include "settings.h"
#include "veeprom.h"
#include "mdaEPiano.h"
#include "reface_cp_chain.h"
#include "midi_reface.h"
#include "picoface/settings_autosave.h"
#include <string.h>
#include "pico/time.h"

extern "C" void ui_set_octave(int oct);
extern "C" int  ui_get_octave(void);

static picoface::SettingsAutosave<SettingsV1> g_autosave;

static void settings_gather(SettingsV1* s, mdaEPiano* ep, RefaceCpChain* fx, RefaceMidi* rm) {
    memset(s, 0, sizeof(*s));
    s->instrument = (uint8_t)ep->getCurrentInstrument();
    s->octave = (int8_t)ui_get_octave();
    s->twMode = (uint8_t)fx->getTremWahMode();
    s->cpMode = (uint8_t)fx->getChoPhaMode();
    s->dlyMode = (uint8_t)fx->getDelayMode();
    rm->getSystemBlock(s->sysBlock);
    for (int i = 0; i < SETTINGS_ENGINE_PARAMS; ++i) {
        s->engineParams[i] = ep->getParameter(i);
    }
    s->drive = fx->getDrive();
    s->twDepth = fx->getTremWahDepth();
    s->twRate = fx->getTremWahRate();
    s->cpDepth = fx->getChoPhaDepth();
    s->cpSpeed = fx->getChoPhaSpeed();
    s->dlyDepth = fx->getDelayDepth();
    s->dlyTime = fx->getDelayTime();
    s->reverb = fx->getReverbDepth();
    s->volume = fx->getVolume();
    s->preGain = fx->getPreGain();
}

void settings_boot_restore(mdaEPiano* ep, RefaceCpChain* fx, RefaceMidi* rm) {
    SettingsV1 s;
    uint16_t len = 0, ver = 0;
    if (!veeprom_load(&s, sizeof(s), &len, &ver)) return;
    if (ver != SETTINGS_VERSION || len != sizeof(SettingsV1)) return;

    if (s.instrument > 5) s.instrument = 0;
    if (s.octave < -2) s.octave = -2;
    if (s.octave > 2) s.octave = 2;

    ep->setInstrument(s.instrument);
    for (int i = 0; i < SETTINGS_ENGINE_PARAMS; ++i) {
        ep->setParameter(i, s.engineParams[i]);
    }
    fx->setVoiceType((int)ep->getCurrentInstrument());
    fx->setDrive(s.drive);
    fx->setTremWahMode(s.twMode);
    fx->setTremWahDepth(s.twDepth);
    fx->setTremWahRate(s.twRate);
    fx->setChoPhaMode(s.cpMode);
    fx->setChoPhaDepth(s.cpDepth);
    fx->setChoPhaSpeed(s.cpSpeed);
    fx->setDelayMode(s.dlyMode);
    fx->setDelayDepth(s.dlyDepth);
    fx->setDelayTime(s.dlyTime);
    fx->setReverbDepth(s.reverb);
    fx->setVolume(s.volume);
    fx->setPreGain(s.preGain);

    ui_set_octave(s.octave);
    rm->loadSystemBlock(s.sysBlock);
}

void settings_task(mdaEPiano* ep, RefaceCpChain* fx, RefaceMidi* rm) {
    g_autosave.task(to_ms_since_boot(get_absolute_time()), SETTINGS_VERSION,
                    [&](SettingsV1& s) { settings_gather(&s, ep, fx, rm); });
}

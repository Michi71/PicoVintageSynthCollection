/*
 * settings.cpp - Persistent settings management for PicoFaceCP firmware.
 *
 * This module handles saving and restoring synthesizer and effects parameters
 * to virtual EEPROM. The restore process is split across cores:
 * - Core 0 restores the engine and FX chain before multicore launch.
 * - Core 1 restores the UI octave and Reface MIDI system block after RefaceMidi initialization.
 *
 * Autosave is polled on core 1. It uses a debounce policy: changes must remain
 * stable for 2 seconds before being written to flash, preventing excessive writes
 * during continuous parameter sweeps.
 */

#include "settings.h"
#include "veeprom.h"
#include "mdaEPiano.h"
#include "reface_cp_chain.h"
#include "midi_reface.h"
#include <string.h>
#include "pico/time.h"

extern "C" void ui_set_octave(int oct);
extern "C" int  ui_get_octave(void);

static SettingsV1 g_loaded;
static bool g_loadedValid = false;
static SettingsV1 g_lastSaved;
static bool g_baselineInit = false;
static SettingsV1 g_pending;
static bool g_pendingActive = false;
static uint32_t g_pendingSinceMs = 0;
static uint32_t g_lastPollMs = 0;

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

void settings_boot_restore_core0(mdaEPiano* ep, RefaceCpChain* fx) {
    veeprom_init();
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

    g_loaded = s;
    g_loadedValid = true;
}

void settings_boot_restore_core1(RefaceMidi* rm) {
    if (!g_loadedValid) return;
    ui_set_octave(g_loaded.octave);
    rm->loadSystemBlock(g_loaded.sysBlock);
}

void settings_task(mdaEPiano* ep, RefaceCpChain* fx, RefaceMidi* rm) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - g_lastPollMs < 250) return;
    g_lastPollMs = now;

    SettingsV1 cur;
    settings_gather(&cur, ep, fx, rm);

    if (!g_baselineInit) {
        g_lastSaved = cur;
        g_baselineInit = true;
        return;
    }

    if (memcmp(&cur, &g_lastSaved, sizeof(cur)) == 0) {
        g_pendingActive = false;
        return;
    }

    if (!g_pendingActive || memcmp(&cur, &g_pending, sizeof(cur)) != 0) {
        g_pending = cur;
        g_pendingSinceMs = now;
        g_pendingActive = true;
        return;
    }

    if (now - g_pendingSinceMs >= 2000) {
        if (veeprom_save(&g_pending, sizeof(g_pending), SETTINGS_VERSION)) {
            g_lastSaved = g_pending;
        }
        g_pendingActive = false;
    }
}

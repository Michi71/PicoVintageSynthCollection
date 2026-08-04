/*
 * settings.cpp - Persistent settings management for PicoFaceDX firmware.
 *
 * This module handles saving and restoring the current DX patch, the master
 * volume and the MIDI system settings to virtual EEPROM. Restore happens once
 * from the adapter's init(), before the audio pool is up; autosave is polled
 * from uiTick(). Both run on core0.
 *
 * Autosave uses a debounce policy: changes must remain stable for 2 seconds
 * before being written to flash, preventing excessive writes during continuous
 * parameter sweeps.
 */

#include "settings.h"
#include "veeprom.h"
#include "DX_Synth_Bridge.h"
#include "midi_reface.h"
#include <string.h>
#include "pico/time.h"

extern "C" void ui_set_octave(int oct);
extern "C" int  ui_get_octave(void);
extern "C" void ui_set_master_volume(int vol);
extern "C" int  ui_get_master_volume(void);

static SettingsV3 g_lastSaved;
static bool g_baselineInit = false;
static SettingsV3 g_pending;
static bool g_pendingActive = false;
static uint32_t g_pendingSinceMs = 0;
static uint32_t g_lastPollMs = 0;

static void settings_gather(SettingsV3* s, DX_Synth_Bridge* dx, RefaceMidi* rm) {
    memset(s, 0, sizeof(*s));
    s->octave = (int8_t)ui_get_octave();
    rm->getSystemBlock(s->sysBlock);
    s->patch = dx->patch();
    s->masterVolume = (uint8_t)ui_get_master_volume();
}

void settings_boot_restore(DX_Synth_Bridge* dx, RefaceMidi* rm) {
    // veeprom_init() is the core's job now; it runs before the instrument's
    // init(), so the record is readable here.
    SettingsV3 s;
    uint16_t len = 0, ver = 0;
    if (!veeprom_load(&s, sizeof(s), &len, &ver)) return;

    if (ver == SETTINGS_VERSION && len == sizeof(SettingsV3)) {
        // current layout, nothing to do
    } else if (ver == SETTINGS_VERSION_V2 && len == sizeof(SettingsV2)) {
        // Legacy record: V3 is a pure append, so everything up to .patch was
        // already read into the right place; only the new field is missing.
        s.masterVolume = SETTINGS_MASTER_VOLUME_DEFAULT;
    } else {
        return;
    }

    if (s.octave < -2) s.octave = -2;
    if (s.octave > 2) s.octave = 2;
    if (s.masterVolume > 100) s.masterVolume = 100;

    dx->patch() = s.patch;
    // Snapped rather than slewed so a stored low volume is already in effect on
    // the first block the core renders -- no boot-time blast.
    dx->setMasterVolume(s.masterVolume, /*snap=*/true);

    ui_set_octave(s.octave);
    // Seeds the UI mirror that the panel and the autosave snapshot read. It
    // also pushes one master-volume packet onto the ring, which the first
    // render() applies to the value that is already in place.
    ui_set_master_volume(s.masterVolume);
    rm->loadSystemBlock(s.sysBlock);
}

void settings_task(DX_Synth_Bridge* dx, RefaceMidi* rm) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - g_lastPollMs < 250) return;
    g_lastPollMs = now;

    SettingsV3 cur;
    settings_gather(&cur, dx, rm);

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


#include "settings.h"
#include "veeprom.h"
#include "YC_Synth_Bridge.h"
#include "midi_reface.h"
#include <string.h>
#include "pico/time.h"

static SettingsV2 g_lastSaved;
static bool g_baselineInit = false;
static SettingsV2 g_pending;
static bool g_pendingActive = false;
static uint32_t g_pendingSinceMs = 0;
static uint32_t g_lastPollMs = 0;

static void settings_gather(SettingsV2* s, YC_Synth_Bridge* yc, RefaceMidi* rm) {
    memset(s, 0, sizeof(*s));
    const yc_engine_state_t& st = yc->state();
    s->panel.wave = st.wave;
    s->panel.octave = st.octave;
    for (int i = 0; i < 9; i++) s->panel.footage[i] = st.footage[i];
    s->panel.perc_on = st.perc_on;
    s->panel.perc_type = st.perc_type;
    s->panel.perc_length = st.perc_length;
    s->panel.vibcho_select = st.vibcho_select;
    s->panel.vibcho_depth = st.vibcho_depth;
    s->panel.rotary_speed = st.rotary_speed;
    s->panel.distortion = st.distortion;
    s->panel.reverb = st.reverb;
    s->panel.volume = st.volume;
    s->panel.midi_ctrl_mode = rm->midiControlEnabled() ? 1 : 0;
}

void settings_boot_restore(YC_Synth_Bridge* yc, RefaceMidi* rm) {
    SettingsV2 s;
    uint16_t len = 0, ver = 0;
    if (!veeprom_load(&s, sizeof(s), &len, &ver)) return;
    if (ver != SETTINGS_VERSION || len != sizeof(SettingsV2)) return;
    yc_engine_state_t& st = yc->state();
    st.wave = s.panel.wave;
    st.octave = s.panel.octave;
    for (int i = 0; i < 9; i++) st.footage[i] = s.panel.footage[i];
    st.perc_on = s.panel.perc_on;
    st.perc_type = s.panel.perc_type;
    st.perc_length = s.panel.perc_length;
    st.vibcho_select = s.panel.vibcho_select;
    st.vibcho_depth = s.panel.vibcho_depth;
    st.rotary_speed = s.panel.rotary_speed;
    st.distortion = s.panel.distortion;
    st.reverb = s.panel.reverb;
    st.volume = s.panel.volume;
    st.vol_gain = (float)st.volume / 127.0f;
    yc_wavetable_select(st.wave);
    rm->setMidiControlEnabled(s.panel.midi_ctrl_mode != 0);
}

void settings_task(YC_Synth_Bridge* yc, RefaceMidi* rm) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - g_lastPollMs < 250) return;
    g_lastPollMs = now;

    SettingsV2 cur;
    settings_gather(&cur, yc, rm);

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

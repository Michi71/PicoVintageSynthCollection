// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71


#include "settings.h"
#include "veeprom.h"
#include "YC_Synth_Bridge.h"
#include "midi_reface.h"
#include "picoface/settings_autosave.h"
#include <string.h>
#include "pico/time.h"

static picoface::SettingsAutosave<SettingsV2> g_autosave;

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
    g_autosave.task(to_ms_since_boot(get_absolute_time()), SETTINGS_VERSION,
                    [&](SettingsV2& s) { settings_gather(&s, yc, rm); });
}

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
"""Drive Roland's JUNO-60 VST3 from the J6 engine's 29 patch parameters.

The plugin exposes its whole panel as parameters, so both sides can be given
the same front panel rather than the same patch number. What the parameters
mean is not written down anywhere, so every line of the map below was
measured against the running plugin; the measurement is named at the line.
The probes live in the session scratchpad, the results in the PicoFaceJ6
README.

Three findings shape the map and are easy to get wrong:

  * the modulation depths are BIPOLAR with 128 as the centre -- DCO LFO,
    VCF LFO, VCF ENV and VCF key follow all do nothing at 128 and reach full
    depth at 0 and 255. Writing 0 for "no modulation" detunes the oscillator
    by nearly four semitones, which is how this was found.
  * env1 is the filter contour and env2 the amplifier contour. A Juno-60 has
    one contour generator for both, so both are written from the same four
    parameters.
  * the plugin renders silence at 32 kHz. 44.1 kHz is the engine's own rate
    anyway, so nothing is resampled on either side.
"""
import numpy as np
from pedalboard import load_plugin
from mido import Message

PLUGIN = '/Library/Audio/Plug-Ins/VST3/Roland/JUNO-60.vst3'
SR = 44100.0

# The J6 parameter enum, so the map reads like juno_params.h.
(LFO_RATE, LFO_DELAY, DCO_RANGE, DCO_LFO, DCO_PWM, DCO_PWM_MODE, DCO_SAW,
 DCO_PULSE, DCO_SUB, DCO_SUB_LEVEL, DCO_NOISE, HPF, VCF_FREQ, VCF_RES, VCF_ENV,
 VCF_POLARITY, VCF_LFO, VCF_KYBD, VCA_LEVEL, VCA_MODE, ENV_ATTACK, ENV_DECAY,
 ENV_SUSTAIN, ENV_RELEASE, CHORUS, TUNE, BEND_RANGE, LFO_TRIG,
 TRANSPOSE) = range(29)
NPARAM = 29


def _b(v):
    """A 0..1 panel value as one of the plugin's 0..255 bytes."""
    return float(min(max(round(v * 255.0), 0), 255))


def _bip(v):
    """A 0..1 unipolar panel value on the plugin's bipolar 0..255 axis."""
    return float(min(max(round(128.0 + v * 127.0), 0), 255))


def _step(v, steps):
    s = int(v * steps)
    return min(max(s, 0), steps - 1)


class J6VST:
    def __init__(self, path=PLUGIN):
        self.p = load_plugin(path)
        self.ranges = {k: self.p.parameters[k].range for k in self.p.parameters}
        assert 'dco_saw_level' in self.ranges and 'env2_release' in self.ranges

    def _set(self, name, value, log):
        lo, hi, _ = self.ranges[name]
        v = float(value)
        if v < lo or v > hi:
            log.append(f'{name}={value:.0f} clamped to [{lo:.0f},{hi:.0f}]')
            v = min(max(v, lo), hi)
        setattr(self.p, name, v)

    def set_patch(self, q, dry=False):
        """q: the 29 normalised J6 patch parameters. Returns the clamp log."""
        assert len(q) == NPARAM
        log = []
        S = lambda n, v: self._set(n, v, log)

        # --- LFO. Rate and delay are straight bytes; the laws behind them are
        # what the comparison is for.
        S('lfo_rate', _b(q[LFO_RATE]))
        S('lfo_delay_time', _b(q[LFO_DELAY]))
        # TRIG: the panel's Auto is step 0, and it retriggers on a new phrase.
        S('lfo_key_trig', 1.0 if _step(q[LFO_TRIG], 2) == 0 else 0.0)
        S('lfo_trig_env', 0.0)

        # --- DCO. dco_range is six octaves with 3 = 8' (measured: note 60
        # sounds 261.61 Hz there, and each step is an exact octave), so the
        # Juno's 16'/8'/4' are 2/3/4.
        S('dco_range', 2 + _step(q[DCO_RANGE], 3))
        S('dco_lfo_mod', _bip(q[DCO_LFO]))
        S('dco_pwm_depth', _b(q[DCO_PWM]))
        # PWM source: 1 modulates from the LFO, 2 from the contour, 0 holds
        # still. Positions 3..5 repeat 2 and 0.
        S('dco_pwm_source', (1, 0, 2)[_step(q[DCO_PWM_MODE], 3)])
        S('dco_saw_level', 255.0 if q[DCO_SAW] >= 0.5 else 0.0)
        S('dco_pwm_level', 255.0 if q[DCO_PULSE] >= 0.5 else 0.0)
        S('dco_sub_level', _b(q[DCO_SUB_LEVEL]) if q[DCO_SUB] >= 0.5 else 0.0)
        S('dco_noise_level', _b(q[DCO_NOISE]))

        # --- HPF. The plugin's high-pass is a continuous 0..255 where the
        # instrument has four detents, so the four positions are read as the
        # four ends of that travel. Its corners come out about 1.5x above the
        # ones the service notes' component values give (see the README).
        S('hpf_cutoff_freq', (0.0, 85.0, 170.0, 255.0)[_step(q[HPF], 4)])
        S('hpf_type', 0.0)

        # --- VCF. Contour amount carries its polarity in the sign about 128,
        # which is exactly what the panel's polarity switch is.
        S('vcf_cutoff_freq', _b(q[VCF_FREQ]))
        S('vcf_resonance', _b(q[VCF_RES]))
        pol = 1.0 if _step(q[VCF_POLARITY], 2) == 1 else -1.0
        S('vcf_env_mod', min(max(128.0 + pol * q[VCF_ENV] * 127.0, 0), 255))
        S('vcf_lfo_mod', _bip(q[VCF_LFO]))
        S('vcf_key_follow', _bip(q[VCF_KYBD]))   # 128 = 0 %, 255 = 100 %
        S('vcf_velocity_sens', 0.0)              # the keyboard is a gate

        # --- VCA. Three positions, and none of them is what the names
        # suggest: 0 hangs the amplifier on env1, 1 on env2, and only 2 is the
        # plain gate. Reading 0 as the gate makes every patch whose contour
        # sustains at zero -- the three organs among them -- die away instead
        # of holding.
        S('vca_level', _b(q[VCA_LEVEL]))
        S('vca_mode', 1.0 if _step(q[VCA_MODE], 2) == 0 else 2.0)
        S('vca_velocity_sens', 0.0)
        S('vca_tone', 128.0)

        # --- ENV. One contour on the instrument, two in the plugin.
        for pre in ('env1', 'env2'):
            S(pre + '_attack', _b(q[ENV_ATTACK]))
            S(pre + '_decay', _b(q[ENV_DECAY]))
            S(pre + '_sustain', _b(q[ENV_SUSTAIN]))
            S(pre + '_release', _b(q[ENV_RELEASE]))

        # --- Chorus. Types 2, 3 and 4 modulate at roughly 0.9, 1.7 and 9.2 Hz
        # and the third is all but mono -- Roland's I, II and I+II. Off is
        # depth 0, which leaves the dry signal untouched whatever the type.
        ch = _step(q[CHORUS], 4)
        S('effect_type', (2, 2, 3, 4)[ch])
        S('effect_depth', 0.0 if ch == 0 or dry else 255.0)
        S('reverb_level', 0.0)      # not on a Juno-60
        S('delay_level', 0.0)
        S('reverb_direct_level', 255.0)
        S('delay_direct_level', 255.0)

        # --- Rear panel and things that must not drift into the measurement.
        cents = (q[TUNE] - 0.5) * 2.0 * 50.0
        S('master_tune', 100.0 + cents / 0.4)    # 0.4 cents per unit, +/-40
        S('portamento', 0.0)
        S('legato', 0.0)
        S('assign_mode', 0.0)
        S('condition', 0.0)         # no circuit ageing, so renders repeat
        S('circuit_mod', 0.0)
        S('arpeggio_sw', 0.0)
        S('key_hold', 0.0)
        S('tempo_sync', 0.0)
        S('bend_gain', 0.0)
        return log

    def render(self, q, note=60, vel=100, hold=1.5, tail=2.0):
        """Note as played; the patch's octave transpose moves the note itself,
        because the plugin's own octave_shift does nothing."""
        note += (_step(q[TRANSPOSE], 5) - 2) * 12
        msgs = [Message('note_on', note=note, velocity=vel, time=0.0),
                Message('note_off', note=note, velocity=0, time=hold)]
        return self.p(msgs, duration=hold + tail, sample_rate=SR, reset=True)


def read_bank(path):
    """The bank render_note dumps: (name, [29 floats]) per line."""
    out = []
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        out.append((f[1], [float(x) for x in f[2:2 + NPARAM]]))
    return out

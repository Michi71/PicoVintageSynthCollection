"""Drive Roland's D-50 VST3 from the 448-byte D-50 patch format (pedalboard host)."""
import numpy as np
from pedalboard import load_plugin
from mido import Message

PLUGIN = '/Library/Audio/Plug-Ins/VST3/Roland/D-50.vst3'

class D50VST:
    def __init__(self, path=PLUGIN):
        self.p = load_plugin(path)
        self.names = list(self.p.parameters.keys())
        n = self.names
        assert n[0] == 'upper1_wg_coarse_tune' and n[54] == 'upper2_wg_coarse_tune' and n[108] == 'upper_structure'
        assert n[146] == 'lower1_wg_coarse_tune' and n[200] == 'lower2_wg_coarse_tune' and n[254] == 'lower_structure'
        assert n[292] == 'key_mode' and n[310] == 'chase_time' and n[145] == 'upper_partial_bal'
        self.ranges = {k: self.p.parameters[k].range for k in n}

    def _set(self, name, value, log):
        lo, hi, _ = self.ranges[name]
        v = float(value)
        if v < lo or v > hi:
            log.append(f'{name}={value} clamped to [{lo:.0f},{hi:.0f}]'); v = min(max(v, lo), hi)
        setattr(self.p, name, v)

    def set_patch(self, b):
        """b: 448 bytes in SysEx order UP1 UP2 UC LP1 LP2 LC PATCH (64 each)."""
        assert len(b) == 448
        log = []
        for tone, base, pidx in (('upper', 0, 0), ('lower', 192, 146)):
            for k in range(54): self._set(self.names[pidx + k], b[base + k], log)
            for k in range(54): self._set(self.names[pidx + 54 + k], b[base + 64 + k], log)
            for k in range(38): self._set(self.names[pidx + 108 + k], b[base + 128 + 10 + k], log)
        for k in range(19): self._set(self.names[292 + k], b[384 + 18 + k], log)
        return log

    def render(self, note=60, vel=127, hold=1.0, tail=2.0, sr=44100.0):
        msgs = [Message('note_on', note=note, velocity=vel, time=0.0), Message('note_off', note=note, velocity=0, time=hold)]
        a = self.p(msgs, duration=hold + tail, sample_rate=sr, reset=True)
        return a  # (2, n)

def name_of(b):
    alpha = ' ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-'
    return ''.join(alpha[c] if c < len(alpha) else '?' for c in b[384:402]).strip()

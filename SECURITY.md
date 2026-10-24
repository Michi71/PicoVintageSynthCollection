# Security policy

## Supported versions

Only the latest release receives fixes. Firmware is updated by flashing a new
`.uf2`, so there are no maintained older branches.

## Reporting a vulnerability

Please do **not** open a public issue. Report it privately through GitHub:
**Security** tab -> **Report a vulnerability**, or directly at
<https://github.com/Michi71/PicoVintageSynthCollection/security/advisories/new>.

Include the instrument, the release or commit, and how to reproduce it --
for example the MIDI or SysEx bytes that trigger it. This is a one-person
project: expect an answer within a couple of weeks, and a fix may take longer.

## Scope

The firmware runs on a standalone RP2350 without a network connection. Its
attack surface is what arrives over USB MIDI and DIN MIDI (notes, controllers,
SysEx, bulk dumps). Malformed input that crashes the firmware, corrupts
stored presets or settings, or writes outside its buffers is in scope. The host tools under `tools/` are development
aids and are not hardened against untrusted input.

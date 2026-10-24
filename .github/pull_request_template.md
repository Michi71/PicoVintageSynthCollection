## What and why

<!-- What does this change, and which issue does it address (Fixes #...)? -->

## Affected instruments

<!-- e.g. PicoFaceMD, PicoFaceDX, or "all (core)" -->

## Hardware test

<!-- What should be checked on the board, step by step. -->

## Untested

<!-- What you could not test: host OS (Windows/Linux), DIN MIDI, 4 MB boards,
     ROM-only instruments (RD, JV-880, D-50), ... -->

## Checklist

- [ ] `tools/host_tests/run_all.sh` passes
- [ ] Front-panel changes rendered with `tools/host_tests/ui` (frames are attached by CI)
- [ ] No ROMs, sample recordings or other copyrighted data in the diff
- [ ] Docs updated where behaviour changed (instrument README, `doc/`, `docs/`)

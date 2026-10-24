# Contributing

Thanks for taking an interest. This is a small project with one maintainer and
one hardware test bench, so the process below is mostly about making changes
testable on real hardware.

## Reporting bugs and asking for features

Open an issue with one of the templates. English and German are both fine.
For a bug, the most useful details are the instrument, the board (and its
flash size), how MIDI reaches it (USB or DIN, and from which host OS), and the
version from the splash screen or the release you flashed.

Before reporting "boots, but no sound" or "unstable at 300 MHz", read the
*Hardware* section of the [README](README.md): the board has to feed the core
from the RP2350's own regulator, and some boards route I2S to other pins.

Security issues go through [SECURITY.md](SECURITY.md), not a public issue.

## Pull requests

1. Branch from `main`; one topic per pull request.
2. Run the host tests before pushing. CI runs the same script, and a release
   needs it green:

   ```bash
   tools/host_tests/run_all.sh
   ```

3. For changes to the front panel, render the screens on the host with
   [tools/host_tests/ui](tools/host_tests/ui/README.md) instead of flashing;
   CI attaches the frames to the pull request.
4. Say in the pull request what should be tested on hardware and what you
   could not test. The maintainer runs the hardware test (on a Mac) before
   merging, so a host-OS-specific behaviour (Windows, Linux) is worth calling
   out explicitly.

Code, comments, commit messages and documentation in the repository are in
English.

## Things that will not be merged

- **Roland ROMs or any other copyrighted sample or ROM data** -- not in a
  commit, not in a branch, not in a fork. `roms/` is ignored for that reason;
  the RD, JV-880 and D-50 build only where a local ROM set exists.
  `tools/check_clean_build.sh` checks that a clean checkout builds without them.
- **A build option to swap the encoder A/B pins.** The pin map in
  [core/include/project_config.h](core/include/project_config.h) follows the
  reference board under [hardware/](hardware/README.md); older hand-wired
  builds swap their wires instead.
- **Clean-ups justified by flash size.** The firmware links with
  `--gc-sections`, so unreferenced code already costs nothing.

## Where to start reading

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) -- the shared core, the
  instrument interface, and the open points in section 8
- [docs/ADDING_AN_INSTRUMENT.md](docs/ADDING_AN_INSTRUMENT.md) -- a new
  instrument in three steps
- `instruments/<name>/README.md` and `instruments/<name>/doc/` -- MIDI charts,
  persistence formats, presets and engineering logs per instrument
- [tools/host_tests/README.md](tools/host_tests/README.md) -- host tests and
  the offline renderers

## License

Contributions are accepted under the repository's license, GPL-3.0-or-later
(see [LICENSE](LICENSE) and the *License* section of the README for the parts
derived from other projects). By opening a pull request you agree to license
your contribution under it.

## Code of conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).

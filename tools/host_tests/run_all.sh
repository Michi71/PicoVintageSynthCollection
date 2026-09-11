#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# run_all.sh -- every host test that needs neither a sound card, a MIDI port
# nor a ROM set, in one go, with one exit status. This is what CI runs (the
# host-tests job in .github/workflows/build.yml) and what to run before a PR.
#
#   tools/host_tests/run_all.sh              all of them
#   tools/host_tests/run_all.sh veeprom d5   only these
#
# Each test's output goes to tools/host_tests/_out/<name>.log; the last lines
# of a failing one are repeated on the terminal. Rendered frames and WAVs land
# where the individual scripts put them (ui/out, md/out, cp/out, next to the
# binaries), so a CI run can upload them and a GUI change can be looked at
# without hardware.
#
# The four CoreAudio/PortMidi players (cp/build.sh, cp/build_cp.sh,
# j6/build_juno.sh, md/build_moog.sh, sm/build_solina.sh) are not here: they
# need a sound card and a person. Neither are the JV and D5 engine renders,
# which need a ROM set.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/_out"
mkdir -p "$OUT"

# --- helpers ----------------------------------------------------------------

# At least one PBM frame in the directory, none of them empty.
frames_ok() {
    local dir=$1 n=0 f
    for f in "$dir"/*.pbm; do
        [ -s "$f" ] || { echo "empty or missing frame: $f"; return 1; }
        n=$((n + 1))
    done
    echo "$n frames in $dir"
}

# A raw int16 render that is not just zeros. (tr instead of cmp -n: BSD cmp
# has no -n, and this must run on the Mac as well as in CI.)
audible() {
    local f=$1 nz
    [ -s "$f" ] || { echo "no output: $f"; return 1; }
    nz=$(tr -d '\000' < "$f" | wc -c | tr -d ' ')
    [ "$nz" -gt 0 ] || { echo "silent render: $f"; return 1; }
    echo "$f: $nz non-zero bytes"
}

# --- the tests --------------------------------------------------------------
# Each is a function; a non-zero return anywhere in it fails the test.

t_veeprom()  { "$HERE/veeprom/build_veeprom.sh"; }          # builds and runs
t_d5()       { "$HERE/d5/build_d5.sh"; }                    # builds and runs three
t_dx_sysex() { "$HERE/dx_sysex/build_dx_sysex.sh"; }        # builds and runs
t_ob()       { "$HERE/ob/build_ob.sh" && "$HERE/ob/ob_engine_host_test"; }
t_j6_ui()    { "$HERE/j6/build_ui.sh" && "$HERE/j6/j6_ui_test"; }
t_yc()       { "$HERE/yc/build_yc.sh" && "$HERE/yc/yc_engine_host_test"; }

t_ui_kit() {
    "$HERE/ui/build_ui_kit.sh" && mkdir -p "$HERE/ui/out" \
        && "$HERE/ui/ui_kit_shots" "$HERE/ui/out" && frames_ok "$HERE/ui/out"
}

# Needs the u8g2 archive that t_ui_kit builds; the order below provides it.
t_md_ui() {
    "$HERE/md/build_md_ui.sh" && mkdir -p "$HERE/md/out" \
        && "$HERE/md/md_ui_shots" "$HERE/md/out" && frames_ok "$HERE/md/out"
}

# One note through each of the six CP voices: a keygroup table pointing past
# its data would crash or render silence here before it reaches a board.
t_cp_render() {
    "$HERE/cp/build_render.sh" && mkdir -p "$HERE/cp/out" || return 1
    local i
    for i in 0 1 2 3 4 5; do
        "$HERE/cp/render" "$i" 60 100 1.0 1.5 "$HERE/cp/out/ci_voice$i.raw" \
            && audible "$HERE/cp/out/ci_voice$i.raw" || return 1
    done
}

ALL="veeprom d5 dx_sysex ob j6_ui yc ui_kit md_ui cp_render"

# --- runner -----------------------------------------------------------------

failed=0
for name in ${*:-$ALL}; do
    if ! declare -F "t_$name" > /dev/null; then
        echo "unknown test '$name' (have: $ALL)" >&2
        failed=$((failed + 1))
        continue
    fi
    log="$OUT/$name.log"
    t0=$(date +%s)
    if ( set -e; "t_$name" ) > "$log" 2>&1; then
        status="pass"
    else
        status="FAIL"
        failed=$((failed + 1))
    fi
    printf '%-10s %s  %3ds\n' "$name" "$status" "$(( $(date +%s) - t0 ))"
    if [ "$status" = FAIL ]; then
        sed 's/^/    | /' "$log" | tail -n 30
    fi
done

if [ "$failed" -eq 0 ]; then
    echo "all host tests passed"
else
    echo "$failed host test(s) FAILED - logs in $OUT"
fi
exit "$failed"

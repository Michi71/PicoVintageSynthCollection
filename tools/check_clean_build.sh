#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Michi71
#
# Builds the whole collection from a fresh clone, the way somebody who just
# found the repository would.
#
#     tools/check_clean_build.sh [--local] [--variants] [--keep] [<dir>]
#
# The point is the ROMs. Three instruments need a local ROM set, and those sets
# are not in the repository and must never be -- so the one thing a build here
# never exercises is whether a stranger with the right ROMs can actually build
# them. Everything else is covered by CI; this is not.
#
# So: clone into a temporary directory, copy the ROMs in from outside git, and
# build. If the instructions have drifted, this is what says so -- when it was
# first run against the RD's documented recipe it found three real faults,
# including a ROM the instructions named that was not in roms/ at all.
#
#   --local     clone from this checkout instead of GitHub (no network; tests
#               what is committed here, including branches not yet pushed)
#   --variants  also build the reduced images: PICOFACEJV_4MB=ON and
#               PICOFACERD_MODEL=MK80. Roughly doubles the time.
#   --keep      leave the temporary tree behind for poking at
#
# <dir> is where to build, and it stays behind afterwards -- for looking at the
# logs, or flashing the .uf2 files straight out of it. Useful too when /tmp is
# small: a full run is well over a gigabyte. Without one it goes to a temporary
# directory that is removed again unless --keep.
#
# A repeat run into the same directory clears it first, because a check that
# starts on leftovers is not checking a clean build. Only a directory a previous
# run left is ever cleared: anything else in the way is reported and left alone.
#
# Where the ROMs come from:
#
#   PICOFACE_ROMS=/path   a directory holding PicoFaceD5/, PicoFaceJV/ and
#                         PicoFaceRD/ subdirectories of ROM files
#   unset                 this checkout's own instruments/*/roms
#
# An instrument whose ROMs are absent is reported as skipped, not as a failure:
# that is the designed behaviour, and it is worth confirming too.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
URL="https://github.com/Michi71/PicoVintageSynthCollection"
ROM_INSTRUMENTS=(PicoFaceD5 PicoFaceJV PicoFaceRD)

local_clone=0; variants=0; keep=0; outdir=""
for arg in "$@"; do
    case "$arg" in
        --local)    local_clone=1 ;;
        --variants) variants=1 ;;
        --keep)     keep=1 ;;
        # Print the comment block above, whatever length it grows to: skip the
        # shebang and the two SPDX lines, then stop at the first line of code.
        -h|--help)  awk 'NR<=3 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' "$0"
                    exit 0 ;;
        -*) echo "check_clean_build: unknown option $arg" >&2; exit 2 ;;
        *)  if [ -n "$outdir" ]; then
                echo "check_clean_build: more than one directory given" >&2; exit 2
            fi
            outdir="$arg" ;;
    esac
done

# The marker is what makes emptying a named directory safe: only a directory
# this script made before is ever cleared, and anything else is left untouched.
MARKER=".picoface-clean-build"

if [ -n "$outdir" ]; then
    if [ -e "$outdir" ] && [ ! -d "$outdir" ]; then
        echo "check_clean_build: $outdir exists and is not a directory" >&2; exit 2
    fi
    if [ -d "$outdir" ] && [ -n "$(ls -A "$outdir" 2>/dev/null)" ]; then
        if [ -f "$outdir/$MARKER" ]; then
            echo "reusing $outdir (clearing the previous run)"
            find "$outdir" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        else
            echo "check_clean_build: $outdir is not empty and was not made by this" >&2
            echo "    script. Empty it yourself or name another directory." >&2
            exit 2
        fi
    fi
    mkdir -p "$outdir" || exit 2
    WORK="$(cd "$outdir" && pwd)"
    : > "$WORK/$MARKER"
    keep=1                       # a directory somebody named is theirs to keep
else
    WORK="$(mktemp -d "${TMPDIR:-/tmp}/picoface-clean.XXXXXX")"
    : > "$WORK/$MARKER"
fi

cleanup () {
    if [ "$keep" -eq 1 ]; then echo "tree kept at $WORK"
    else rm -rf "$WORK"; fi
}
trap cleanup EXIT
TREE="$WORK/PicoVintageSynthCollection"

say () { printf '\n== %s\n' "$*"; }

# ---------------------------------------------------------------- fresh clone
if [ "$local_clone" -eq 1 ]; then
    say "cloning from this checkout"
    git clone --quiet "$REPO" "$TREE" || exit 1
    # A local clone starts on whatever HEAD pointed at; make that explicit.
    branch="$(git -C "$REPO" rev-parse --abbrev-ref HEAD)"
    git -C "$TREE" checkout --quiet "$branch" 2>/dev/null || true
    echo "   branch $branch, commit $(git -C "$TREE" rev-parse --short HEAD)"
else
    say "cloning $URL"
    git clone --quiet "$URL" "$TREE" || exit 1
    echo "   commit $(git -C "$TREE" rev-parse --short HEAD)"
fi

say "submodules"
git -C "$TREE" submodule update --init --recursive --quiet || exit 1
git -C "$TREE" submodule status --recursive | awk '{printf "   %s %s\n", substr($1,1,7), $2}'

# ------------------------------------------------------------------- the ROMs
say "ROM sets"
src_root="${PICOFACE_ROMS:-}"
for inst in "${ROM_INSTRUMENTS[@]}"; do
    if [ -n "$src_root" ]; then src="$src_root/$inst"
    else src="$REPO/instruments/$inst/roms"; fi
    dst="$TREE/instruments/$inst/roms"
    if [ -d "$src" ] && [ -n "$(ls -A "$src" 2>/dev/null)" ]; then
        mkdir -p "$dst"
        # -f follows symlinks, so a ROM directory kept as links into another
        # checkout works as well as one holding the files -- which is how at
        # least one of these is actually kept. Directories and dangling links
        # are skipped rather than erroring.
        for f in "$src"/*; do
            [ -f "$f" ] || continue
            cp -L "$f" "$dst/"
        done
        printf '   %-12s %2d files from %s\n' "$inst" "$(ls -1 "$dst" 2>/dev/null | wc -l | tr -d ' ')" "$src"
    else
        printf '   %-12s none (expected at %s) -- will be skipped\n' "$inst" "$src"
    fi
done

# -------------------------------------------------------------------- the build
build_one () {          # $1 = label, $2.. = extra -D flags
    local label="$1"; shift
    local dir="$WORK/build-$label"
    say "configure + build: $label"
    if ! cmake -S "$TREE" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@" \
            > "$WORK/$label.configure.log" 2>&1; then
        echo "   CONFIGURE FAILED -- $WORK/$label.configure.log"
        tail -15 "$WORK/$label.configure.log" | sed 's/^/     /'
        return 1
    fi
    grep -E 'PicoFace[A-Za-z0-9]+: skipped' "$WORK/$label.configure.log" | sed 's/^-- /   /'
    if ! cmake --build "$dir" > "$WORK/$label.build.log" 2>&1; then
        echo "   BUILD FAILED -- $WORK/$label.build.log"
        grep -iE '\berror\b' "$WORK/$label.build.log" | head -10 | sed 's/^/     /'
        return 1
    fi
    for uf2 in "$dir"/*.uf2; do
        [ -e "$uf2" ] || continue
        local n; n="$(basename "$uf2" .uf2)"
        local bytes; bytes="$(wc -c < "$dir/$n.bin" 2>/dev/null || echo 0)"
        local fits="fits 4 MB"
        [ "$bytes" -ge 4194304 ] && fits="needs a bigger board"
        printf '   %-13s %9d bytes  %s\n' "$n" "$bytes" "$fits"
    done
    return 0
}

rc=0
build_one full || rc=1
if [ "$variants" -eq 1 ]; then
    build_one jv4mb -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceJV -DPICOFACEJV_4MB=ON || rc=1
    build_one rdmk80 -DPICOFACE_INSTRUMENTS_FILTER=PicoFaceRD -DPICOFACERD_MODEL=MK80 || rc=1
fi

# ------------------------------------------------------------------- the verdict
say "result"
built="$(ls -1 "$WORK/build-full"/*.uf2 2>/dev/null | wc -l | tr -d ' ')"
echo "   $built of 10 instruments built"
if [ "$rc" -eq 0 ] && [ "$built" -gt 0 ]; then
    echo "CLEAN BUILD PASS"
else
    echo "CLEAN BUILD FAIL"
    rc=1
fi
exit $rc

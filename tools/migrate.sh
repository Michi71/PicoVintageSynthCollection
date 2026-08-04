#!/usr/bin/env bash
#
# tools/migrate.sh - populates the PicoVintageSynthCollection monorepo
# from the six existing PicoFace repositories.
# Source repositories are READ-ONLY, never modified.

set -euo pipefail

SRC_ROOT="${SRC_ROOT:-$HOME/GitHub}"
DEST="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_DONOR=PicoFaceMD
UI_DONOR=PicoFaceCP
INSTRUMENTS=(PicoFaceYC PicoFaceCP PicoFaceRD PicoFaceJ6 PicoFaceMD PicoFaceSM)
SHARED_HEADERS=(get_serial.h midi_input_usb.h tusb_config.h veeprom.h)
CORE_SRC=(get_serial.c usb_descriptors.c pico_hw.cpp veeprom.cpp midi_input_usb.cpp)
UI_SRC=(pico_frontpanel.cpp pico_selection_list.cpp pico_input_value.cpp settings.cpp)
UI_HEADERS=(pico_userinterface.h pico_frontpanel.h settings.h midi_reface.h)

die() { echo "ERROR: $*" >&2; exit 1; }
log() { echo "==> $*"; }

# Copy a single file, aborting if the source does not exist.
copy_file() {
  local src="$1"
  local dest="$2"
  [ -f "$src" ] || die "missing source file: $src"
  cp "$src" "$dest"
}

# 1. Verify that all six source repositories are present.
for inst in "${INSTRUMENTS[@]}"; do
  [ -d "$SRC_ROOT/$inst" ] || die "source repository not found: $SRC_ROOT/$inst"
done

# 2. Core sources (shared by all instruments).
log 'core sources'
mkdir -p "$DEST/core/src"
for f in "${CORE_SRC[@]}"; do
  copy_file "$SRC_ROOT/$CORE_DONOR/src/$f" "$DEST/core/src/$f"
done

# 3. Core headers (shared by all instruments).
log 'core headers'
mkdir -p "$DEST/core/include"
for f in "${SHARED_HEADERS[@]}"; do
  copy_file "$SRC_ROOT/$CORE_DONOR/include/$f" "$DEST/core/include/$f"
done

# 4. Optional core modules: frontpanel UI and reface MIDI mapping.
log 'optional core modules'
mkdir -p "$DEST/core/src/ui" "$DEST/core/src/midi"
for f in "${UI_SRC[@]}"; do
  copy_file "$SRC_ROOT/$UI_DONOR/src/$f" "$DEST/core/src/ui/$f"
done
copy_file "$SRC_ROOT/$UI_DONOR/src/midi_reface.cpp" "$DEST/core/src/midi/midi_reface.cpp"
for f in "${UI_HEADERS[@]}"; do
  copy_file "$SRC_ROOT/$UI_DONOR/include/$f" "$DEST/core/include/$f"
done

# 5. Shared libraries.
log 'shared libraries'
mkdir -p "$DEST/lib"
for d in audio encoder u8g2; do
  if command -v rsync >/dev/null 2>&1; then
    # u8g2/u8g2 becomes a git submodule and is therefore excluded.
    rsync -a --exclude 'u8g2/u8g2' "$SRC_ROOT/$CORE_DONOR/lib/$d" "$DEST/lib/"
  else
    cp -R "$SRC_ROOT/$CORE_DONOR/lib/$d" "$DEST/lib/"
    # u8g2/u8g2 becomes a git submodule and is therefore removed.
    rm -rf "$DEST/lib/u8g2/u8g2"
  fi
done

# 6. CMake import helpers.
log 'cmake imports'
mkdir -p "$DEST/cmake"
copy_file "$SRC_ROOT/$CORE_DONOR/pico_sdk_import.cmake" "$DEST/cmake/pico_sdk_import.cmake"
copy_file "$SRC_ROOT/$CORE_DONOR/pico_extras_import.cmake" "$DEST/cmake/pico_extras_import.cmake"

# build/, .git/, lib/pico-sdk and lib/pico-extras are deliberately never copied.

# --- migrate instruments -----------------------------------------------------
log 'instruments'

for inst in "${INSTRUMENTS[@]}"; do
  log "  $inst"

  # Create target directory structure
  mkdir -p "$DEST/instruments/$inst/src" "$DEST/instruments/$inst/include"

  # Copy all sources (recursively, including engine subdirectories
  # like moog/ juno/ solina/ rd_engine/)
  if [ -d "$SRC_ROOT/$inst/src" ]; then
    cp -R "$SRC_ROOT/$inst/src/." "$DEST/instruments/$inst/src/"
  fi

  # Copy all headers (recursively, including yc_engine/ juno/ moog/ etc.)
  if [ -d "$SRC_ROOT/$inst/include" ]; then
    cp -R "$SRC_ROOT/$inst/include/." "$DEST/instruments/$inst/include/"
  fi

  # Copy effects directory if the instrument has one
  if [ -d "$SRC_ROOT/$inst/effects" ]; then
    mkdir -p "$DEST/instruments/$inst/effects"
    cp -R "$SRC_ROOT/$inst/effects/." "$DEST/instruments/$inst/effects/"
  fi

  # Deduplicate top-level sources against core.
  # Only files directly in src/ are checked; subdirectories are
  # instrument-specific engines and stay untouched.
  for f in "$DEST/instruments/$inst/src"/*; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    for cand in "$DEST/core/src/$name" \
                "$DEST/core/src/ui/$name" \
                "$DEST/core/src/midi/$name"; do
      if [ -f "$cand" ]; then
        if cmp -s "$f" "$cand"; then
          rm -f "$f"
          echo "    identical to core, removed: $inst/src/$name"
        else
          echo "    DIVERGENT, kept locally: $inst/src/$name"
        fi
        break
      fi
    done
  done

  # Deduplicate top-level headers against core/include
  for f in "$DEST/instruments/$inst/include"/*; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    cand="$DEST/core/include/$name"
    if [ -f "$cand" ]; then
      if cmp -s "$f" "$cand"; then
        rm -f "$f"
        echo "    identical to core, removed: $inst/include/$name"
      else
        echo "    DIVERGENT, kept locally: $inst/include/$name"
      fi
    fi
  done

  # Per-instrument usb_descriptors.c is obsolete (core version is parameterised
  # via PICOFACE_INSTRUMENT_NAME / PICOFACE_USB_PID); remove unconditionally.
  if [[ -f "$DEST/instruments/$inst/src/usb_descriptors.c" ]]; then
    rm "$DEST/instruments/$inst/src/usb_descriptors.c"
    echo "    superseded by parameterised core/src/usb_descriptors.c, removed: $inst/src/usb_descriptors.c"
  fi

  # Remove old entry points (now provided by core/src/picoface_main.cpp)
  for f in main.cpp rd_main.cpp j6_main.cpp md_main.cpp sm_main.cpp; do
    if [ -f "$DEST/instruments/$inst/src/$f" ]; then
      rm -f "$DEST/instruments/$inst/src/$f"
      echo "    old entry point removed (replaced by core/src/picoface_main.cpp): $f"
    fi
  done
done

# Count .DS_Store files first, then remove them safely across macOS and Linux
ds_count=$(find "$DEST/core" "$DEST/instruments" "$DEST/lib" -type f -name '.DS_Store' 2>/dev/null | wc -l | tr -d ' ')
find "$DEST/core" "$DEST/instruments" "$DEST/lib" -type f -name '.DS_Store' -delete 2>/dev/null || true
log "Removed $ds_count .DS_Store file(s)."

log "migrated ${#INSTRUMENTS[@]} instruments"

cat <<'EOF'

  NEXT STEP: for each migrated instrument, write an adapter file

      instruments/<inst>/src/<XX>_Instrument.cpp

  which implements the picoface::Instrument interface and registers
  itself by calling PICOFACE_REGISTER_INSTRUMENT(...).
  Without this adapter the instrument will not be built anymore.
EOF

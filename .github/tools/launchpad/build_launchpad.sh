#!/usr/bin/env bash
# Build every top-level ESP-IDF example in this repo for one target and emit flashable,
# Launchpad-ready merged binaries directly in the published site layout:
#
#   $SITE_DIR/<app>/<target>/<app>-merged.bin   flashable image (bootloader + parttable + app)
#   $SITE_DIR/<app>/LAUNCHPAD.md                per-firmware readme (copied from the example)
#   $SITE_DIR/_meta/<app>.factory_offset        fctry partition offset (feeds the site README)
#   $SITE_DIR/_meta/idf.txt                     resolved IDF version + commit
#
# _meta/ is consumed and deleted by generate_launchpad_config.sh, so it never ships.
#
# Inputs (env):
#   IDF_TARGET        required, e.g. esp32c3
#   SITE_DIR          required, output root (per-target; artifacts are merged later)
#   EXAMPLES_DIR      default "examples"
#   LAUNCHPAD_EXCLUDE optional, space-separated example names or paths to skip
#   BUILD_TMP         default "$PWD/launchpad_build_tmp"
#
# Must run with the IDF environment exported and idf-build-apps installed.

set -euo pipefail

: "${IDF_TARGET:?IDF_TARGET is not set}"
: "${SITE_DIR:?SITE_DIR is not set}"
EXAMPLES_DIR="${EXAMPLES_DIR:-examples}"
LAUNCHPAD_EXCLUDE="${LAUNCHPAD_EXCLUDE:-}"
BUILD_TMP="${BUILD_TMP:-$PWD/launchpad_build_tmp}"
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

mkdir -p "$SITE_DIR/_meta"

# Record the resolved IDF version + commit so the site README can link the exact tree.
{
  idf.py --version 2>/dev/null || echo "unknown"
  git -C "$IDF_PATH" rev-parse HEAD 2>/dev/null || echo ""
} > "$SITE_DIR/_meta/idf.txt"

# --- discover examples -------------------------------------------------------
# Target-independent: a TOP-LEVEL $EXAMPLES_DIR entry with a `main/` subdir is an
# example, i.e. exactly $EXAMPLES_DIR/<app>/main and nothing deeper. Launchpad ships
# the flagship per-device firmwares only, so grouped subtrees (examples/advanced/) are
# deliberately NOT published, and examples/common/ has no `main/` to begin with.
#
# The depth bound doubles as the dirt filter: a CI checkout is clean, but a local run
# in a dirty workspace otherwise discovers hundreds of `main/` dirs under build/ and
# managed_components/ and tries to build them as examples. Those all sit deeper than
# $EXAMPLES_DIR/<app>/main, so -maxdepth 2 excludes them without a prune list.
ALL_DIRS=()
while IFS= read -r maindir; do
  ALL_DIRS+=("${maindir%/main}")
done < <(find "$EXAMPLES_DIR" -mindepth 2 -maxdepth 2 -type d -name main -print | sort)

# Nested examples are skipped silently by the bound above, which is indistinguishable
# from "forgot to add it". Name them so the job log says why they are absent.
while IFS= read -r maindir; do
  [ -n "$maindir" ] && echo "Not publishing to Launchpad (nested example): ${maindir%/main}"
done < <(find "$EXAMPLES_DIR" -mindepth 3 -maxdepth 3 -type d -name main -print | sort)

# An exclude entry matches the example's basename OR its path, with or without
# the examples/ prefix, so both "light" and "examples/light" work.
BUILD_DIRS=()
for d in "${ALL_DIRS[@]}"; do
  name=$(basename "$d")
  rel="${d#./}"
  skip=0
  for ex in $LAUNCHPAD_EXCLUDE; do
    ex="${ex%/}"
    if [ "$ex" = "$name" ] || [ "$ex" = "$rel" ] || [ "$ex" = "$EXAMPLES_DIR/$name" ]; then
      skip=1
      break
    fi
  done
  if [ "$skip" = "1" ]; then
    echo "Excluding example (LAUNCHPAD_EXCLUDE): $d"
  else
    BUILD_DIRS+=("$d")
  fi
done

if [ ${#BUILD_DIRS[@]} -eq 0 ]; then
  echo "No ESP-IDF examples to build for launchpad (all excluded?)." >&2
  exit 1
fi
echo "Building launchpad examples for $IDF_TARGET: ${BUILD_DIRS[*]}"

# --- build -------------------------------------------------------------------
# Launchpad ships one image per example: build ONLY the default config, never the
# sdkconfig.ci.* variants. Build dir name is therefore always "<app>_default".
idf-build-apps build --config-rules "sdkconfig.defaults=default" \
  --target "$IDF_TARGET" \
  --path "${BUILD_DIRS[@]}" \
  --build-dir "${BUILD_TMP}/@n_@w"

ccache --show-stats 2>/dev/null || true

# --- merge + stage -----------------------------------------------------------
# esptool 5.x renamed subcommands to the dashed form; the underscore alias still
# works but warns. Probe once instead of guessing per IDF version.
MERGE_CMD="merge-bin"
python -m esptool merge-bin --help >/dev/null 2>&1 || MERGE_CMD="merge_bin"

# fctry offset: Launchpad images ship WITHOUT a factory partition, the flashing user
# supplies their own at that offset. Every example carries its own partitions.csv, so
# the offset is example-specific (they happen to agree today, nothing enforces it).
# Read it from the *built* partition table (the source of truth after config selection),
# falling back to the example's partitions.csv when the decoder is unavailable.
factory_offset_of() {
  local build_dir="$1" project_path="$2" off=""
  local ptbin="$build_dir/partition_table/partition-table.bin"
  local gen="$IDF_PATH/components/partition_table/gen_esp32part.py"
  if [ -f "$ptbin" ] && [ -f "$gen" ]; then
    # gen_esp32part.py decodes the binary to CSV: Name,Type,SubType,Offset,Size,Flags
    off=$(python "$gen" "$ptbin" 2>/dev/null \
      | awk -F',' '{gsub(/[[:space:]]/,"",$1)} $1=="fctry"{gsub(/[[:space:]]/,"",$4); print $4; exit}') || true
  fi
  if [ -z "$off" ] && [ -f "$project_path/partitions.csv" ]; then
    off=$(awk -F',' '{gsub(/[[:space:]]/,"",$1)} $1=="fctry"{gsub(/[[:space:]]/,"",$4); print $4; exit}' \
      "$project_path/partitions.csv") || true
  fi
  printf '%s' "$off"
}

staged=0
for build_dir in "$BUILD_TMP"/*; do
  [ -d "$build_dir" ] || continue
  # No flash_args => not a completed flashable build, skip it.
  [ -f "$build_dir/flash_args" ] || continue

  build_name=$(basename "$build_dir")   # "<app>_default"
  app="${build_name%_default}"
  desc="$build_dir/project_description.json"
  project_path=""
  if [ -f "$desc" ]; then
    project_path=$(python -c "import json;print(json.load(open('$desc')).get('project_path',''))" 2>/dev/null) || true
  fi

  out_dir="$SITE_DIR/$app/$IDF_TARGET"
  mkdir -p "$out_dir"
  # flash_args holds paths relative to the build dir, so merge from inside it.
  ( cd "$build_dir" && python -m esptool --chip "$IDF_TARGET" "$MERGE_CMD" \
      -o "$out_dir/${app}-merged.bin" "@flash_args" )
  staged=$((staged + 1))
  echo "merged: $app/$IDF_TARGET/${app}-merged.bin ($(du -h "$out_dir/${app}-merged.bin" | cut -f1))"

  # Per-app files: identical across targets, so first target to run wins. Artifact
  # merging in the deploy job overwrites them with identical content.
  off=$(factory_offset_of "$build_dir" "$project_path")
  [ -z "$off" ] || printf '%s\n' "$off" > "$SITE_DIR/_meta/${app}.factory_offset"

  # LAUNCHPAD.md is the self-contained per-firmware page shown via the app's
  # readme.text. The example's base README.md carries internal SDK links that break
  # in the Launchpad UI, so it is deliberately not used.
  #
  # The fallback (only reached when build metadata is unreadable) can assume
  # $EXAMPLES_DIR/<app>: discovery is bounded to that depth, so $app — the project
  # name — is also the top-level directory name.
  if [ -n "$project_path" ] && [ -f "$project_path/LAUNCHPAD.md" ]; then
    cp "$project_path/LAUNCHPAD.md" "$SITE_DIR/$app/LAUNCHPAD.md"
  elif [ -f "$REPO_ROOT/$EXAMPLES_DIR/$app/LAUNCHPAD.md" ]; then
    cp "$REPO_ROOT/$EXAMPLES_DIR/$app/LAUNCHPAD.md" "$SITE_DIR/$app/LAUNCHPAD.md"
  else
    echo "WARN: no LAUNCHPAD.md for $app; readme.text will be omitted" >&2
  fi
done

if [ "$staged" = "0" ]; then
  echo "ERROR: no flashable builds produced for $IDF_TARGET." >&2
  exit 1
fi
echo "Staged $staged merged binaries for $IDF_TARGET."

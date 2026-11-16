#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#
# Sync this component's copy of the Matter mapping from a checkout of the canonical repo
# (esp-rainmaker-neo-matter-mapping) and stamp mapping/SOURCE with the origin and a hash of every
# copied file. Proto-style: the JSON is the ".proto" and the shared validator comes with it; this
# component owns only its profiles and the C emitters in gen_mapping_table.py. Usage:
#   scripts/sync_mapping.sh /path/to/esp-rainmaker-neo-matter-mapping
#   scripts/sync_mapping.sh --check      # verify the copies still match mapping/SOURCE
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
source_file="$here/mapping/SOURCE"

# lib/vectors are inputs; scripts/mapping/ is the shared validator. Each entry is the path in
# this component, then the path in the mapping checkout.
copied=(
    "mapping/lib/rmng_matter_mapping.json:lib/rmng_matter_mapping.json"
    "mapping/vectors/rmng_matter_mapping.vectors.json:vectors/rmng_matter_mapping.vectors.json"
    "scripts/mapping/__init__.py:mapping/__init__.py"
    "scripts/mapping/vocabulary.py:mapping/vocabulary.py"
    "scripts/mapping/validate.py:mapping/validate.py"
)

hash_of() {
    python3 -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$1"
}

if [[ "${1:-}" == "--check" ]]; then
    status=0
    for pair in "${copied[@]}"; do
        dst=${pair%%:*}
        want=$(sed -n "s|^# sha256 $dst  *||p" "$source_file")
        if [[ -z "$want" ]]; then
            echo "DRIFT: $dst has no sha256 in mapping/SOURCE" >&2
            status=1
        elif [[ "$want" != "$(hash_of "$here/$dst")" ]]; then
            echo "DRIFT: $dst differs from the copy mapping/SOURCE records" >&2
            status=1
        fi
    done
    (( status == 0 )) && echo "mapping copies match mapping/SOURCE"
    exit $status
fi

src=${1:?usage: sync_mapping.sh <path to esp-rainmaker-neo-matter-mapping checkout> | --check}
for pair in "${copied[@]}"; do
    dst=${pair%%:*}
    mkdir -p "$(dirname "$here/$dst")"
    cp "$src/${pair#*:}" "$here/$dst"
done

commit=$(git -C "$src" rev-parse --short HEAD 2>/dev/null || echo "(not a git checkout)")
version=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['mapping_version'])" \
    "$here/mapping/lib/rmng_matter_mapping.json")
{
    cat <<SRC
# Provenance of the mapping copy in this component (proto-style: the canonical source is the
# esp-rainmaker-neo-matter-mapping repo). Update via scripts/sync_mapping.sh; --check verifies the
# copies still match the hashes below. The profiles and the C emitters are this component's own.
source_repo: esp-rainmaker-neo-matter-mapping
source_commit: $commit
mapping_version: $version
synced: $(date -u +%Y-%m-%d)
SRC
    for pair in "${copied[@]}"; do
        dst=${pair%%:*}
        echo "# sha256 $dst $(hash_of "$here/$dst")"
    done
} > "$source_file"

echo "synced from $src @ $commit (mapping_version $version); review the diff and commit"

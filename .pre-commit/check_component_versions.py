#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
"""Check that every first-party idf_component.yml agrees with esp_rmaker_neo/versioning.cmake.

The component manager needs a literal version in each manifest -- it does not expand
variables -- so the SDK version is necessarily written in more than one place. This keeps
the copies honest: esp_rmaker_neo/versioning.cmake is the single source of truth, and a bump that
misses a manifest fails here instead of shipping a set of manifests that disagree.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VERSIONING_CMAKE = REPO_ROOT / "components" / "esp_rmaker_neo" / "versioning.cmake"


def source_of_truth() -> str:
    text = VERSIONING_CMAKE.read_text()
    parts = []
    for field in ("MAJOR", "MINOR", "PATCH"):
        m = re.search(
            rf"^\s*set\(RMNG_VERSION_{field}\s+(\d+)\s*\)", text, re.MULTILINE
        )
        if not m:
            sys.exit(f"{VERSIONING_CMAKE}: could not find RMNG_VERSION_{field}")
        parts.append(m.group(1))
    return ".".join(parts)


def manifests() -> list[Path]:
    # Component manifests only; the nested pattern is for a component inside another one
    # (none today). Do NOT widen to **/idf_component.yml -- that also matches the 12 */main/
    # application manifests, which are apps rather than distributable components and
    # legitimately carry no version.
    found = []
    for pattern in (
        "components/*/idf_component.yml",
        "components/*/*/idf_component.yml",
        "examples/common/*/idf_component.yml",
    ):
        found += sorted(REPO_ROOT.glob(pattern))
    return found


def main() -> int:
    expected = source_of_truth()
    problems = []
    for manifest in manifests():
        # Only the top-level `version:` key, i.e. before the dependencies block, where
        # each dependency may legitimately carry its own version spec.
        head = manifest.read_text().split("dependencies:", 1)[0]
        m = re.search(r'^version:\s*"?([^"\s]+)"?', head, re.MULTILINE)
        rel = manifest.relative_to(REPO_ROOT)
        if not m:
            problems.append(f"  {rel}: no top-level 'version:' key")
        elif m.group(1) != expected:
            problems.append(f"  {rel}: version {m.group(1)}, expected {expected}")

    if problems:
        print(
            f"Component manifest versions disagree with {VERSIONING_CMAKE.name} ({expected}):"
        )
        print("\n".join(problems))
        print(
            "\nUpdate the manifests, or esp_rmaker_neo/versioning.cmake if the SDK version itself changed."
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

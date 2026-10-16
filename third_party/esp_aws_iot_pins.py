#!/usr/bin/env python
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
"""Checks or updates the esp-aws-iot archive pins in third_party_versions.cmake."""

import argparse
import hashlib
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

VERSIONS_FILE = Path(__file__).resolve().parent / "third_party_versions.cmake"
REPOSITORY = "espressif/esp-aws-iot"
PREFIX = "RMNG_THIRD_PARTY_ESP_AWS_IOT"


def read_vars(text: str) -> dict[str, str]:
    return {
        m[1]: m[2].strip().strip('"')
        for m in re.finditer(rf"set\(\s*({PREFIX}\S*)\s+(.*?)\s*\)", text, re.DOTALL)
    }


def set_var(text: str, name: str, value: str) -> str:
    pattern = rf'(set\(\s*{re.escape(name)}\s+")[^"]*(")'
    if not re.search(pattern, text):
        sys.exit(f"error: {name} is not set in {VERSIONS_FILE.name}")
    return re.sub(pattern, lambda m: m[1] + value + m[2], text)


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(repo), *args], check=True, capture_output=True, text=True
    ).stdout


def archive_url(github_repo: str, commit: str) -> str:
    return f"https://github.com/{github_repo}/archive/{commit}.tar.gz"


def archive_sha256(url: str) -> str:
    digest = hashlib.sha256()
    with urllib.request.urlopen(url) as response:
        while chunk := response.read(1 << 16):
            digest.update(chunk)
    return digest.hexdigest()


def expected_pins(ref: str, libraries: list[str]) -> dict[str, str]:
    """Returns the pin values for ref, read from a depth-1 fetch of esp-aws-iot."""
    with tempfile.TemporaryDirectory() as tmp:
        repo = Path(tmp)
        git(repo, "init", "-q")
        git(
            repo, "fetch", "-q", "--depth", "1", f"https://github.com/{REPOSITORY}", ref
        )
        commit = git(repo, "rev-parse", "FETCH_HEAD").strip()
        gitmodules = git(
            repo,
            "config",
            "--blob",
            "FETCH_HEAD:.gitmodules",
            "--get-regexp",
            r"^submodule\..*\.url$",
        )
        urls = dict(line.split(" ", 1) for line in gitmodules.splitlines())
        pins = {
            f"{PREFIX}_COMMIT": commit,
            f"{PREFIX}_URL": archive_url(REPOSITORY, commit),
        }
        for lib in libraries:
            path = f"libraries/{lib}/{lib}"
            entry = git(repo, "ls-tree", "FETCH_HEAD", path).split()
            if len(entry) < 3 or entry[1] != "commit":
                sys.exit(f"error: {path} is not a submodule at esp-aws-iot {commit}")
            github_url = urls[f"submodule.{path}.url"]
            github_repo = re.sub(r"^https://github\.com/|\.git$", "", github_url)
            pins[f"{PREFIX}_{lib}_URL"] = archive_url(github_repo, entry[2])
    for name in [n for n in pins if n.endswith("_URL")]:
        print(f"hashing {pins[name]}", file=sys.stderr)
        pins[name.removesuffix("_URL") + "_SHA256"] = archive_sha256(pins[name])
    return pins


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--check",
        action="store_true",
        help="compare the pins with the pinned esp-aws-iot commit",
    )
    mode.add_argument(
        "--update",
        metavar="REF",
        help="rewrite the pins for an esp-aws-iot commit, tag, or branch",
    )
    args = parser.parse_args()

    text = VERSIONS_FILE.read_text()
    current = read_vars(text)
    libraries = current[f"{PREFIX}_LIBRARIES"].split()
    pins = expected_pins(args.update or current[f"{PREFIX}_COMMIT"], libraries)

    if args.update:
        for name, value in pins.items():
            text = set_var(text, name, value)
        VERSIONS_FILE.write_text(text)
        print(f"updated {VERSIONS_FILE.name} to esp-aws-iot {pins[f'{PREFIX}_COMMIT']}")
        return 0

    mismatches = [name for name, value in pins.items() if current.get(name) != value]
    for name in mismatches:
        print(f"{name}:\n  pinned:   {current.get(name)}\n  expected: {pins[name]}")
    print(f"{len(mismatches)} mismatch(es) in {len(pins)} pins")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())

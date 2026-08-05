#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Check whether every requirement in a requirements file is already satisfied.

Usage: check_requirements.py <requirements_file>

Exit code 0 means the running interpreter satisfies the whole file and the caller
can skip installing; any non-zero exit means the caller should install.

`pip install --dry-run` cannot be used on its own: it exits 0 whether or not
packages are missing. This asks pip what it *would* install (`--report`) and
treats a non-empty install list as "unsatisfied". Delegating to pip's resolver
keeps `-r` includes, version specifiers and environment markers correct.

Any failure to reach a verdict -- a pip too old for `--report` (needs >= 22.2),
unparsable output, no network -- also exits non-zero, so an inconclusive check
falls through to the install rather than silently skipping it.
"""

import json
import subprocess
import sys

SATISFIED = 0
NEEDS_INSTALL = 1


def pending_installs(requirements_path) -> list[str]:
    """Return the names pip would install to satisfy the requirements file."""
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--dry-run",
            "--quiet",
            "--report",
            "-",
            "-r",
            requirements_path,
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            result.stderr.strip() or "pip failed to resolve the requirements"
        )

    report = json.loads(result.stdout)
    return [entry["metadata"]["name"] for entry in report["install"]]


def main():
    if len(sys.argv) != 2:
        print("Usage: check_requirements.py <requirements_file>", file=sys.stderr)
        sys.exit(NEEDS_INSTALL)

    requirements_path = sys.argv[1]

    try:
        pending = pending_installs(requirements_path)
    except (OSError, RuntimeError, ValueError, KeyError) as exc:
        print(f"Could not check {requirements_path}: {exc}", file=sys.stderr)
        sys.exit(NEEDS_INSTALL)

    if pending:
        print(f"Missing or outdated: {', '.join(sorted(pending))}", file=sys.stderr)
        sys.exit(NEEDS_INSTALL)

    sys.exit(SATISFIED)


if __name__ == "__main__":
    main()

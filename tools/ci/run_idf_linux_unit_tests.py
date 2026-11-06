#!/usr/bin/env python
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
"""Run test/apps/esp-idf built for the ESP-IDF linux target, one Unity suite at a time.

The app's unity_run_menu() reads test selections from stdin and never returns, so each suite tag is written to stdin
and its "N Tests M Failures" summary awaited before the next one. Fails if a suite fails, prints no summary in time,
or the app exits early.

Usage: run_idf_linux_unit_tests.py <app.elf> <tag> [<tag> ...]   e.g. "[nvs-common]"
"""

import re
import subprocess
import sys
import threading
import time

SUITE_TIMEOUT_S = 300
SUMMARY = re.compile(r"^(\d+) Tests (\d+) Failures (\d+) Ignored")


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    elf, tags = sys.argv[1], sys.argv[2:]
    app = subprocess.Popen(
        [elf],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    summaries = []
    cond = threading.Condition()

    def pump():
        for line in app.stdout:
            sys.stdout.write(line)
            m = SUMMARY.match(line.strip())
            if m:
                with cond:
                    summaries.append(tuple(int(x) for x in m.groups()))
                    cond.notify_all()
        with cond:
            cond.notify_all()

    threading.Thread(target=pump, daemon=True).start()
    failed = False
    try:
        for tag in tags:
            # Unity prints the summary twice per selection (suite, then run total), so wait for the first new one.
            with cond:
                seen = len(summaries)
            app.stdin.write(tag + "\n")
            app.stdin.flush()
            deadline = time.monotonic() + SUITE_TIMEOUT_S
            with cond:
                while (
                    len(summaries) <= seen
                    and app.poll() is None
                    and time.monotonic() < deadline
                ):
                    cond.wait(timeout=1)
                if len(summaries) <= seen:
                    print(
                        f"\n{tag}: no result ({'app exited' if app.poll() is not None else 'timed out'})"
                    )
                    return 1
                tests, failures, _ = summaries[seen]
            print(f"\n{tag}: {tests} tests, {failures} failures")
            failed |= failures > 0
            # Let the second summary line for this selection arrive before the next tag.
            time.sleep(1)
    finally:
        app.kill()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

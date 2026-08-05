# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Global limiter for heavy compile / link steps (CMake, Ninja, idf.py build).

Used by the test/ pytest suite so many parallel tests do not run unbounded
concurrent toolchains (memory pressure / OOM). Configure from pytest or call
``configure_max_concurrent_build_jobs`` before builds run (e.g. manager server init).
"""

from __future__ import annotations

import threading
from contextlib import contextmanager
from typing import Optional

_lock = threading.Lock()
_sem: Optional[threading.Semaphore] = None


def configure_max_concurrent_build_jobs(n: int) -> None:
    """
    Set the maximum number of concurrent build jobs (CMake configure, Ninja, idf.py build).

    ``n <= 0`` disables limiting. Safe to call again to replace the limit (e.g. in tests).
    """
    global _sem
    with _lock:
        if n <= 0:
            _sem = None
            return
        _sem = threading.Semaphore(n)


@contextmanager
def build_job_slot():
    """
    Hold one build slot for the duration of a compile / link phase.

    No-op when limiting is disabled.
    """
    with _lock:
        sem = _sem
    if sem is None:
        yield
        return
    sem.acquire()
    try:
        yield
    finally:
        sem.release()

# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import uuid
import socket
from threading import Lock
from .base import (
    FirmwareInstanceFactory,
    FirmwareInstanceFactoryOta,
    FirmwareInstanceFactoryHostCtrl,
    FirmwareInstance,
    FirmwareInstanceHostCtrl,
    FirmwareInstanceOta,
    LoggingStream,
)
import os
import sys
import signal
import time
from host_ctrl_python.commands import PortManagerSingle
from host_ctrl_python.globals import Globals as HostCtrlGlobals
from host_ctrl_python.host_ctrl import NodeHostCtrl

from util.factory_config import FactoryConfigFactory, FactoryConfig
from util.build_job_slot import build_job_slot
from util.build_opts import force_ipv4_enabled
from util.proc_manager import ProcManager
from util.kconfig import gen_sdkconfig_defaults
from rmng_backend import User
import shutil
from pathlib import Path
from ..filepaths import (
    RMNG_SDK_DEVICE_SIM_POSIX_DIR,
    RMNG_SDK_OTA_SIM_POSIX_DIR,
    RMNG_SDK_OSAL_OTA_MODULE_DIR,
    RMNG_SDK_DIR,
)
import re
import subprocess
import traceback
from typing import Optional

# Subdirectory under each run-* folder where GCOV_PREFIX redirects .gcda output.
_GCOV_RUN_SUBDIR = "gcda"
# Under build_dir: each stop() copies run-*/gcda/* here as gcda-shards/<run-folder-name>/...
# so coverage survives destroy()'s removal of run-*.
_GCOV_SHARDS_DIR = "gcda-shards"
# Temp per-run JSON fragments under the CMake build directory (removed after merging).
_GCOV_RUN_JSON_PARTS_DIR = ".posix_cov_json_parts"
# One merged tracefile kept at the build root (all runs for this build tree).
_POSIX_BUILD_COVERAGE_JSON = "posix-build-coverage.json"

# After the simulator process exits, gcov may still flush .gcda asynchronously; wait before
# archiving so shards are not empty or partial.
_GCOV_POST_EXIT_GRACE_S = 0.2
_GCOV_FLUSH_POLL_INTERVAL_S = 0.15
_GCOV_FLUSH_TIMEOUT_S = 10.0
_GCOV_FLUSH_STABLE_ROUNDS = 4  # consecutive polls with same .gcda count
_GCOV_FLUSH_WAIT_FIRST_GCDA_S = 3.0  # max wait for the first .gcda to appear after exit

# Mirrors components/../cmake/testing.cmake (testing_add_gcovr_coverage_target);
# per-factory --filter via _get_coverage_gcovr_filters() matches CMake FILTERS when set.
#
# gcovr: if no positional search_paths are given, it scans both -r and --object-directory,
# so the repo root is walked and stale .gcda from other presets break gcov (B42 vs B52).
# CMake and shard capture pass the build dir as the sole positional search path.
_DEFAULT_GCOVR_FILE_EXCLUDES = [
    "^.*/test-.*(/|$)",
    "^.*/tests_src/.*",
    "^.*/test_.*\\.c$",
    "^.*/host_ctrl/.*",
]
_DEFAULT_GCOVR_DIR_EXCLUDES = [
    # Do not use ^.*/build: it matches test/build/... and hides the whole POSIX object tree.
    r"^(components|examples)/.+/build($|/)",
    "^posix/lib($|/)",
    # Vendor CMake deps: .gcda vs .gcno stamp skew is common; --exclude does not skip gcov.
    r"^.*/deps$",
    # Gcovr 8.x applies these via RelativeFilter against -r (repo root).
    # Shard / run-* .gcda are not beside .gcno; only our copied tree under CMakeFiles/
    # is valid for gcov. Anchored ^gcda-shards$ / ^run-[^/]+$ miss paths like
    # test/build/<preset>/gcda-shards/... (see debug: stamp mismatch / cannot open gcno).
    rf"^.*/{_GCOV_SHARDS_DIR}(/|$)",
    r"^.*/run-[^/]+(/|$)",
]

# Same .c file built with different CMake/sdkconfig presets can yield different line
# numbers for the same symbol; gcovr's default strict merge raises GcovrMergeAssertionError.
_GCOVR_MERGE_FUNCTIONS_ARGS = ["--merge-mode-functions=merge-use-line-min"]

# Shard .gcda can predate a later ninja rebuild (.gcno newer → gcov "stamp mismatch").
# _copy_gcda_into_build skips those pairs; this keeps gcovr from aborting the whole capture.
_GCOVR_IGNORE_FAILED_GCOV_ARGS = ["--gcov-ignore-errors=no_working_dir_found"]


# ---------------------------------------------------------------------------
# Low-level helpers
# ---------------------------------------------------------------------------


def _gcov_prefix_strip_count(build_dir: Path) -> int:
    """Number of leading path components to strip from the baked-in absolute .gcda path."""
    p = build_dir.resolve()
    if not p.is_absolute() or len(p.parts) <= 1:
        return 0
    return len(p.parts) - 1


def _find_gcovr() -> Optional[str]:
    return shutil.which("gcovr")


def _gcovr_verbose_cli_flags() -> list[str]:
    """
    Pass ``-v`` when env ``RMNG_POSIX_GCOVR_VERBOSE`` is 1/true/yes/on so gcovr logs
    DEBUG lines (filter decisions, paths). Same as running ``gcovr --verbose`` by hand.
    """
    v = os.environ.get("RMNG_POSIX_GCOVR_VERBOSE", "").strip().lower()
    if v in ("1", "true", "yes", "on"):
        return ["-v"]
    return []


def _cmake_dir_version_key(name: str) -> tuple[int, ...]:
    """
    Sort key for a ``CMakeFiles/<cmake-version>`` directory name.

    Numeric components in order, so 3.28.3 > 3.9.0. Names that are not a version
    (``CMakeFiles/CMakeTmp`` and friends) yield an empty tuple and sort lowest.
    """
    return tuple(int(part) for part in re.findall(r"\d+", name))


def _read_cmake_c_compiler_info(build_dir: Path) -> tuple[Optional[str], Optional[str]]:
    """
    (CMAKE_C_COMPILER_ID, CMAKE_C_COMPILER_VERSION) as recorded by the build tree.

    Neither is written to CMakeCache.txt; CMake puts them in
    CMakeFiles/<cmake-version>/CMakeCCompiler.cmake. Missing values come back None.
    """
    cfiles = build_dir.resolve() / "CMakeFiles"
    if not cfiles.is_dir():
        return None, None
    cid: Optional[str] = None
    cver: Optional[str] = None
    # Several <cmake-version> dirs can coexist after a CMake upgrade; newest wins.
    # Sort on the parsed version, not the name: "3.9.0" sorts after "3.28.3" lexically.
    for info in sorted(
        cfiles.glob("*/CMakeCCompiler.cmake"),
        key=lambda p: _cmake_dir_version_key(p.parent.name),
        reverse=True,
    ):
        try:
            text = info.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        m = re.search(r'^set\(CMAKE_C_COMPILER_ID\s+"([^"]*)"\)', text, re.MULTILINE)
        if m:
            cid = m.group(1).strip() or None
        m = re.search(
            r'^set\(CMAKE_C_COMPILER_VERSION\s+"([^"]*)"\)', text, re.MULTILINE
        )
        if m:
            cver = m.group(1).strip() or None
        if cid:
            return cid, cver
    return cid, cver


def _gnu_gcov_executable(compiler_version: Optional[str]) -> str:
    """
    gcovr --gcov-executable value for GNU (mirrors testing.cmake).

    Prefer the version-suffixed gcov: distros where the default gcc is newer than
    the default gcov (e.g. Ubuntu 24.04 ships gcc 14 with gcov 13) otherwise fail
    every .gcno with a "version 'B42*', prefer 'B33*'" mismatch.
    """
    major = re.match(r"[0-9]+", compiler_version or "")
    if major:
        exe = shutil.which(f"gcov-{major.group(0)}")
        if exe:
            return exe
    return shutil.which("gcov") or "gcov"


def _llvm_cov_gcov_executable() -> str:
    """gcovr --gcov-executable value for Clang (mirrors testing.cmake)."""
    exe = shutil.which("llvm-cov")
    if not exe and sys.platform == "darwin":
        try:
            proc = subprocess.run(
                ["xcrun", "-f", "llvm-cov"],
                capture_output=True,
                text=True,
                check=True,
            )
            cand = proc.stdout.strip()
            if cand:
                exe = cand
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
    if not exe:
        exe = "llvm-cov"
    return f"{exe} gcov"


def _gcov_executable_for_build(build_dir: Path) -> str:
    """
    gcovr --gcov-executable: GNU builds need gcov; Clang needs 'llvm-cov gcov'.
    Matches components/../cmake/testing.cmake (testing_add_gcovr_coverage_target).
    """
    cid, cver = _read_cmake_c_compiler_info(build_dir)
    if cid == "GNU":
        return _gnu_gcov_executable(cver)
    if cid in ("Clang", "AppleClang", "IntelLLVM", "ARMClang"):
        return _llvm_cov_gcov_executable()
    # Unknown or missing compiler info: platform heuristic
    if sys.platform == "darwin":
        return _llvm_cov_gcov_executable()
    return _gnu_gcov_executable(cver)


def _gcovr_exclude_cli_args(extra_file_excludes: list[str]) -> list[str]:
    args: list[str] = []
    for pat in _DEFAULT_GCOVR_FILE_EXCLUDES:
        args.extend(["--exclude", pat])
    for pat in extra_file_excludes:
        args.extend(["--exclude", pat])
    for pat in _DEFAULT_GCOVR_DIR_EXCLUDES:
        args.extend(["--exclude-directories", pat])
    return args


def _gcovr_filter_cli_args(filter_patterns: list[str]) -> list[str]:
    """gcovr --filter args; paths are relative to -r (see _get_coverage_source_root)."""
    args: list[str] = []
    for pat in filter_patterns:
        args.extend(["--filter", pat])
    return args


def _rmtree_with_retries(path: Path, should_log: bool = True) -> None:
    last_err: Optional[OSError] = None
    for attempt in range(5):
        try:
            shutil.rmtree(path, ignore_errors=False)
            return
        except OSError as e:
            last_err = e
            if attempt < 4:
                time.sleep(0.15 * (attempt + 1))
    if should_log:
        print(f"Warning: could not remove {path} after retries ({last_err})")
    shutil.rmtree(path, ignore_errors=True)


# ---------------------------------------------------------------------------
# Coverage helpers — the core of the parallel-safe merge pipeline
# ---------------------------------------------------------------------------


def _delete_gcda_gcov_in_build(build_dir: Path) -> None:
    """Remove ALL .gcda and .gcov files directly in the build tree (not under run-* or gcda-shards)."""
    bd = build_dir.resolve()
    for pattern in ("*.gcda", "*.gcov"):
        for p in list(bd.rglob(pattern)):
            try:
                rel = p.relative_to(bd)
            except ValueError:
                continue
            if rel.parts[0].startswith("run-"):
                continue
            if rel.parts[0] == _GCOV_SHARDS_DIR:
                continue
            try:
                p.unlink()
            except OSError:
                pass


def _copy_gcda_into_build(gcda_root: Path, build_dir: Path) -> list[Path]:
    """
    Copy .gcda files from a run's gcda tree into the build tree, placing them
    next to the corresponding .gcno files so gcov/gcovr can pair them.

    Skips copying when the build tree's matching .gcno is newer than the shard .gcda
    (rebuild after the test run → stale profile; gcov would report stamp mismatch).

    Returns the list of destination paths (for cleanup after capture).
    """
    bd = build_dir.resolve()
    copied: list[Path] = []
    for src in gcda_root.rglob("*.gcda"):
        rel = src.relative_to(gcda_root)
        # Match gcovr --exclude-directories .../deps: never mirror vendor .gcda (stamp skew).
        if "deps" in rel.parts:
            continue
        dest = bd / rel
        gcno = dest.with_suffix(".gcno")
        if gcno.is_file():
            try:
                if gcno.stat().st_mtime_ns > src.stat().st_mtime_ns:
                    continue
            except OSError:
                pass
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        copied.append(dest)
    return copied


def _remove_files(paths: list[Path]) -> None:
    for p in paths:
        try:
            p.unlink()
        except OSError:
            pass


def _count_gcda_under(path: Path) -> int:
    if not path.is_dir():
        return 0
    return sum(1 for _ in path.rglob("*.gcda"))


def _wait_for_gcda_flush(gcda_dir: Path, should_log: bool) -> None:
    """
    Wait until .gcda under gcda_dir stop growing (flush settled) or timeout.

    Call after the instrumented process has exited so profiling data is not archived
    while the runtime is still writing files.
    """
    time.sleep(_GCOV_POST_EXIT_GRACE_S)
    start = time.monotonic()
    settle_deadline = start + _GCOV_FLUSH_TIMEOUT_S
    first_gcda_deadline = start + _GCOV_FLUSH_WAIT_FIRST_GCDA_S
    last_count: Optional[int] = None
    stable_rounds = 0
    while time.monotonic() < settle_deadline:
        count = _count_gcda_under(gcda_dir)
        now = time.monotonic()
        if count == 0 and now >= first_gcda_deadline:
            if should_log:
                print(
                    "POSIX coverage: no .gcda under "
                    f"{gcda_dir} after {_GCOV_FLUSH_WAIT_FIRST_GCDA_S}s "
                    "(skipping long flush wait)"
                )
            return
        if count > 0:
            if last_count is not None and count == last_count:
                stable_rounds += 1
                if stable_rounds >= _GCOV_FLUSH_STABLE_ROUNDS:
                    if should_log:
                        print(
                            f"POSIX coverage: gcda settled ({count} file(s) under {gcda_dir})"
                        )
                    return
            else:
                stable_rounds = 0
        else:
            stable_rounds = 0
        last_count = count
        time.sleep(_GCOV_FLUSH_POLL_INTERVAL_S)

    final = _count_gcda_under(gcda_dir)
    if should_log:
        print(
            f"Warning: POSIX coverage: gcda flush wait timed out after "
            f"{_GCOV_FLUSH_TIMEOUT_S}s ({final} file(s) under {gcda_dir})"
        )


def _sweep_orphan_posix_run_worktrees(build_dir: Path, should_log: bool = True) -> None:
    """Remove all build/run-*/ trees."""
    bd = build_dir.resolve()
    if not bd.is_dir():
        return
    for run_dir in sorted(bd.glob("run-*")):
        if run_dir.is_dir():
            if should_log:
                print(f"Removing POSIX sim run worktree: {run_dir}")
            _rmtree_with_retries(run_dir, should_log=should_log)


def _archive_run_gcda_shard(build_dir: Path, run_folder: Optional[Path]) -> None:
    """
    After the simulator exits, copy run-*/gcda into build_dir/gcda-shards/<run-folder-name>/
    so .gcda survives destroy()'s removal of the run worktree.
    """
    if run_folder is None or not run_folder.is_dir():
        return
    src = run_folder / _GCOV_RUN_SUBDIR
    if not src.is_dir():
        return
    if not any(src.rglob("*.gcda")):
        return
    bd = build_dir.resolve()
    dest_root = bd / _GCOV_SHARDS_DIR / run_folder.name
    if dest_root.exists():
        _rmtree_with_retries(dest_root, should_log=False)
    dest_root.mkdir(parents=True, exist_ok=True)
    for path in src.rglob("*.gcda"):
        rel = path.relative_to(src)
        dest = dest_root / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dest)


def _discover_gcda_shard_roots(build_dir: Path) -> list[Path]:
    """Per-instance gcda trees archived under build_dir/gcda-shards/*/."""
    bd = build_dir.resolve()
    roots: list[Path] = []
    shards = bd / _GCOV_SHARDS_DIR
    if not shards.is_dir():
        return roots
    for d in sorted(shards.iterdir()):
        if d.is_dir() and any(d.rglob("*.gcda")):
            roots.append(d)
    return roots


def _sweep_gcda_shards(build_dir: Path, should_log: bool = True) -> None:
    """Remove build_dir/gcda-shards after a merged coverage report."""
    sd = build_dir.resolve() / _GCOV_SHARDS_DIR
    if not sd.is_dir():
        return
    if should_log:
        print(f"Removing POSIX GCDA shards: {sd}")
    _rmtree_with_retries(sd, should_log=should_log)


def _cleanup_coverage_artifacts_in_build(build_dir: Path) -> None:
    """Remove .gcda and .gcov files under the build directory after a report."""
    bd = build_dir.resolve()
    if not bd.is_dir():
        return
    for pattern in ("*.gcda", "*.gcov"):
        for p in bd.rglob(pattern):
            try:
                p.unlink()
            except OSError:
                pass


class FirmwareInstanceFactoryPosix(FirmwareInstanceFactory):
    """
    A class that represents a factory for POSIX firmware instances.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        project_dir: Path,
        executable_name: str,
        should_log: bool = True,
    ):
        FirmwareInstanceFactory.__init__(
            self, factory_config_factory=factory_config_factory, should_log=should_log
        )
        self.project_dir = project_dir
        self.executable_name = executable_name

    def get_id(self) -> str:
        """
        Get the ID of the firmware instance.
        """
        return "firmware-posix"

    def _build_executable(
        self,
        build_dir: Path,
        cmake_args: list[str] = [],
        add_configs: dict[str, str] = {},
    ) -> Path:
        """
        Build the executable.
        """
        # When --force-ipv4 is passed to pytest, every posix build skips IPv6 at the
        # transport layer (avoids the ~60s connect stall on runners with black-holed
        # IPv6 egress). Lowest precedence so explicit add_configs can still override.
        force_ipv4_config = (
            {"OSAL_MQTT_CORE_FORCE_IPV4": "y"} if force_ipv4_enabled() else {}
        )
        add_defaults = gen_sdkconfig_defaults(
            {
                "RMNG_TESTING": "y",
                **force_ipv4_config,
                **self._get_add_configs(),
                **add_configs,
            }
        )
        with open(add_defaults, "r") as f:
            if self.should_log:
                print(f.read())
        commands = [
            # Make build directory
            ["mkdir", "-p", str(build_dir)],
            # Run CMake for ninja build
            [
                "cmake",
                "-B",
                str(build_dir),
                "-S",
                str(self.project_dir),
                "-GNinja",
                "-DCMAKE_BUILD_TYPE=Debug",
                f"-DSDKCONFIG_DEFAULTS={add_defaults}",
                *cmake_args,
            ],
            # Run ninja to build the executables
            ["ninja", "-C", str(build_dir)],
        ]
        with build_job_slot():
            for cmd in commands:
                if self.should_log:
                    print(f"Running command: {' '.join(cmd)}")
                ProcManager.run_cmd_sync(cmd)

        # Check that the executable was built
        executable_path = build_dir / self.executable_name
        if not executable_path.exists():
            raise RuntimeError(f"Executable path {executable_path} does not exist")

        # Unlink the temporary SDK config defaults file
        try:
            os.unlink(add_defaults)
        except FileNotFoundError:
            pass

        return executable_path

    def build(self, add_configs: dict[str, str | int] = {}):
        """
        Build the firmware instance.
        """
        self._remake_build_dir(self.build_dir)
        self._build_executable(self.build_dir, add_configs=add_configs)

    def build_version_binary_if_not_built(self, version_str: str) -> Path:
        """
        Build a binary with a specific firmware version if it is not built.
        Returns the path to the binary.
        """
        build_dir = self._get_build_dir(version_str)
        with self._get_build_dir_lock(build_dir):
            if self._is_built_locked(build_dir):
                return self.partition_helper.get_first_partition_path(build_dir)

            # Ensure fresh directory
            self._remake_build_dir(build_dir)

            # Build the executable
            cmake_args = [
                f"-DPROJECT_VER={version_str}",
            ]
            self._build_executable(build_dir, cmake_args=cmake_args)

            # Mark as built
            self._mark_as_built_locked(build_dir)

            # Return the path to the first partition
            return self.partition_helper.get_first_partition_path(build_dir)

    def _get_coverage_target(self) -> str:
        """
        {abstract}
        Get the coverage target.
        """
        raise NotImplementedError("_get_coverage_target() is not implemented")

    def _get_coverage_source_root(self) -> Path:
        """Directory passed to gcovr -r (repo root; must match testing_add_gcovr_coverage_target ROOT_DIR)."""
        raise NotImplementedError("_get_coverage_source_root() is not implemented")

    def _get_coverage_output_dir(self) -> Path:
        """Directory for index.html (must match testing_add_gcovr_coverage_target OUTPUT_DIR)."""
        raise NotImplementedError("_get_coverage_output_dir() is not implemented")

    def _get_coverage_gcovr_extra_excludes(self) -> list[str]:
        """Extra --exclude regexes for gcovr (e.g. OTA deps)."""
        return []

    def _get_coverage_gcovr_filters(self) -> list[str]:
        """
        Optional gcovr --filter regexes for paths relative to the repo root (-r).

        Gcovr resolves relative --filter against the process cwd at startup, not -r.
        POSIX coverage therefore runs gcovr with cwd = -r so patterns like
        ``^components/esp_rmaker_neo(/|$)`` match. Empty means include all sources under -r.
        """
        return []

    def _gcovr_filter_args_for_this_factory(self) -> list[str]:
        return _gcovr_filter_cli_args(self._get_coverage_gcovr_filters())

    def _collect_posix_shard_json_fragments(self) -> Optional[list[Path]]:
        """
        Produce one temporary gcovr --json file per run (gcda shard) under
        ``.posix_cov_json_parts/`` for this build dir.

        Returns None if there is no shard/legacy gcda tree so the ninja single-process
        path should be used instead.
        """
        if self.build_dir is None:
            return None

        bd = self.build_dir.resolve()
        run_gcda_roots = _discover_gcda_shard_roots(bd)
        if not run_gcda_roots:
            for run_dir in sorted(bd.glob("run-*")):
                g = run_dir / _GCOV_RUN_SUBDIR
                if g.is_dir() and any(g.rglob("*.gcda")):
                    run_gcda_roots.append(g)
        if not run_gcda_roots:
            return None

        gcovr = _find_gcovr()
        if not gcovr:
            return None

        root = self._get_coverage_source_root().resolve()
        gcov_exe = _gcov_executable_for_build(bd)
        exclude_args = _gcovr_exclude_cli_args(
            self._get_coverage_gcovr_extra_excludes()
        )
        filter_args = self._gcovr_filter_args_for_this_factory()
        json_dir = bd / _GCOV_RUN_JSON_PARTS_DIR
        if json_dir.exists():
            _rmtree_with_retries(json_dir, should_log=False)
        json_dir.mkdir(parents=True, exist_ok=True)
        _delete_gcda_gcov_in_build(bd)

        json_files: list[Path] = []
        for i, gcda_root in enumerate(run_gcda_roots):
            copied = _copy_gcda_into_build(gcda_root, bd)
            json_path = json_dir / f"part-{i:04d}.json"
            try:
                cmd = [
                    gcovr,
                    *_gcovr_verbose_cli_flags(),
                    "-r",
                    str(root),
                    *_GCOVR_MERGE_FUNCTIONS_ARGS,
                    *_GCOVR_IGNORE_FAILED_GCOV_ARGS,
                    *filter_args,
                    "--object-directory",
                    str(bd),
                    "--gcov-executable",
                    gcov_exe,
                    *exclude_args,
                    "--json",
                    str(json_path),
                    str(bd.resolve()),
                ]
                if self.should_log:
                    print(
                        f"POSIX coverage: capturing JSON for "
                        f"{gcda_root.name} ({len(copied)} gcda files)"
                    )
                # cwd must match -r: gcovr applies relative --filter vs cwd, not vs -r.
                subprocess.run(cmd, check=True, cwd=str(root))
                json_files.append(json_path)
            finally:
                _remove_files(copied)
        return json_files

    def _merge_run_json_shards_to_build_json(self, run_json_files: list[Path]) -> Path:
        """
        Merge per-run JSON shards into ``<build_dir>/posix-build-coverage.json``.

        Removes the temporary ``.posix_cov_json_parts/`` directory (run JSONs only).
        """
        if self.build_dir is None:
            raise ValueError(
                "build_dir is required for _merge_run_json_shards_to_build_json"
            )
        bd = self.build_dir.resolve()
        gcovr = _find_gcovr()
        if not gcovr:
            raise RuntimeError("gcovr not found on PATH")
        root = self._get_coverage_source_root().resolve()
        out = bd / _POSIX_BUILD_COVERAGE_JSON
        cmd: list[str] = [
            gcovr,
            *_gcovr_verbose_cli_flags(),
            "-r",
            str(root),
            *_GCOVR_MERGE_FUNCTIONS_ARGS,
        ]
        for jf in run_json_files:
            cmd.extend(["--json-add-tracefile", str(jf)])
        cmd.extend(["--json", str(out)])
        if self.should_log:
            print(
                f"POSIX coverage: merging {len(run_json_files)} run JSON shard(s) "
                f"→ {out.name}"
            )
        subprocess.run(cmd, check=True, cwd=str(root))
        json_dir = bd / _GCOV_RUN_JSON_PARTS_DIR
        if json_dir.exists():
            _rmtree_with_retries(json_dir, should_log=False)
        return out

    def _finalize_posix_coverage_after_report(self, bd: Path) -> None:
        """Remove shards, run-* worktrees, gcda/gcov. Keeps ``posix-build-coverage.json``."""
        _sweep_gcda_shards(bd, should_log=self.should_log)
        _sweep_orphan_posix_run_worktrees(bd, should_log=self.should_log)
        _cleanup_coverage_artifacts_in_build(bd)

    def generate_coverage_report(self) -> Optional[str]:
        """
        Generate the coverage report.  Returns the path to the HTML index.

        Pipeline for parallel POSIX sim instances:
          1. Clean stale .gcda/.gcov from the build tree (not under run-* or gcda-shards).
          2. For each gcda-shards/<run-id>/ tree (populated by stop() from run-*/gcda),
             copy .gcda next to .gcno, run gcovr --json per run → temporary run JSONs.
          3. Merge run JSONs with gcovr --json-add-tracefile → persistent
             ``<build_dir>/posix-build-coverage.json`` (temporary run JSONs removed).
          4. Merge that file to HTML + XML under the factory output directory.
          5. Remove gcda-shards, run-* worktrees, and other coverage artifacts (not the
             persistent build JSON).

        Falls back to the CMake ninja target when no shard (or legacy run-*/gcda) data exists.

        Callers must stop POSIX simulators first so stop() can archive flushed .gcda into
        gcda-shards/ before destroy() removes run-*.
        """
        if self.build_dir is None:
            return None

        bd = self.build_dir.resolve()
        json_dir = bd / _GCOV_RUN_JSON_PARTS_DIR

        try:
            json_files = self._collect_posix_shard_json_fragments()

            # No per-instance data → use the existing ninja target (single-process case)
            if json_files is None:
                out = ProcManager.run_cmd_sync(
                    ["ninja", "-C", str(bd), self._get_coverage_target()]
                )
                m = re.search(r"Generating coverage report @ (\S+)", out)
                if m:
                    _cleanup_coverage_artifacts_in_build(bd)
                    return m.group(1)
                return None

            gcovr = _find_gcovr()
            if not gcovr:
                if self.should_log:
                    print(
                        "Warning: gcovr not found on PATH; skipping parallel POSIX coverage merge."
                    )
                return None

            root = self._get_coverage_source_root().resolve()
            out_dir = self._get_coverage_output_dir().resolve()

            build_json = self._merge_run_json_shards_to_build_json(json_files)

            if out_dir.exists():
                _rmtree_with_retries(out_dir, should_log=self.should_log)
            out_dir.mkdir(parents=True, exist_ok=True)

            # Do not pass --filter here: gcovr applies it to --json-add-tracefile paths too.
            # Capture already restricted coverage; merge only combines tracefiles.
            merge_cmd: list[str] = [
                gcovr,
                *_gcovr_verbose_cli_flags(),
                "-r",
                str(root),
                *_GCOVR_MERGE_FUNCTIONS_ARGS,
                "--json-add-tracefile",
                str(build_json),
                "--html-details",
                str(out_dir / "index.html"),
                "--xml",
                str(out_dir / "coverage.xml"),
                "--print-summary",
            ]
            if self.should_log:
                print(f"POSIX coverage: generating HTML/XML from {build_json.name}")
            subprocess.run(merge_cmd, check=True, cwd=str(root))

            self._finalize_posix_coverage_after_report(bd)

            index_html = out_dir / "index.html"
            if self.should_log:
                print(f"Generating coverage report @ {index_html}")
            return str(index_html)

        except Exception as e:
            if json_dir.exists():
                _rmtree_with_retries(json_dir, should_log=False)
            if self.should_log:
                print(
                    f"Warning: POSIX coverage report failed ({type(e).__name__}): {e}"
                )
                traceback.print_exc()
            return None

    def destroy(self) -> None:
        """
        Destroy the factory.
        """
        pass  # Nothing to destroy


class FirmwareInstanceFactoryPosixHostCtrl(
    FirmwareInstanceFactoryPosix, FirmwareInstanceFactoryHostCtrl
):
    """
    A class that represents a factory for POSIX firmware instances with a host_ctrl.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        request_type: FirmwareInstanceFactoryHostCtrl.REQUEST_TYPES = FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_DEFAULT,
        should_log: bool = True,
    ):
        self._set_request_type(request_type)
        super().__init__(
            factory_config_factory=factory_config_factory,
            project_dir=RMNG_SDK_DEVICE_SIM_POSIX_DIR,
            executable_name="device-sim",
            should_log=should_log,
        )

    def get_id(self) -> str:
        """
        Get the ID of the firmware instance.
        """
        return super().get_id() + f"-host_ctrl-{self.request_type}"

    def _get_coverage_target(self) -> str:
        """
        Get the coverage target.
        """
        return "rmng_coverage_gen"

    def _get_coverage_source_root(self) -> Path:
        return RMNG_SDK_DIR

    def _get_coverage_output_dir(self) -> Path:
        return self.build_dir.resolve() / "rmng-coverage"

    def _get_coverage_gcovr_filters(self) -> list[str]:
        return [r"^components/esp_rmaker_neo(/|$)"]

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        """
        return FirmwareInstanceFactoryHostCtrl._get_add_configs(self)

    def get_next_instance(self, **kwargs) -> "FirmwareInstancePosixHostCtrl":
        """
        Get the next firmware instance.
        """
        self._build_if_not_built()
        return FirmwareInstancePosixHostCtrl(
            factory_config=self.factory_config_factory.create(),
            build_dir=self.build_dir,
            executable_name=self.executable_name,
            should_log=self.should_log,
        )

    def build_version_binary_if_not_built(self, version_str: str) -> Path:
        """
        Build a versioned binary and return the executable path.

        device-sim posix has no OTA partition layout — but its executable still
        carries the platform-common app_desc (PROJECT_NAME, PROJECT_VER), which
        is what the OTA image-header verifier reads. Returning the raw executable
        lets it serve as a cross-project OTA payload for negative tests.
        """
        build_dir = self._get_build_dir(version_str)
        with self._get_build_dir_lock(build_dir):
            executable_path = build_dir / self.executable_name
            if self._is_built_locked(build_dir):
                return executable_path

            self._remake_build_dir(build_dir)
            self._build_executable(
                build_dir, cmake_args=[f"-DPROJECT_VER={version_str}"]
            )
            self._mark_as_built_locked(build_dir)
            return executable_path

    def destroy(self) -> None:
        """
        Call the destroy methods of the parent classes.
        """
        FirmwareInstanceFactoryPosix.destroy(self)


class PosixOtaPartitionHelper:
    """
    A class that represents a helper for POSIX OTA testing.
    """

    def __init__(self):
        self._get_partition_path_info()

    def _get_partition_path_info(self):
        """
        Get partition path information from the osal/ota module.
        """
        # Read the osal_ota_posix_shared.h file
        osal_ota_posix_shared_path = (
            RMNG_SDK_OSAL_OTA_MODULE_DIR / "priv_include" / "osal_ota_posix_shared.h"
        )
        with open(osal_ota_posix_shared_path, "r") as f:
            content = f.read()

        # Find the partition folder name
        partition_folder_name_pattern = re.compile(
            r"#define OSAL_OTA_POSIX_BASE_DIR \"([^\"]*)\""
        )
        m = partition_folder_name_pattern.search(content)
        if m:
            self.partition_folder_name = m.group(1)
        else:
            raise RuntimeError("Failed to find partition folder name")

        # Find the first partition name
        first_partition_name_pattern = re.compile(
            r"#define OSAL_OTA_POSIX_PART_PREFIX \"([^\"]*)\""
        )
        m = first_partition_name_pattern.search(content)
        if m:
            self.first_partition_name = m.group(1) + "0"
        else:
            raise RuntimeError("Failed to find first partition name")

        # Find the OTA config file name
        ota_config_file_name_pattern = re.compile(
            r"#define OSAL_OTA_POSIX_CONFIG_FILE \"([^\"]*)\""
        )
        m = ota_config_file_name_pattern.search(content)
        if m:
            self.ota_config_file_name = m.group(1)
        else:
            raise RuntimeError("Failed to find OTA config file name")

    def get_first_partition_path(self, build_dir: Path) -> Path:
        """
        Get the path to the first partition.
        """
        partition_path = (
            build_dir / self.partition_folder_name / self.first_partition_name
        )
        if not partition_path.exists():
            raise RuntimeError(f"Partition path {partition_path} does not exist")
        return partition_path


class FirmwareInstanceFactoryPosixOta(
    FirmwareInstanceFactoryPosix, FirmwareInstanceFactoryOta
):
    """
    A class that represents a factory for POSIX firmware instances for OTA testing.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        ota_type: FirmwareInstanceFactoryOta.OTA_TYPES,
        should_log: bool = True,
    ):
        self._set_ota_type(ota_type)
        FirmwareInstanceFactoryPosix.__init__(
            self,
            factory_config_factory=factory_config_factory,
            project_dir=RMNG_SDK_OTA_SIM_POSIX_DIR,
            executable_name="ota-sim",
            should_log=should_log,
        )
        self.partition_helper = PosixOtaPartitionHelper()

    def get_id(self) -> str:
        """
        Get the ID of the firmware instance.
        """
        return super().get_id() + f"-ota-{self.ota_type}"

    def _get_coverage_target(self) -> str:
        """
        Get the coverage target.
        """
        return "rmng_ota_coverage_gen"

    def _get_coverage_source_root(self) -> Path:
        return RMNG_SDK_DIR

    def _get_coverage_output_dir(self) -> Path:
        return self.build_dir.resolve() / "rmng-ota-coverage"

    def _get_coverage_gcovr_extra_excludes(self) -> list[str]:
        return ["^.*/deps/.*"]

    def _get_coverage_gcovr_filters(self) -> list[str]:
        return [r"^components/esp_rmaker_neo_ota(/|$)"]

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        """
        return FirmwareInstanceFactoryOta._get_add_configs(self)

    def _build_executable(
        self,
        build_dir: Path,
        cmake_args: list[str] = [],
        add_configs: dict[str, str] = {},
    ) -> Path:
        """
        Build the executable.
        """
        add_cmake_args = [
            "-DTEST_RMNG_OTA=ON",
            *cmake_args,
        ]
        return super()._build_executable(
            build_dir, cmake_args=add_cmake_args, add_configs=add_configs
        )

    def get_next_instance(self, **kwargs) -> "FirmwareInstancePosixOta":
        """
        Get the next firmware instance.
        """
        self._build_if_not_built()
        return FirmwareInstancePosixOta(
            factory_config=self.factory_config_factory.create(),
            build_dir=self.build_dir,
            executable_name=self.executable_name,
            partition_helper=self.partition_helper,
            should_log=self.should_log,
        )


class FileLoggingStream(LoggingStream):
    """
    A class that represents a stream for logging to a named pipe.
    """

    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.pos = 0
        self.file = None
        self.is_open = False

    def read(self, size: int = 1024, timeout: float = 0.5) -> Optional[bytes]:
        start_time = time.time()

        while time.time() - start_time < timeout:
            if self.file is None:
                # File is not open
                self.open()
                continue

            # Seek to the current position
            self.file.seek(self.pos)

            # Try to read size bytes
            data = self.file.read(size)

            if not data:
                # Continue trying until timeout
                time.sleep(0.1)  # 100ms sleep
                continue

            # Update position
            self.pos += len(data)

            # Return data if we got any (including empty bytes)
            return data

        # Timeout reached, return empty bytes
        return b""

    def open(self):
        """
        Open the stream.
        """
        self.file = open(self.filepath, "rb")
        self.is_open = True

    def close(self):
        """
        Close the stream.
        """
        if self.file is not None:
            self.file.close()
            self.file = None
        self.is_open = False

    def __getstate__(self) -> dict:
        if self.is_open:
            self.close()
        # LoggingStream doesn't have __getstate__, so we handle state manually
        state = self.__dict__.copy()
        state.pop("file", None)
        return state

    def __setstate__(self, state: dict):
        # LoggingStream doesn't have __setstate__, so we handle state manually
        self.__dict__.update(state)
        self.file = None  # Don't restore file handle
        if self.is_open:
            self.open()


class FirmwareInstancePosix(FirmwareInstance):
    """
    A class that represents a POSIX firmware instance.
    """

    def __init__(
        self,
        factory_config: FactoryConfig,
        build_dir: Path,
        executable_name: str,
        should_log: bool = True,
        admin_user: Optional[User] = None,
    ):
        FirmwareInstance.__init__(
            self,
            factory_config=factory_config,
            should_log=should_log,
            admin_user=admin_user,
        )

        # Get the factory partition information
        self.factory_config.read_factory_config(build_dir / "config" / "sdkconfig.h")

        self.build_dir = build_dir
        self.executable_name = executable_name
        self.run_folder = self._get_run_folder()
        self.executable_path = self.run_folder / self.executable_name
        self.proc = None
        self.pid = None

        self._log_file_path = self.run_folder / "stdout.log"
        self._log_file_path.touch()
        self._log_stream = FileLoggingStream(self._log_file_path)

        # Register the factory config with AWS
        self.factory_config.register_with_cloud(admin_user=self.admin_user)

    def _get_run_folder(self) -> Path:
        """
        Copy the executable and required files to the run folder.
        """
        # Create the run folder
        run_folder = self.build_dir / f"run-{uuid.uuid4().hex[:8]}"
        run_folder.mkdir(parents=True, exist_ok=True)

        # Copy executable
        shutil.copy(
            self.build_dir / self.executable_name, run_folder / self.executable_name
        )

        # Build and install the factory partition
        self.factory_partition_path = self._build_and_install_factory_partition(
            run_folder
        )

        return run_folder

    def _build_and_install_factory_partition(self, build_dir: Path) -> Path:
        """
        Build the factory partition.
        """
        posix_bin_path = self.factory_config.to_posix_binary()
        target_bin_relative_path = Path(*posix_bin_path.parts[-2:])

        # Copy the factory partition to the build directory
        target_bin_path = build_dir / target_bin_relative_path
        target_bin_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(posix_bin_path, target_bin_path)

        # Clean up the factory file
        posix_bin_path.unlink()

        return target_bin_path

    def _start_internal(self):
        """
        Start the firmware instance internally.
        """
        if self.executable_path is None:
            raise RuntimeError("Executable path is not set")

        # Start the logging thread
        self._posix_start_logging_thread()

        # Create file to read from the process
        _log_file_handle = open(self._log_file_path, "ab")

        args = [str(self.executable_path)]
        # Force unbuffered output if possible
        if shutil.which("stdbuf"):
            args = ["stdbuf", "-oL", "-eL", *args]

        # Redirect coverage data per run folder so parallel instances do not clobber .gcda files.
        env = os.environ.copy()
        gcda_root = self.run_folder / _GCOV_RUN_SUBDIR
        gcda_root.mkdir(parents=True, exist_ok=True)
        env["GCOV_PREFIX"] = str(gcda_root.resolve()) + os.sep
        env["GCOV_PREFIX_STRIP"] = str(_gcov_prefix_strip_count(self.build_dir))

        # Start the process
        self.proc = subprocess.Popen(
            args=args,
            cwd=self.executable_path.parent,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=_log_file_handle,
            stderr=subprocess.STDOUT,
            close_fds=True,
            preexec_fn=os.setsid,  # Start as process group leader
        )
        self.pid = self.proc.pid

        # Transfer ownership of the file handle to the process
        _log_file_handle.close()

    def _stop_internal(self):
        """
        Stop the firmware instance internal.
        """
        terminate_timeout_seconds = 30
        if self.proc is not None:
            # Kill the entire process group (including child processes)
            try:
                pgid = os.getpgid(self.proc.pid)
                os.killpg(pgid, signal.SIGTERM)
                print("Waiting for process group to terminate")
                try:
                    ret = self.proc.wait(timeout=terminate_timeout_seconds)
                    print(f"Process group terminated with return code {ret}")
                except subprocess.TimeoutExpired:
                    print("Process group didn't terminate gracefully, killing it...")
                    os.killpg(pgid, signal.SIGKILL)
                    try:
                        ret = self.proc.wait(timeout=5)  # 5 more seconds after kill
                        print(f"Process group killed with return code {ret}")
                    except subprocess.TimeoutExpired:
                        print(
                            "Process group failed to die even after kill, continuing anyway..."
                        )
            except (ProcessLookupError, OSError) as e:
                print(f"Warning: Could not kill process group: {e}")
                # Fallback to killing just the main process
                self.proc.terminate()
                try:
                    ret = self.proc.wait(timeout=terminate_timeout_seconds)
                    print(f"Process terminated with return code {ret}")
                except subprocess.TimeoutExpired:
                    print("Process didn't terminate gracefully, killing it...")
                    self.proc.kill()
                    try:
                        ret = self.proc.wait(timeout=5)
                        print(f"Process killed with return code {ret}")
                    except subprocess.TimeoutExpired:
                        print(
                            "Process failed to die even after kill, continuing anyway..."
                        )
            self.proc = None
            self.pid = None
        elif self.pid is not None:
            try:
                pgid = os.getpgid(self.pid)
                os.killpg(pgid, signal.SIGTERM)
                _, ret = os.waitpid(pgid, 0)
                print(f"Process terminated with return code {ret}")
            except ChildProcessError as cp_error:
                print(f"Child process error when trying to kill process: {cp_error}")
            except ProcessLookupError as pl_error:
                print(f"Process lookup error when trying to kill process: {pl_error}")
            except PermissionError as pe_error:
                print(f"Permission denied when trying to kill process: {pe_error}")
            except Exception as e:
                print(f"Unknown error when trying to kill process: {e}")
            self.pid = None

        # Persist .gcda under build_dir/gcda-shards/ before destroy() removes run-*/
        if hasattr(self, "run_folder") and self.run_folder is not None:
            gcda_dir = self.run_folder / _GCOV_RUN_SUBDIR
            if not gcda_dir.is_dir():
                gcda_dir.mkdir(parents=True, exist_ok=True)
            _wait_for_gcda_flush(gcda_dir, should_log=self.should_log)
        _archive_run_gcda_shard(self.build_dir, self.run_folder)

        # Stop the logging thread
        self._posix_stop_logging_thread()

    def _posix_start_logging_thread(self):
        """
        Start the logging thread.
        """
        self._log_stream = FileLoggingStream(self._log_file_path)
        self._log_stream.open()
        self._start_logging_thread(log_stream=self._log_stream)

    def _posix_stop_logging_thread(self):
        """
        Stop the logging thread.
        """
        if self._log_stream is not None:
            self._log_stream.close()
            self._log_stream = None
        self._stop_logging_thread()

    def start(self) -> bool:
        """
        Start the firmware instance.
        """
        self._start_internal()
        self._mark_as_running()
        return True

    def stop(self) -> bool:
        """
        Stop the firmware instance.
        """
        self._stop_internal()
        self._mark_as_not_running()
        return True

    def destroy(self):
        """
        Destroy the firmware instance.
        """
        self._destroy_internal()

        # Clean up the run folder
        if (
            hasattr(self, "run_folder")
            and self.run_folder is not None
            and self.run_folder.exists()
        ):
            shutil.rmtree(self.run_folder, ignore_errors=True)
            self.run_folder = None

    def __getstate__(self) -> dict:
        state = super().__getstate__()
        state.pop("proc", None)
        return state

    def __setstate__(self, state: dict):
        super().__setstate__(state)
        self.proc = None
        if self.is_running():
            self._start_logging_thread(log_stream=self._log_stream)


class FirmwareInstancePosixHostCtrl(FirmwareInstancePosix, FirmwareInstanceHostCtrl):
    """
    A class that represents a POSIX firmware instance with a host_ctrl.
    """

    _LOCAL_CTRL_HTTP_PORT_MIN = 49153
    _LOCAL_CTRL_HTTP_PORT_MAX = 65535
    _posix_lc_next_port = _LOCAL_CTRL_HTTP_PORT_MIN
    _posix_lc_claimed = set()
    _posix_lc_lock = Lock()

    @classmethod
    def _claim_posix_local_ctrl_http_port(cls) -> int:
        """
        Pick a port not claimed by other test instances and bindable on loopback (probe bind, then release).
        """
        span = cls._LOCAL_CTRL_HTTP_PORT_MAX - cls._LOCAL_CTRL_HTTP_PORT_MIN + 1
        with cls._posix_lc_lock:
            for _ in range(span):
                port = cls._posix_lc_next_port
                cls._posix_lc_next_port += 1
                if cls._posix_lc_next_port > cls._LOCAL_CTRL_HTTP_PORT_MAX:
                    cls._posix_lc_next_port = cls._LOCAL_CTRL_HTTP_PORT_MIN
                if port in cls._posix_lc_claimed:
                    continue
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                    try:
                        sock.bind(("127.0.0.1", port))
                    except OSError:
                        continue
                cls._posix_lc_claimed.add(port)
                return port
        raise RuntimeError(
            f"No free TCP port in [{cls._LOCAL_CTRL_HTTP_PORT_MIN}, {cls._LOCAL_CTRL_HTTP_PORT_MAX}] "
            "for POSIX local-control HTTP (test allocation)"
        )

    def _release_posix_local_ctrl_http_port_claim(self) -> None:
        port = getattr(self, "_posix_instance_local_ctrl_http_port", None)
        if port is None:
            return
        with self._posix_lc_lock:
            self._posix_lc_claimed.discard(port)
        self._posix_instance_local_ctrl_http_port = None

    def __init__(self, *args, **kwargs):
        FirmwareInstancePosix.__init__(self, *args, **kwargs)
        self._posix_instance_local_ctrl_http_port = (
            self._claim_posix_local_ctrl_http_port()
        )

    def start(self) -> bool:
        """
        Start the host_ctrl firmware instance, and capture the serial port.
        """
        host_ctrl_port = None
        try:
            self._start_internal()

            # Wait for the serial port file to be created and read the port
            serial_port_file = self.run_folder / "port.out"
            timeout_seconds = 10.0
            poll_interval = 0.1
            start_time = time.time()

            while time.time() - start_time < timeout_seconds:
                if serial_port_file.exists():
                    try:
                        with open(serial_port_file, "r") as f:
                            host_ctrl_port = f.read().strip()
                        if host_ctrl_port:
                            break
                    except (OSError, IOError):
                        pass  # File might still be being written, try again
                time.sleep(poll_interval)

            if host_ctrl_port is None or not host_ctrl_port:
                raise RuntimeError(
                    f"Failed to read serial port from file within {timeout_seconds} seconds"
                )
            print(f"Serial port: {host_ctrl_port}")

            port_manager = PortManagerSingle(
                host_ctrl_port, 115200, HostCtrlGlobals.protocol
            )
            self._setup_host_ctrl_variables(port_manager)
            self._mark_as_running()
            return True
        except BaseException:
            self._release_posix_local_ctrl_http_port_claim()
            raise

    def get_host_ctrl(self) -> NodeHostCtrl:
        """
        Return a NodeHostCtrl after pushing the per-instance local control HTTP port to the sim.
        """
        host_ctrl = super().get_host_ctrl()
        port = getattr(self, "_posix_instance_local_ctrl_http_port", None)
        if port is not None:
            if not host_ctrl.set_local_ctrl_http_port(port):
                print(
                    f"WARN: Failed to set local control HTTP port {port} on POSIX device-sim via host_ctrl"
                )
        return host_ctrl

    def destroy(self) -> None:
        self._release_posix_local_ctrl_http_port_claim()
        super().destroy()

    def run_unit_tests(self):
        """
        Run the unit tests.
        """
        if self.build_dir is None:
            raise RuntimeError("Build directory is not set, run build() first")

        # Copy the factory partition to the test_rmaker_neo directory
        factory_partition_relative_path = Path(*self.factory_partition_path.parts[-2:])
        test_rmng_bin_path = (
            self.build_dir
            / "components"
            / "esp_rmaker_neo"
            / "test_rmaker_neo"
            / factory_partition_relative_path
        )
        test_rmng_bin_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(self.factory_partition_path, test_rmng_bin_path)

        ProcManager.run_cmd_sync(
            ["ninja", "-C", str(self.build_dir), "rmng_coverage_test"]
        )


class FirmwareInstancePosixOta(FirmwareInstancePosix, FirmwareInstanceOta):
    """
    A class that represents a POSIX firmware instance for OTA testing.
    """

    def __init__(
        self,
        factory_config: FactoryConfig,
        build_dir: Path,
        executable_name: str,
        partition_helper: PosixOtaPartitionHelper,
        should_log: bool = True,
    ):
        self.partition_helper = partition_helper
        FirmwareInstanceOta.__init__(
            self, factory_config=factory_config, should_log=should_log
        )
        FirmwareInstancePosix.__init__(
            self,
            factory_config=factory_config,
            build_dir=build_dir,
            executable_name=executable_name,
            should_log=should_log,
        )

    def _get_run_folder(self) -> Path:
        """
        Get the run folder.
        """
        run_folder = super()._get_run_folder()

        # Copy the partitions
        shutil.copytree(
            self.build_dir / self.partition_helper.partition_folder_name,
            run_folder / self.partition_helper.partition_folder_name,
            dirs_exist_ok=True,
        )

        # Copy the original binary to a temporary file
        self.base_binary_path = run_folder / "base-binary.bin"
        shutil.copy(
            self.partition_helper.get_first_partition_path(run_folder),
            self.base_binary_path,
        )

        return run_folder

    def reset_ota_state(self) -> bool:
        """
        Reset the OTA state of the firmware instance.
        """
        # Stop the firmware instance
        if not self.stop():
            return False

        # Wipe the OTA config file
        ota_config_file_path = (
            self.run_folder
            / self.partition_helper.partition_folder_name
            / self.partition_helper.ota_config_file_name
        )
        if not ota_config_file_path.exists():
            print(
                f"WARN: OTA config file path {ota_config_file_path} does not exist, assume reset"
            )
        else:
            ota_config_file_path.unlink()

        # Copy back the original file
        if self.base_binary_path is None:
            print("WARN: Base binary path is not set, cannot reset OTA state")
            return False
        shutil.copy(
            self.base_binary_path,
            self.partition_helper.get_first_partition_path(self.run_folder),
        )

        return True


def _posix_merged_coverage_default_dir(factories: list, *, should_log: bool) -> Path:
    """
    ``<shared build folder>/coverage_reports/posix-merged-coverage``.

    Preset CMake dirs are ``<cwd>/build/<preset>/`` (see FirmwareInstanceFactory._get_build_dir);
    the shared folder is the parent of each ``build_dir``.
    """
    parents = [
        f.build_dir.resolve().parent for f in factories if f.build_dir is not None
    ]
    if not parents:
        raise ValueError(
            "merge_posix_factory_coverage_reports: missing build_dir on factory"
        )
    unique = set(parents)
    if len(unique) > 1 and should_log:
        print(
            "Warning: POSIX factories use different build folder parents; "
            f"merged report uses {parents[0]} (first factory)."
        )
    return parents[0] / "coverage_reports" / "posix-merged-coverage"


def merge_posix_factory_coverage_reports(
    factories: list,
    *,
    merged_output_dir: Optional[Path] = None,
    should_log: bool = True,
) -> Optional[str]:
    """
    One HTML/XML coverage report for all POSIX factories (e.g. device-sim + ota-sim).

    For each build tree: run shards → ``posix-build-coverage.json`` (kept), then merge
    those per-build JSON files into a single session report under
    ``<cwd>/build/coverage_reports/posix-merged-coverage`` (sibling of preset build dirs),
    unless ``merged_output_dir`` is set.

    Each factory's _get_coverage_gcovr_filters() is applied during run-level JSON capture.
    If only one factory is present, delegates to generate_coverage_report().
    If multiple factories are present but any lacks shard gcda data, returns None so
    callers can fall back to per-factory reports.
    """
    if not factories:
        return None
    if len(factories) == 1:
        return factories[0].generate_coverage_report()

    gcovr = _find_gcovr()
    if not gcovr:
        if should_log:
            print("Warning: gcovr not found on PATH; skipping merged POSIX coverage.")
        return None

    ordered = sorted(factories, key=lambda f: str(f.build_dir or ""))
    build_json_paths: list[Path] = []
    for f in ordered:
        if f.build_dir is None:
            return None
        frags = f._collect_posix_shard_json_fragments()
        if frags is None:
            if should_log:
                print(
                    "POSIX merged coverage: not all builds use shard gcda; "
                    "use per-factory reports instead."
                )
            return None
        build_json_paths.append(f._merge_run_json_shards_to_build_json(frags))

    root = RMNG_SDK_DIR.resolve()
    out_dir = (
        merged_output_dir.resolve()
        if merged_output_dir is not None
        else _posix_merged_coverage_default_dir(ordered, should_log=should_log)
    )

    try:
        if out_dir.exists():
            _rmtree_with_retries(out_dir, should_log=should_log)
        out_dir.mkdir(parents=True, exist_ok=True)

        merge_cmd: list[str] = [
            gcovr,
            *_gcovr_verbose_cli_flags(),
            "-r",
            str(root),
            *_GCOVR_MERGE_FUNCTIONS_ARGS,
        ]
        for bj in build_json_paths:
            merge_cmd.extend(["--json-add-tracefile", str(bj)])
        merge_cmd.extend(
            [
                "--html-details",
                str(out_dir / "index.html"),
                "--xml",
                str(out_dir / "coverage.xml"),
                "--print-summary",
            ]
        )
        if should_log:
            print(
                f"POSIX coverage: merging {len(build_json_paths)} build JSON file(s) "
                f"from {len(ordered)} build tree(s) → session report"
            )
        subprocess.run(merge_cmd, check=True, cwd=str(root))

        for f in ordered:
            f._finalize_posix_coverage_after_report(f.build_dir.resolve())

        index_html = out_dir / "index.html"
        if should_log:
            print(f"Generating merged coverage report @ {index_html}")
        return str(index_html)
    except Exception as e:
        if should_log:
            print(f"Warning: merged POSIX coverage failed ({type(e).__name__}): {e}")
            traceback.print_exc()
        return None

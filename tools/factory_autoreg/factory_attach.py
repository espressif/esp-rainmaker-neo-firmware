#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path


QUIT_WORDS = {"q", "quit"}


def _is_quit(value: str) -> bool:
    return value.strip().lower() in QUIT_WORDS


def _parse_offset(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"Invalid offset '{value}'. Use decimal or hex, e.g. 0x340000."
        ) from exc


def _parse_truncated(value: str) -> int:
    try:
        length = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"Invalid truncated value '{value}'. Must be a positive integer."
        ) from exc
    if length <= 0:
        raise argparse.ArgumentTypeError("truncated must be greater than 0")
    return length


def _list_dirs(path: Path) -> list[Path]:
    if not path.is_dir():
        return []
    return sorted((p for p in path.iterdir() if p.is_dir()), key=lambda p: p.name)


def _prompt(prompt: str) -> str:
    try:
        return input(prompt).strip()
    except EOFError:
        return "q"


def _choose_from_paths(items: list[Path], prompt: str) -> Path | None:
    while True:
        for idx, item in enumerate(items, start=1):
            print(f"  {idx}) {item.name}")
        selection = _prompt(prompt)
        if _is_quit(selection):
            return None
        if selection.isdigit():
            choice = int(selection)
            if 1 <= choice <= len(items):
                return items[choice - 1]
        print(
            f"Invalid selection '{selection}'. Enter 1-{len(items)} or q/quit.",
            file=sys.stderr,
        )


def _parse_selection_indices(selection: str, max_index: int) -> list[int] | None:
    raw = selection.strip()
    if raw.startswith("(") and raw.endswith(")"):
        raw = raw[1:-1].strip()
    if not raw:
        return None

    result: list[int] = []
    seen: set[int] = set()
    parts = [p.strip() for p in raw.split(",") if p.strip()]
    if not parts:
        return None

    for part in parts:
        if "-" in part:
            start_s, end_s = part.split("-", 1)
            if not start_s.isdigit() or not end_s.isdigit():
                return None
            start = int(start_s)
            end = int(end_s)
            if start < 1 or end < 1 or start > end or end > max_index:
                return None
            for idx in range(start, end + 1):
                if idx not in seen:
                    seen.add(idx)
                    result.append(idx)
            continue

        if not part.isdigit():
            return None
        idx = int(part)
        if idx < 1 or idx > max_index:
            return None
        if idx not in seen:
            seen.add(idx)
            result.append(idx)

    return result if result else None


def _choose_things_or_all(items: list[Path], prompt: str) -> list[Path] | str | None:
    while True:
        print("  0) ALL")
        for idx, item in enumerate(items, start=1):
            print(f"  {idx}) {item.name}")
        selection = _prompt(prompt)
        if _is_quit(selection):
            return None
        if selection == "0":
            return "ALL"
        indices = _parse_selection_indices(selection, len(items))
        if indices:
            return [items[i - 1] for i in indices]
        print(
            (
                f"Invalid selection '{selection}'. Enter 0 for ALL, single number, "
                f"or range/list like 1-4,5 (or (1-4,5)); max={len(items)}."
            ),
            file=sys.stderr,
        )


def _load_flash_args_tokens(flash_args: Path) -> list[str]:
    tokens: list[str] = []
    for line in flash_args.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        tokens.extend(shlex.split(stripped))
    return tokens


def _is_int_token(value: str) -> bool:
    try:
        int(value, 0)
        return True
    except ValueError:
        return False


def _with_replaced_partition(
    tokens: list[str], offset: int, factory_bin: Path
) -> list[str]:
    filtered: list[str] = []
    i = 0
    while i < len(tokens):
        if (
            i + 1 < len(tokens)
            and _is_int_token(tokens[i])
            and not tokens[i + 1].startswith("-")
        ):
            token_offset = int(tokens[i], 0)
            if token_offset == offset:
                i += 2
                continue
            filtered.extend((tokens[i], tokens[i + 1]))
            i += 2
            continue
        filtered.append(tokens[i])
        i += 1

    filtered.extend((hex(offset), str(factory_bin.resolve())))
    return filtered


def _resolve_factory_bin(thing_dir: Path) -> Path | None:
    esp_dir = thing_dir / "esp-idf"
    if not esp_dir.is_dir():
        return None
    bins = sorted(esp_dir.glob("*.bin"))
    if len(bins) != 1:
        return None
    return bins[0]


def _load_project_description(build_dir: Path) -> tuple[str, str]:
    project_desc = build_dir / "project_description.json"
    if not project_desc.is_file():
        raise FileNotFoundError(f"project_description.json not found: {project_desc}")

    data = json.loads(project_desc.read_text(encoding="utf-8"))
    project_name = str(data.get("project_name", "")).strip()
    if not project_name:
        raise ValueError(f"project_name missing in {project_desc}")
    target = str(data.get("target", "")).strip()
    if not target:
        raise ValueError(f"target missing in {project_desc}")
    return target, project_name


def _build_output_name(
    target: str, project_name: str, thing_name: str, truncated: int | None
) -> str:
    thing_part = thing_name if truncated is None else thing_name[:truncated]
    return f"{target}-{project_name}-{thing_part}.bin"


def _merge_for_thing(
    build_dir: Path,
    flash_args_tokens: list[str],
    offset: int,
    binaries_root: Path,
    thing_dir: Path,
    target: str,
    project_name: str,
    truncated: int | None,
) -> int:
    factory_bin = _resolve_factory_bin(thing_dir)
    if factory_bin is None:
        print(
            f"Skipping '{thing_dir.name}': expected exactly one ESP-IDF factory .bin under "
            f"'{thing_dir / 'esp-idf'}'.",
            file=sys.stderr,
        )
        return 1

    out_name = _build_output_name(target, project_name, thing_dir.name, truncated)
    binaries_root.mkdir(parents=True, exist_ok=True)
    out_bin = binaries_root / out_name
    args = _with_replaced_partition(flash_args_tokens, offset, factory_bin)
    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        target,
        "merge_bin",
        "-o",
        str(out_bin.resolve()),
        *args,
    ]

    print(f"Merging '{thing_dir.name}' with factory '{factory_bin.name}'...")
    result = subprocess.run(cmd, cwd=build_dir, check=False)
    if result.returncode == 0:
        print(f"Wrote merged binary: {out_bin}")
    else:
        print(
            f"Merge failed for '{thing_dir.name}' (exit {result.returncode}).",
            file=sys.stderr,
        )
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Attach factory ESP-IDF partition binaries from factory_autoreg artifacts into merged flash binaries "
            "using an ESP-IDF build directory flash_args."
        )
    )
    parser.add_argument(
        "build_dir", type=Path, help="ESP-IDF build directory containing flash_args"
    )
    parser.add_argument(
        "offset", type=_parse_offset, help="Factory partition offset (hex or decimal)"
    )
    parser.add_argument(
        "--truncated",
        type=_parse_truncated,
        default=None,
        help="Optional thing name truncation length for output name (default: full thing name)",
    )
    parser.add_argument(
        "--binaries-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "binaries",
        help="Output root for merged binaries (default: tools/factory_autoreg/binaries)",
    )
    parser.add_argument(
        "artifacts_dir",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent / "outputs",
        help="Artifacts root (default: tools/factory_autoreg/outputs)",
    )
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    artifacts_dir = args.artifacts_dir.resolve()
    binaries_root = args.binaries_dir.resolve()
    flash_args = build_dir / "flash_args"

    if not build_dir.is_dir():
        print(f"ERROR: build_dir does not exist: {build_dir}", file=sys.stderr)
        return 2
    if not flash_args.is_file():
        print(
            f"ERROR: flash_args not found in build_dir: {flash_args}", file=sys.stderr
        )
        return 2
    if not artifacts_dir.is_dir():
        print(f"ERROR: artifacts_dir does not exist: {artifacts_dir}", file=sys.stderr)
        return 2

    try:
        target, project_name = _load_project_description(build_dir)
    except (FileNotFoundError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    flash_args_tokens = _load_flash_args_tokens(flash_args)
    if not flash_args_tokens:
        print(f"ERROR: flash_args is empty: {flash_args}", file=sys.stderr)
        return 2

    print("Enter 'q' or 'quit' in any prompt to exit.")
    print(f"Build dir: {build_dir}")
    print(f"Target: {target}")
    print(f"Project name: {project_name}")
    print(f"Artifacts dir: {artifacts_dir}")
    print(f"Binaries dir: {binaries_root}")
    print(f"Factory offset: {hex(args.offset)}")
    if args.truncated is not None:
        print(f"Thing name truncation: {args.truncated}")

    while True:
        account_dirs = _list_dirs(artifacts_dir)
        if not account_dirs:
            print(
                f"No stack account folders found under: {artifacts_dir}",
                file=sys.stderr,
            )
            return 1

        print("\nAvailable stack account IDs:")
        account_choice = _choose_from_paths(
            account_dirs, "Select stack account ID [number, q=quit]> "
        )
        if account_choice is None:
            break

        account_dir = account_choice
        account_in = account_dir.name

        thing_types = _list_dirs(account_dir)
        if not thing_types:
            print(f"No thing type folders under: {account_dir}", file=sys.stderr)
            continue

        print(f"Available thing types in '{account_in}':")
        type_choice = _choose_from_paths(
            thing_types, "Select thing type [number, q=quit]> "
        )
        if type_choice is None:
            break

        type_dir = type_choice
        type_in = type_dir.name

        while True:
            thing_dirs = _list_dirs(type_dir)
            if not thing_dirs:
                print(f"No thing directories under: {type_dir}", file=sys.stderr)
                break

            print(f"\nAvailable things in '{account_in}/{type_in}':")
            thing_choice = _choose_things_or_all(
                thing_dirs,
                "Select thing [0=ALL, number/range/list, q=back]> ",
            )
            if thing_choice is None:
                break

            if thing_choice == "ALL":
                print(f"Merging ALL things in '{account_in}/{type_in}'...")
                selected_things = thing_dirs
            else:
                selected_things = thing_choice

            for thing_dir in selected_things:
                _merge_for_thing(
                    build_dir,
                    flash_args_tokens,
                    args.offset,
                    binaries_root,
                    thing_dir,
                    target,
                    project_name,
                    args.truncated,
                )

    return 0


if __name__ == "__main__":
    sys.exit(main())

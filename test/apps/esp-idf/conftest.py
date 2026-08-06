# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import json
from pathlib import Path
from typing import Optional

import pytest


def _built_target(config: pytest.Config) -> Optional[str]:
    """Chip the build directory was configured for, or None if there is no readable build."""
    app_path = config.getoption("--app-path", None) or Path(__file__).parent
    build_dir = config.getoption("--build-dir", None) or "build"
    # Multi-DUT runs pass "|"-separated paths; no single target to resolve.
    if "|" in str(app_path) or "|" in str(build_dir):
        return None

    description = Path(app_path) / build_dir / "project_description.json"
    try:
        return json.loads(description.read_text())["target"]
    except (OSError, ValueError, KeyError):
        return None


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    """Keep only the parametrized target the build is actually for."""
    target = config.getoption("--target", None)
    if not target or target == "auto":
        target = _built_target(config)
    if not target:
        return

    selected, deselected = [], []
    for item in items:
        item_target = getattr(item, "callspec", None) and item.callspec.params.get(
            "target"
        )
        (selected if item_target in (None, target) else deselected).append(item)

    if deselected:
        config.hook.pytest_deselected(items=deselected)
        items[:] = selected

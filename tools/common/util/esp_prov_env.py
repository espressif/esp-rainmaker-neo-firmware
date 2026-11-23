# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Loader for the pinned esp_prov helpers (security schemes and transports)."""

import os
import sys
from pathlib import Path
from typing import Any, Optional

from util.github_deps import (
    ESP_IDF_REF,
    ESP_IDF_REPO_URL,
    IDF_EXTRA_COMPONENTS_REF,
    IDF_EXTRA_COMPONENTS_REPO_URL,
    NETWORK_PROVISIONING_REL,
)
from util.github_sync import (
    get_github_archive_repo_root,
    materialize_github_folder,
)

_cached: Optional[tuple[Any, Any]] = None


def import_esp_prov() -> tuple[Any, Any]:
    """
    Import the esp_prov ``security`` and ``transport`` modules.

    Returns:
        tuple: (security_module, transport_module)
    """
    global _cached
    if _cached is not None:
        return _cached

    # esp_prov/proto loads the protocomm *_pb2 modules via os.environ["IDF_PATH"].
    prev_idf_path = os.environ.get("IDF_PATH")
    paths_added: list[str] = []
    try:
        idf_root = get_github_archive_repo_root(ESP_IDF_REPO_URL, ESP_IDF_REF)
        os.environ["IDF_PATH"] = str(idf_root)

        np_root = materialize_github_folder(
            IDF_EXTRA_COMPONENTS_REPO_URL,
            IDF_EXTRA_COMPONENTS_REF,
            NETWORK_PROVISIONING_REL,
        )
        esp_prov_dir = str((Path(np_root) / "tool" / "esp_prov").resolve())
        paths_added.append(esp_prov_dir)
        if esp_prov_dir not in sys.path:
            sys.path.insert(0, esp_prov_dir)

        # Both packages import their submodules eagerly, so sys.modules keeps
        # them after the path is removed below.
        import security
        import transport

        _cached = (security, transport)
        return _cached

    except ImportError as e:
        raise RuntimeError(
            f"Failed to import ESP-IDF security modules from GitHub (esp_prov / protocomm): {e}"
        ) from e

    finally:
        for p in reversed(paths_added):
            if p in sys.path:
                sys.path.remove(p)
        if prev_idf_path is not None:
            os.environ["IDF_PATH"] = prev_idf_path
        else:
            os.environ.pop("IDF_PATH", None)

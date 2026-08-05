# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: F403 -- esp-docs pattern: star-import the base config, then mutate names it defines

# English language Sphinx config.
#
# Uses ../conf_common.py for all non-language-specific settings.

try:
    from conf_common import *
except ImportError:
    import os
    import sys

    sys.path.insert(0, os.path.abspath("../"))
    from conf_common import *

# -- Project information -----------------------------------------------------

project = "ESP RainMaker Neo SDK Programming Guide"
copyright = "2026, Espressif Systems (Shanghai) CO., LTD"
author = "Espressif Systems"

# No autodoc_mock_imports: unlike esp-rainmaker there is no Python CLI to
# document here, only the C API.

language = "en"

# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Pinned GitHub sources for ESP-IDF Python helpers (no local ESP-IDF clone)."""

ESP_IDF_REPO_URL = "https://github.com/espressif/esp-idf"
ESP_IDF_REF = "v6.0.2"

IDF_EXTRA_COMPONENTS_REPO_URL = "https://github.com/espressif/idf-extra-components"
IDF_EXTRA_COMPONENTS_REF = "master"

# Paths inside the repository archives (see esp-idf and idf-extra-components layouts).
PROTOCOMM_PYTHON_REL = "components/protocomm/python"
# Full component tree so esp_prov/proto can resolve protocomm and network_provisioning/python.
NETWORK_PROVISIONING_REL = "network_provisioning"

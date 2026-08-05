# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: F821 -- cmake-format evaluates this module with `section` injected

# pyright: reportUndefinedVariable=false
# cmake-format evaluates this module with `section` injected.

# Custom commands in this repo, ESP-IDF public CMake API (see IDF tools/cmake/{build,component}.cmake), and bundled
# third-party test helpers — cmake-format `additional_commands`.
with section("parse"):
    additional_commands = {
        "_github_resolve_ref_to_sha": {"pargs": 4},
        "_kconfig_emit": {"pargs": 4},
        "_rmng_menuconfig_get_paths": {"pargs": 3},
        "append": {"pargs": "+"},
        "append_line": {"pargs": "+"},
        "create_test": {"pargs": "+"},
        "determine_target_architecture": {"pargs": 1},
        "download_github_file": {"pargs": 5},
        "download_github_folder": {"pargs": 5},
        "http_common_curl_make_available": {"pargs": 0},
        "idf_build_component": {"pargs": 1},
        "idf_build_executable": {"pargs": 1},
        "idf_build_get_config": {
            "flags": ["GENERATOR_EXPRESSION"],
            "pargs": 2,
        },
        "idf_build_get_property": {
            "flags": ["GENERATOR_EXPRESSION"],
            "pargs": 2,
        },
        "idf_build_process": {
            "kwargs": {
                "BUILD_DIR": 1,
                "COMPONENTS": "+",
                "PROJECT_DIR": 1,
                "PROJECT_NAME": 1,
                "PROJECT_VER": 1,
                "SDKCONFIG": 1,
                "SDKCONFIG_DEFAULTS": "+",
            },
            "pargs": 1,
        },
        "idf_build_set_property": {
            "flags": ["APPEND"],
            "pargs": 2,
        },
        "idf_build_unset_property": {"pargs": 1},
        "idf_component_add_link_dependency": {
            "kwargs": {
                "FROM": 1,
                "TO": 1,
            },
        },
        "idf_component_get_property": {
            "flags": ["GENERATOR_EXPRESSION"],
            "pargs": 3,
        },
        "idf_component_mock": {
            "kwargs": {
                "INCLUDE_DIRS": "+",
                "MOCK_HEADER_FILES": "+",
                "REQUIRES": "+",
            },
        },
        "idf_component_optional_requires": {"pargs": "1+"},
        "idf_component_register": {
            "flags": ["WHOLE_ARCHIVE"],
            "kwargs": {
                "EMBED_FILES": "+",
                "EMBED_TXTFILES": "+",
                "EXCLUDE_SRCS": "+",
                "INCLUDE_DIRS": "+",
                "KCONFIG": 1,
                "KCONFIG_PROJBUILD": 1,
                "LDFRAGMENTS": "+",
                "PRIV_INCLUDE_DIRS": "+",
                "PRIV_REQUIRES": "+",
                "REQUIRED_IDF_TARGETS": "+",
                "REQUIRES": "+",
                "SRCS": "+",
                "SRC_DIRS": "+",
            },
        },
        "idf_component_set_property": {
            "flags": ["APPEND"],
            "pargs": 3,
        },
        "kconfig_generate": {
            "kwargs": {
                "OUTPUT": 1,
                "ROOT": 1,
            },
        },
        "kconfig_register": {
            "kwargs": {
                "CHILDREN": "+",
                "KCONFIGS": "+",
                "NAME": 1,
            },
        },
        "make_and_append_identifier": {"pargs": "+"},
        "mbedtls_install_requirements": {"pargs": 1},
        "protocomm_posix_civetweb_make_available": {"pargs": 0},
        "REMOVE_DUPLICATE_PATHS": {"pargs": 1},
        "rmng_fetchcontent_acquire_lock": {"pargs": 2},
        "rmng_fetchcontent_release_lock": {"pargs": 1},
        "rmng_git_working_tree_current_commit": {"pargs": 2},
        "rmng_idf_components_import": {
            "kwargs": {
                "DEPENDENCIES": "+",
                "DIR": 1,
                "RESOLUTION": 1,
            },
            "pargs": 1,
        },
        "rmng_menuconfig_configure_cmake_variables": {
            "kwargs": {
                "COMPONENT": 1,
                "CONFIG_FILE": 1,
                "DEFAULTS_FILE": "+",
                "OUTPUT_VARIABLE": 1,
            },
        },
        "rmng_python_get_executable": {"pargs": 2},
        "rmng_python_venv_prepare": {
            "kwargs": {
                "DEPS": "+",
                "PYTHON_EXECUTABLE": 1,
            },
            "pargs": 1,
        },
        "rmng_python_venv_run": {
            "kwargs": {
                "ARGS": "+",
                "FUNCTION": 1,
                "MODULE": 1,
                "WORKING_DIRECTORY": 1,
            },
            "pargs": 1,
        },
        "rmng_setup_menuconfig": {
            "kwargs": {
                "COMPONENT": 1,
                "CONFIG_FILE": 1,
            },
        },
        "target_add_binary_data": {
            "kwargs": {
                "DEPENDS": "+",
                "RENAME_TO": 1,
            },
            "pargs": 3,
        },
        "testing_add_gcovr_coverage_target": {
            "kwargs": {
                "EXCLUDES": "+",
                "FILTERS": "+",
                "NAME": 1,
                "OUTPUT_DIR": 1,
                "ROOT_DIR": 1,
                "TEST_DIR": 1,
            },
        },
        "testing_enable_coverage": {"pargs": 1},
        "unity_make_available": {"pargs": 0},
    }

with section("format"):
    line_width = 120
    tab_size = 4
    use_tabchars = False
    separate_ctrl_name_with_space = True
    dangle_parens = True
    dangle_align = "prefix"
    line_ending = "unix"

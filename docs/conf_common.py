# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: F403, F405 -- esp-docs pattern: star-import the base config, then mutate names it defines

# Common (non-language-specific) Sphinx configuration.
#
# Imported by en/conf.py. Everything here is shared by all languages.

# type: ignore
# pylint: disable=wildcard-import
# pylint: disable=undefined-variable


import os
import sys

from esp_docs.conf_docs import *

sys.path.insert(0, os.path.abspath("."))

extensions += [
    "sphinx_copybutton",
    # The spec pages under en/specs/ are markdown; the c-api-reference pages are
    # not, and cannot be -- 'include-build-file' resolves its argument against
    # the source directory rather than the build directory when invoked from a
    # MyST page, so the Doxygen fragments are not found. Mixing the two formats
    # in one project is otherwise fine: cross-references resolve in both
    # directions. See docs/README.md.
    "myst_parser",
    # Moves the page hierarchy out of the pages and into en/_toc.yml -- a fenced
    # ```{toctree} is a literal code block when the file is viewed on GitHub, and
    # there is no spelling of the directive that renders in both places.
    "sphinx_external_toc",
    # dummy_build_system emits the 'defines-generated' event that
    # run_doxygen listens for, so it is the trigger that runs Doxygen
    "esp_docs.esp_extensions.dummy_build_system",
    "esp_docs.esp_extensions.run_doxygen",
    "sphinx.ext.autodoc",
    # esp-docs bundles blockdiag/seqdiag/nwdiag but not mermaid, which is what
    # the specs' sequence diagrams are written in.
    "sphinxcontrib.mermaid",
]

# The specs use plain ```mermaid fences rather than the directive form, so the
# diagrams also render when the files are read straight from the repository,
# which happens as often as the built site is read.
myst_fence_as_directive = ["mermaid"]

# sphinxcontrib-mermaid defaults every diagram to a fixed 500px-tall viewport,
# which scales tall sequence diagrams down to unreadable sizes. Let each
# diagram render at its natural aspect ratio instead: full column width,
# growing vertically as needed.
mermaid_height = "auto"

# Every spec cross-reference is a plain markdown link -- `[text](page.md#anchor)`
# -- so the specs read correctly browsed in the repository as well as in the
# build. That works because the anchors MyST registers here are GitHub heading
# slugs, and MyST rewrites them to the docutils section ids on the way out; the
# same `#anchor` therefore resolves in both places. Set to 6, not 4: the schedule
# spec targets h5/h6 headings, and a heading past this depth gets no anchor at
# all, which is a fatal xref warning rather than a silent miss.
myst_heading_anchors = 6

# Relative to the source directory, so docs/en/_toc.yml. It is the single
# source of truth for the page tree: every page must appear in it exactly once
# or the build fails, the same guarantee the `toctree` directives used to give.
external_toc_path = "_toc.yml"

# NOTE: do not add sphinx.ext.autosectionlabel. Every Doxygen-generated .inc
# fragment carries "Header File" / "Functions" / "Macros" sections, so labelling
# sections automatically produces hundreds of duplicate-label warnings on the
# c-api-reference pages. Nothing in the specs needs it: markdown links reach
# sections through the heading anchors above.

# Repository the published docs should link into. esp-docs' :project_file:/
# :component:/:example: roles (used by the Doxygen-generated .inc fragments for
# the "Header File" link) join this with '<link_type>/<rev>/<path>'. esp-docs
# >= 2.0 accepts an absolute URL here; the option name is historical.
#
# Must be a repository the reader can actually open: the built HTML is published
# publicly, so these links have to resolve for someone with no special access.
github_repo = "https://github.com/espressif/esp-rainmaker-neo-firmware"

# Keep 'View page source' rather than the theme's 'Edit on GitHub' link. Turning
# it on needs more than flipping this flag: the breadcrumb template builds the
# URL from html_context's own 'github_user' and 'github_repo' -- a bare slug,
# NOT the absolute `github_repo` config value above that the :project_file: role
# uses. Left off until someone wants to set both and verify the resulting URL.
html_context["display_github"] = False

# html_static_path is deliberately NOT set. There is no docs/_static/ to point
# at, and naming a missing directory is a Sphinx warning -- fatal here, since no
# sphinx-known-warnings.txt exists. Add both together if custom CSS is ever
# needed. (The theme's own assets come from esp-docs, not from here.)

# Used by sphinx_idf_theme for the version/doc switcher URLs.
project_slug = "esp-rainmaker-neo-firmware"

# idf_targets is intentionally unset. The C API here is target-agnostic (it also
# builds for POSIX), so docs are built with no target at all -- build-docs
# without -t, which lands the output in _build/<lang>/generic/. Setting
# idf_targets would add a target switcher whose per-target URLs are never built.

languages = ["en"]

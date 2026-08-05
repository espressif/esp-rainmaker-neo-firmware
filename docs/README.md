# Contributing to the ESP RainMaker Neo documentation

`docs/` is the Sphinx root, wrapped by
[esp-docs](https://github.com/espressif/esp-docs). Two toolchains feed it:
the specs are **Markdown → MyST → Sphinx**, the C API reference is
**Doxygen → XML → Breathe → Sphinx**.

```
docs/
  Doxyfile conf_common.py requirements.txt Makefile utils.sh
  en/                     <- the only directory Sphinx reads
    index.rst
    specs/                <- node firmware specification (Markdown, MyST)
    c-api-reference/      <- generated from the component headers by Doxygen
```

esp-docs requires the `<source-dir>/<language>` layout and accepts only `en` or
`zh_CN`, so the language level is not optional; a `zh_CN/` tree can be added later
without moving anything. User-facing guides (setup, build, factory, provisioning)
are not part of this build — they live on the
[ESP RainMaker Neo docs site](https://docs.neo.rainmaker.espressif.com/docs/firmware/).

## Build locally

Needs `doxygen` on `PATH` and Python 3.9–3.12:

```sh
cd docs
pip install -r requirements.txt
make html          # -> _build/en/generic/html/index.html
make fast          # skip the Doxygen API includes, for quick rst iteration
make clean
```

`make` wraps `build-docs`; do not call `sphinx-build` directly — the Doxygen run
and the generated per-header fragments depend on config values only `build-docs`
passes in.

The docs are built with **no target** (`build-docs -l en`, no `-t`), so the output
lands under `_build/en/generic/`. The C API is identical across ESP targets and
POSIX, so a per-target build would only duplicate pages.

## What is committed vs generated

| | Where |
|---|---|
| Committed | `Doxyfile`, `conf_common.py`, `en/conf.py`, `requirements.txt`, `Makefile`, `utils.sh`, all `*.md` and `*.rst` |
| Generated per build, never committed | Doxygen `xml/`, `xml_in/`, `inc/*.inc`, `_build/**` |

### Do not add generated headers to the Doxyfile

Every path in the Doxyfile `INPUT` list must exist in a plain checkout. A header
that only a **firmware** build produces is not there during a docs build, so
Doxygen emits no XML for it and esp-docs' `run_doxygen` dies with
`FileNotFoundError: .../xml/<name>_8h.xml`.

`components/esp_rmaker_neo/include/versioning/esp_rmaker_version.h` is the example: only
the `.h.in` template is committed, and `esp_rmaker_neo/versioning.cmake` renders it into the
*build* tree so a configure never dirties the worktree. Doxygen would also give it
a source link to a path that is not in git.

Its macros are still public API, so `en/c-api-reference/rmng-core.rst`
documents them by hand in a **Version** section. Do the same for any other
generated header: describe it in prose, do not add it to `INPUT`.

## Markdown for the specs, RST for the C API reference

The spec pages are Markdown, parsed by `myst-parser`. The `c-api-reference/`
pages are reStructuredText and **cannot** be Markdown: from a MyST page the
`include-build-file` directive resolves its argument against the source
directory rather than the build directory, so none of the Doxygen `.inc`
fragments are found:

```
CRITICAL: Directive "include-build-file": file not found:
'.../docs/en/c-api-reference/inc/rmaker_ota.inc'
```

Mixing the two formats is otherwise fine, and cross-references resolve in **both**
directions — an RST page can `:doc:`/`:ref:` into a Markdown page, and a Markdown
page reaches an RST one with an ordinary link (see *Editing the specs* below).

The specs are Markdown because they are read straight from the repository as
often as on the built site, and GitHub renders ` ```mermaid ` fences as diagrams
while it renders nothing useful for RST's `.. mermaid::`.

## Editing the specs

- The page tree lives in `en/_toc.yml`, not in `toctree` directives on the pages
  (`sphinx-external-toc`). A new page must be added there, or the build fails
  (every warning is fatal). Nesting in the YAML is what produces nesting in the
  sidebar; the file order is the prev/next order. Do **not** add a `toctree`
  directive to a page — a fenced `` ```{toctree} `` shows as a literal code
  block on GitHub, which is the whole reason the tree was moved out.
- `index.md` pages carry a hand-written bullet list of their children, with a
  sentence of description each. That list is the in-page table of contents and
  has to be updated alongside `_toc.yml`; the RST-only
  `c-api-reference/index.rst` uses `.. tableofcontents::` instead, since nobody
  reads it as source.
- Cross-page links are **plain markdown**: `[text](page.md)` for a whole page and
  `[text](page.md#anchor)` for a section, with the path relative to the linking
  file. Same page, just `[text](#anchor)`. Do **not** use `` {doc}` ` ``,
  `` {ref}` ` `` or `` (label)= `` targets: they render as literal text on
  GitHub, which readers use as often as the built site.
- The `#anchor` is the **GitHub heading slug** — lowercase, punctuation
  dropped, spaces to hyphens, `_` kept, `-1` appended for a repeated heading.
  So `` ### `ch_resp` usage `` is `#ch_resp-usage`. That is what GitHub serves
  *and* what `myst_heading_anchors` registers, so one spelling works in both;
  Sphinx rewrites it to the docutils section id when it builds. Note the built
  page's own id can differ (`#ch-resp-usage`) — link the slug, not the id.
- Renaming a heading breaks every link into it, but the build catches it:
  an unresolved anchor is a fatal `myst.xref_missing` warning.
- The C API pages are RST, so link them with their real extension:
  `[C API Reference](../c-api-reference/index.rst)`. GitHub renders RST too.
- **Links inside a directive body must be markdown too.** A `:ref:` left inside
  a `` ```{list-table} `` body degrades silently to plain text and the build
  stays warning-free — nothing catches it but reading the output.
- Files elsewhere in the repository (`examples/`, `components/`) are linked by
  their **full `https://github.com/espressif/esp-rainmaker-neo-firmware/...` URL**
  on the default branch — `blob/main/` for a file, `tree/main/` for a directory.
  Nothing shorter works: those paths sit outside the Sphinx source tree, so a
  relative link to a `.md` there is a fatal warning, a relative link to a source
  file is silently turned into a *download* link that copies the file into the
  built site, and esp-docs' `` {project_file}` ` `` role renders as raw text
  when the page is read on GitHub. An absolute URL is clickable everywhere, at
  the cost of pointing upstream from a fork.
  Add one only where it earns its place — a spec should specify behaviour, not
  cite the code that implements it. Point at a *guide* the built site does not
  carry, or at a worked example for the task the page teaches.
  (`{project_file}` is still right on the RST pages, which are never read as
  source — `en/index.rst` and `c-api-reference/` use it, so `github_repo` in
  `conf_common.py` stays. It also pins to the build's revision, which an
  absolute URL cannot.)
- Sequence diagrams are plain ` ```mermaid ` fences, not the directive form, so
  they render in the repository view as well; `myst_fence_as_directive` turns them into real
  diagrams in the build. The rendered page pulls `mermaid.js` from a CDN.
- Payload examples that use `<placeholder>` values or `//` comments are tagged
  ` ```javascript `, not ` ```json `: pygments' JSON lexer is strict and errors
  on them ("Could not lex literal_block"), which fails the build. Genuine JSON
  keeps the `json` tag.
- Tables are plain GFM pipe tables, or `` ```{list-table} `` where a cell needs
  markup a pipe table cannot hold. Do **not** hand-write HTML in
  `` ```{raw} html `` blocks: those fences show as literal tags on GitHub, and a
  bare `<table>` misses the theme's `table.docutils` styling, so it renders
  unbordered and unpadded in the build.
  A cell can never hold a fenced code block. When a row needs a multi-line
  payload, that is the signal the table is the wrong shape — give each entry its
  own heading with the payload beneath it, as `networking/cloud_communication.md`
  does for the `get`/`set` events. That reads correctly in both places, keeps the
  payload at full page width instead of crushed into a column, and gives every
  entry its own anchor.

## Adding a header to the C API reference

Two edits, and the names must match:

1. Add `$(PROJECT_PATH)/components/.../my_header.h \` to the `INPUT` block in
   `Doxyfile`. Constraints on that block are documented in the file itself —
   read them first (no globs, no blank lines, `$(PROJECT_PATH)` prefix required,
   basenames unique across the whole list).
2. Add `.. include-build-file:: inc/my_header.inc` under a heading on the
   relevant page in `en/c-api-reference/`.

A header in `INPUT` with no `include-build-file` is simply not published (Breathe
emits only what a page asks for). An `include-build-file` with no matching
`INPUT` entry fails the build.

### Per-platform headers

If you ever document a header that exists in ESP-IDF and POSIX variants sharing a
basename, put only one in `INPUT` — documenting both gives Breathe duplicate
targets for the same symbols — and describe the platform difference in prose on
the page. esp-docs' `idf_targets`/`:only:` mechanism does not help: it models ESP
targets and has no notion of a POSIX target.

## Warnings

`build-docs` treats any Doxygen or Sphinx warning that is not listed in
`doxygen-known-warnings.txt` / `sphinx-known-warnings.txt` (this directory) as
fatal. **The tree builds warning-free**, so neither file exists — keep it that
way rather than adding one.

`WARN_NO_PARAMDOC=YES` is on, so a `@param` naming an argument that does not
exist (or a missing one) fails the build. Two classes of warning are handled in
`Doxyfile` via `PREDEFINED` instead of in the headers: GCC `__attribute__`
expressions, which Sphinx' C++ parser rejects, and `OSAL_EVENT_DECLARE_BASE`,
whose file-scope invocations Doxygen records as an ambiguous function.

Because `OSAL_EVENT_DECLARE_BASE(x)` expands to nothing, the event bases it
declares (`RMAKER_EVENT`, `RMAKER_COMMON_EVENT`, …) are not symbols Doxygen knows
about. Refer to them in docblocks as ``` ``RMAKER_EVENT`` ``` — a `::RMAKER_EVENT`
is an *explicit link request*, and it will fail the build with "explicit link
request to … could not be resolved". The same applies to any symbol that is in
`INPUT` but has no `include-build-file`, and so is not published.

When the check does trip, the HTML is still built; read
`_build/en/generic/{doxygen,sphinx}-warning-log.txt`.

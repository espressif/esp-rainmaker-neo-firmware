# Shared CI sdkconfig variants

One file per CI build variant that applies to **every** example, instead of an identical copy
in each example directory.

Each `sdkconfig.ci.<name>` here becomes one extra CI build of every example, layered on that
example's own `sdkconfig.defaults`. The variant name in the file name is the config name that
appears in CI job output and in the size reports.

`sdkconfig.defaults` is deliberately **not** shared: it is per-example project configuration,
it is regenerated in place by `idf.py save-defconfig`, and the QA firmware pipeline
(`.jenkins/`) edits an example's copy directly to apply custom build options.

An example that needs a variant no other example has keeps it as its own
`sdkconfig.ci.<variant>` next to its `sdkconfig.defaults` — see
`examples/advanced/ota-custom`.

## Adding a shared variant

1. Add `sdkconfig.ci.<name>` here.
2. Add a `--config-rules` line for it in `.gitlab/examples.yml` (ESP-IDF) **at every example
   nesting depth** — `../common/ci-sdkconfig/...` for `examples/<app>` and
   `../../common/ci-sdkconfig/...` for a grouped one like `examples/advanced/<app>`.

The POSIX side (`build_app_posix()` in `.gitlab/config.yml`) globs this directory by absolute
path, so it is depth-independent and needs no change.

Step 2 cannot be replaced by a wildcard rule. `idf-build-apps` globs the pattern relative to
each app directory, so `../common/ci-sdkconfig/...` does find the file — but it derives a
wildcard's config name by regex-matching the pattern against the *resolved* path, in which
`../` has already been normalised away. A wildcard rule that crosses `..` therefore fails an
internal assertion and aborts the build. Spelling the config name out avoids the derivation
entirely.

The same relative globbing is why the rules are depth-specific. Passing both forms is safe:
for any given app exactly one resolves to an existing file and the other matches nothing, so
the repeated config names never collide.

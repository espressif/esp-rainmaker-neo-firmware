# Shared CI sdkconfig variants (Matter examples)

The Matter counterpart of [`examples/common/ci-sdkconfig`](../../../common/ci-sdkconfig/README.md):
one file per CI build variant that applies to **every** example under `examples/matter/`,
instead of an identical copy in each example directory.

Each `sdkconfig.ci.<name>` here becomes one extra CI build of every Matter example, layered on
that example's own `sdkconfig.defaults`. The variant name in the file name is the config name
that appears in CI job output and in the size reports.

| Variant | What it covers |
| --- | --- |
| `sdkconfig.ci.onnetwork` | On-network onboarding instead of the default Matter-first mode |

## Adding a variant

1. Add `sdkconfig.ci.<name>` here.
2. Add a `--config-rules` line for it to `build_examples_matter` in `.gitlab/matter.yml`,
   naming the config explicitly:
   `"../common/ci-sdkconfig/sdkconfig.ci.<name>=<name>"`.

Step 2 cannot be replaced by a wildcard rule. `idf-build-apps` globs the pattern relative to
each app directory, so `../common/ci-sdkconfig/...` does find the file — but it derives a
wildcard's config name by regex-matching the pattern against the *resolved* path, in which
`../` has already been normalised away. A wildcard rule that crosses `..` therefore fails an
internal assertion and aborts the build.

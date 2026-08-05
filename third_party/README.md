# Third Party Sources

Upstream sources the SDK builds against but does **not** vendor. Only the CMake
glue in this directory is committed; every source tree listed below is fetched
at configure time with `FetchContent` and is ignored by git (see
[`.gitignore`](.gitignore)).

Nothing here is relicensed — each fetched tree keeps its own upstream license,
which is the authoritative text for that dependency.

## Dependencies

| Dependency | Upstream | License | Ref pinned in | Fetched for |
|---|---|---|---|---|
| [Unity](https://github.com/ThrowTheSwitch/Unity) | `ThrowTheSwitch/Unity` | MIT | `RMNG_THIRD_PARTY_UNITY_TAG` (v2.6.1) | POSIX only (ESP-IDF uses its own) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | `Mbed-TLS/mbedtls` | Apache-2.0 **OR** GPL-2.0-or-later (dual; pick one) | `RMNG_THIRD_PARTY_MBEDTLS_TAG` (v4.1.0) | POSIX only (ESP-IDF uses its own) |
| [protobuf-c](https://github.com/protobuf-c/protobuf-c) | `protobuf-c/protobuf-c` | BSD-2-Clause | `RMNG_THIRD_PARTY_PROTOBUF_C_TAG` (v1.5.2) | POSIX only (ESP-IDF uses its own) |
| [esp-aws-iot](https://github.com/espressif/esp-aws-iot) | `espressif/esp-aws-iot` | Apache-2.0 (its library submodules are MIT) | `RMNG_ESP_AWS_IOT_GIT_REF` in [`cmake/esp_aws_iot.cmake`](../cmake/esp_aws_iot.cmake) | ESP-IDF and POSIX |

`esp-aws-iot` carries 11 library submodules. Only these six are fetched — all MIT-licensed:

- `backoffAlgorithm`
- `coreMQTT`
- `coreMQTT-Agent`
- `coreJSON`
- `Jobs-for-AWS-IoT-embedded-sdk`
- `aws-iot-core-mqtt-file-streams-embedded-c`

## How fetching works

Each install script defines one `*_make_available()` function that a consumer
`include()`s and calls; it is idempotent (returns early if the target already
exists), so there is no ordering dependency between consumers.

- **Existing checkouts are reused.** If a source directory is already a git
  working tree with a resolvable `HEAD`
  ([`rmng_git_working_tree_current_commit()`](../cmake/fetch_helpers.cmake)), it is
  used as-is and never re-fetched. Dropping a local fork or patched checkout
  into place therefore just works. The exception is `esp-aws-iot`: when its ref
  is pinned to a full 40-character SHA, a tree sitting on another commit *is*
  re-fetched. A branch or tag is a moving ref, so an existing tree is left alone
  and must be advanced by hand.
- **Parallel configures are serialized.** Source directories live in the repo
  tree and are shared between concurrent configure processes, so populate is guarded
  by a `.rmng-fc-<name>.lock` file next to each source directory.
- **First configure needs network access**, including for submodules. Prime a cache
  or offline mirror by cloning the pinned refs into the directories above before configuring.
- **Mbed TLS needs a virtualenv Python.** `mbedtls_make_available()` hard-fails
  unless the `Python3` CMake finds is a venv interpreter (PEP 405), then
  installs Mbed TLS' `scripts/basic.requirements.txt` into it. Activate a venv,
  or pass `-DPython3_EXECUTABLE=...`, before configuring a POSIX build.

## Re-pinning and overrides

To force any dependency to be re-fetched, remove its directory and reconfigure.

## Distribution note

The SDK itself is [Apache-2.0](../LICENSE). Shipping a binary built from it also
carries the obligations of the dependencies above — attribution for the MIT/BSD
components, and a license election for Mbed TLS' Apache-2.0 / GPL-2.0-or-later
dual license. Because the trees are fetched rather than vendored, collect the
notices from the fetched checkouts (each has a `LICENSE` / `LICENSE.txt`) at
release time.

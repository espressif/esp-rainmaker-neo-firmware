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
| [Unity](https://github.com/ThrowTheSwitch/Unity) | `ThrowTheSwitch/Unity` | MIT | `RMNG_THIRD_PARTY_UNITY_URL` (v2.6.1) | POSIX only (ESP-IDF uses its own) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | `Mbed-TLS/mbedtls` | Apache-2.0 **OR** GPL-2.0-or-later (dual; pick one) | `RMNG_THIRD_PARTY_MBEDTLS_URL` (v4.1.0) | POSIX only (ESP-IDF uses its own) |
| [protobuf-c](https://github.com/protobuf-c/protobuf-c) | `protobuf-c/protobuf-c` | BSD-2-Clause | `RMNG_THIRD_PARTY_PROTOBUF_C_URL` (v1.5.2) | POSIX only (ESP-IDF uses its own) |
| [esp-aws-iot](https://github.com/espressif/esp-aws-iot) | `espressif/esp-aws-iot` | Apache-2.0 (its library submodules are MIT) | `RMNG_THIRD_PARTY_ESP_AWS_IOT_URL` (untagged master commit) | ESP-IDF and POSIX |

`esp-aws-iot` carries 11 library submodules. Only these six are fetched — all MIT-licensed.
Each one is a separate archive at the commit that the `esp-aws-iot` commit records:

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

- **Release archives, not git clones.** Unity, Mbed TLS, protobuf-c, curl and
  civetweb download a pinned release archive and check its SHA-256. The Mbed TLS
  archive already contains its `framework` and `tf-psa-crypto` submodules.
  `esp-aws-iot` has no release that contains the pinned commit, so it downloads
  the GitHub archive of that commit and one archive for each library submodule.
- **Only a stamped tree is reused.** An archive fetch writes a `.rmng-fc-stamp` file
  with the archive SHA-256 into the source directory
  ([`rmng_fetchcontent_source_ready()`](../cmake/fetch_helpers.cmake)). A directory with a
  matching stamp is used as-is and never re-fetched. Any other directory is deleted
  and fetched again, so an interrupted fetch repairs itself.
- **Git checkouts are not used.** A clean git clone at the pinned release commit
  (`*_COMMIT`) is replaced by the archive. Any other git checkout stops the configure
  ([`rmng_fetchcontent_guard_checkout()`](../cmake/fetch_helpers.cmake)), so local work is never
  deleted. Move or delete that directory, then configure again.
- **Parallel configures are serialized.** Source directories live in the repo
  tree and are shared between concurrent configure processes, so populate is guarded
  by a `.rmng-fc-<name>.lock` file next to each source directory.
- **First configure needs network access.** To work offline, extract the pinned
  archives into the directories above and write each `.rmng-fc-stamp`.
- **Mbed TLS needs a virtualenv Python.** `mbedtls_make_available()` hard-fails
  unless the `Python3` CMake finds is a venv interpreter (PEP 405), then
  installs Mbed TLS' `scripts/basic.requirements.txt` into it. Activate a venv,
  or pass `-DPython3_EXECUTABLE=...`, before configuring a POSIX build.

## Re-pinning and overrides

To force any dependency to be re-fetched, remove its directory and reconfigure.

All pins are in [`third_party_versions.cmake`](third_party_versions.cmake). The
`esp-aws-iot` block holds seven URL and SHA-256 pairs that must change together.
Do not edit them by hand. Run
[`esp_aws_iot_pins.py --update <ref>`](esp_aws_iot_pins.py) to re-pin them, and
`esp_aws_iot_pins.py --check` to verify them. Both need network access to GitHub.
A pre-commit hook runs `--check` when the pins or the script change, and in every
CI pipeline.

GitHub keeps the contents of a commit archive stable, but not its bytes. If
GitHub changes the archive compression, all seven SHA-256 pins fail together. Run
`esp_aws_iot_pins.py --update <pinned commit>` to refresh the hashes.

## Distribution note

The SDK itself is [Apache-2.0](../LICENSE). Shipping a binary built from it also
carries the obligations of the dependencies above — attribution for the MIT/BSD
components, and a license election for Mbed TLS' Apache-2.0 / GPL-2.0-or-later
dual license. Because the trees are fetched rather than vendored, collect the
notices from the fetched checkouts (each has a `LICENSE` / `LICENSE.txt`) at
release time.

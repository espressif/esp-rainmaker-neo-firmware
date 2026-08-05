# esp_rmaker_neo_ota

Optional OTA support for the ESP RainMaker Neo SDK. Job control is **AWS IoT
Jobs**; the image download uses **MQTT file streams**. Both build on AWS
embedded SDK sources taken from one esp-aws-iot checkout (see
[dependency sources](#dependency-sources)). Node-side behaviour, the job
document and status details are specified in
[`docs/en/specs/ota/`](../../docs/en/specs/ota/index.md); for running an update
end to end see the
[OTA guide](https://docs.neo.rainmaker.espressif.com/docs/firmware/features/ota).

## Enabling it

Optional means "not linked unless you ask for it" — there is no master Kconfig
switch. Add the component as a dependency, then call `esp_rmaker_ota_enable()`:

- **ESP-IDF**: add `esp_rmaker_neo_ota` to your main component's `idf_component.yml` and
  `PRIV_REQUIRES` (see [`examples/switch/`](../../examples/switch/)).
- **POSIX**: `add_subdirectory(components/esp_rmaker_neo_ota)` **before** the
  `examples/common/app_*` components, and link `esp_rmaker_neo_ota`. That subdirectory also
  re-exports `RMNG_OTA_POSIX_BOOTLOADER_NAME`, `RMNG_OTA_POSIX_PARTITION_FOLDER`
  and `RMNG_OTA_POSIX_DEFAULT_FIRST_PARTITION`, which examples use to name the
  mock-bootloader and OTA-slot executables.

`esp_rmaker_ota_enable()` does not start the engine directly: it queues the real
enable onto the `esp_rmaker_neo_common` work queue, so OTA comes up when
`esp_rmaker_start()` runs the queue.

## Public API (`include/`)

- `esp_rmaker_ota.h` — `esp_rmaker_ota_config_t` (OTA callback, diagnostics callback,
  image-reference validator, optional custom-job and custom-filetype hooks),
  `esp_rmaker_ota_enable()` / `..._disable()`, `..._fetch[_with_delay]()`,
  `..._report_status()`, `..._request_recovery()`, `..._mark_valid()` /
  `..._mark_invalid()`, and the per-transport `esp_rmaker_ota_mqtt_cb()`.
  Leaving `ota_cb` NULL selects `esp_rmaker_ota_default_cb()` plus the matching
  built-in validator.
- `esp_rmaker_ota_filetype_handler.h` — the filetype-handler contract
  (`esp_rmaker_ota_ft_ctx_t`): versioning, download begin/resume/chunk/complete,
  SHA-256 and optional MD5, image-header verification, integration check,
  post-download and post-reboot hooks, plus
  `esp_rmaker_ota_report_final_status()`. Custom filetypes plug in via
  `custom_filetype_handler_lookup`
  ([custom filetypes](../../docs/en/specs/ota/custom_filetypes.md)).
- `esp_rmaker_ota_event_loop.h`, `esp_rmaker_ota_status_details.h`,
  `esp_rmaker_ota_error_reasons.h` — the `RMAKER_OTA_EVENT` base and its events, the
  status details reported to the cloud, and the reasons carried by
  `RMAKER_OTA_EVENT_ERROR_OCCURRED`.

## Versioning and image artifacts

The version string reported by the firmware must **increase** relative to what
is already running, or the update may be rejected. Set `PROJECT_VER` in the
project's `CMakeLists.txt` **before** the top-level `project()` call (in-tree
examples default to `1.0.0`); on ESP-IDF, `CONFIG_APP_PROJECT_VER_FROM_CONFIG=y`
plus `CONFIG_APP_PROJECT_VER` drives it from sdkconfig instead.

Which file to upload for an update, relative to the project's `build/`
directory:

| Platform | Artifact |
|----------|----------|
| **ESP-IDF** | `<project_name>.bin` — same basename as in `project(<name>)` in the root `CMakeLists.txt`. |
| **POSIX** | `partitions/ota_0` — the OTA slot image, produced only when `esp_rmaker_neo_ota` is linked. |

## Relationship to `osal/ota`

The split is deliberate. [`osal/ota`](../osal/ota/README.md) abstracts **app-image
partitions and boot slots** — write path, partition queries, boot-slot selection,
rollback marking — and nothing above that. `esp_rmaker_neo_ota` is the protocol and
filetype-handler layer on top: the Jobs state machine, the transport, and the
built-in firmware filetype handler that maps the handler contract onto those
`osal_ota_*` calls. A non-firmware filetype brings its own handler and never
touches `osal/ota`.

## Image signature verification

`CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE` (**default `n`**) checks the base64
`file_signature` from the job document against the
SHA-256 the filetype handler computed over the downloaded data, as a
post-download step before the integration check. The verifying key is the
**`codesign_cert`** credential read via `esp_rmaker_neo_common`, so that certificate must
be in the factory partition — generate it with a `codesign_cert` path
([factory NVS tool](../../tools/factory_nvs_gen/README.md)). With the
option on, its absence fails OTA initialisation outright, and a job carrying no
signature is rejected rather than attempted.

> [!IMPORTANT]
> Turn it on only once your OTA jobs carry a signature.
> Until then a downloaded image is authenticated only by transport TLS and its
> `file_md5`, neither of which proves who built it.

Independently, the job's optional
`file_md5` is checked end-to-end on completion and gates
`CONFIG_RMNG_OTA_RESUME` (default `y`, best-effort: a failed resume falls back to
a full download).

## Per-platform notes

- **ESP-IDF**: `CONFIG_RMNG_OTA_FORCE_ENABLE_ROLLBACK` (default `y`) selects
  `BOOTLOADER_APP_ROLLBACK_ENABLE` so the `ota_diag` callback can run before and
  after MQTT connect; `CONFIG_RMNG_OTA_ROLLBACK_WAIT_PERIOD` bounds that wait.
  CBOR comes from the managed `espressif/cbor` component.
- **POSIX**: OTA runs against `osal/ota`'s file-backed slots and mock bootloader,
  so the whole flow is exercisable on a host; tinyCBOR sources are imported from
  the component registry instead.

## Dependency sources

The AWS `Jobs-for-AWS-IoT-embedded-sdk`, `coreJSON` and
`aws-iot-core-mqtt-file-streams-embedded-c` sources are not vendored per
library. Both platforms compile them out of one esp-aws-iot checkout that
`cmake/esp_aws_iot.cmake` fetches with `FetchContent` (submodules included)
into `third_party/esp-aws-iot`, which is **not committed**. `deps_make_available.cmake`
just points at `libraries/<lib>/<lib>` inside it.

The ref is `RMNG_ESP_AWS_IOT_GIT_REF` (cache variable, default a pinned commit
SHA); `RMNG_ESP_AWS_IOT_REPOSITORY` overrides the remote. A full SHA is enforced
on every configure — a checkout on another commit is re-fetched — while a
branch/tag is treated as a moving ref and an existing tree is left alone, so
advance it by hand.

## Other options and tests

`CONFIG_RMNG_OTA_MQTT_BLOCK_SIZE` and
`..._BLOCKS_PER_REQUEST` (bounded by the MQTT in-buffer and the work-queue size
respectively), `CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT` (default `n`),
`CONFIG_RMNG_OTA_TIME_SUPPORT`, `CONFIG_RMNG_OTA_DISABLE_AUTO_REBOOT`.
`test_rmaker_neo_ota/` builds as the `rmng-ota-tests` ctest target on POSIX (when
`TEST_RMNG_OTA=ON`) and as an ESP-IDF test component; coverage targets are
`rmng_ota_coverage_test` then `rmng_ota_coverage_gen`.

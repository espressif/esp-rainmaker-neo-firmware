# esp_rmaker_neo_common

Shared utilities sitting between the SDK components and the OS abstraction
layer. Both `esp_rmaker_neo` and `esp_rmaker_neo_ota` need MQTT plumbing, credentials, a work queue
and one common error type; keeping them here means `osal` stays free of
RainMaker concepts and the two upper components agree on a single
implementation instead of each carrying its own.

The dependency direction is one-way: examples → `esp_rmaker_neo` / `esp_rmaker_neo_ota` →
`esp_rmaker_neo_common` → `osal`. Nothing here may depend on `esp_rmaker_neo`. Applications rarely
link it directly — it arrives transitively (in POSIX builds `esp_rmaker_neo`'s
`CMakeLists.txt` is what adds the `esp_rmaker_neo_common` subdirectory, which in turn adds
`osal`).

## What lives here

| Public header (`include/`) | Provides |
|---|---|
| `esp_rmaker_error_types.h` | `esp_rmaker_error_t`, a typedef of `osal_err_t`, plus `ESP_RMAKER_*` aliases for the `OSAL_ERR_*` codes. Not a separate error space. |
| `esp_rmaker_credentials.h`, `esp_rmaker_credentials_provider.h`, `esp_rmaker_factory_part.h` | Credential reads and the provider-override mechanism (below). |
| `esp_rmaker_mqtt_impl.h`, `esp_rmaker_mqtt_channels.h` | The shared `esp_rmaker_mqtt_impl` (an `osal_mqtt_impl_t`) and the `mqtt_channel_main_t` channel IDs, so dependents don't collide on channel numbers. |
| `esp_rmaker_work_queue.h` | Work queue: `esp_rmaker_work_queue_add_task()` runs a function in a dedicated task's context. |
| `esp_rmaker_runtime_gate.h` | `esp_rmaker_should_do_work()` — one advisory atomic flag telling deferrable background work to stand down while the SDK is stopping/resetting. |
| `esp_rmaker_common_events.h` | The `RMAKER_COMMON_EVENT` event base and its IDs (reboot/network-reset/factory-reset, MQTT connect/publish/subscribe completions, timezone changes). |
| `retry/esp_rmaker_backoff.h` | Exponential backoff with random jitter for retried `osal_scheduler` tasks. |
| `util/esp_rmaker_nvs.h`, `util/esp_rmaker_crypto.h`, `util/esp_rmaker_convert_hex.h`, `util/esp_rmaker_convert_base64.h` | Typed NVS get/update helpers over `osal_storage`; SHA-256, signing and key-format helpers over mbedTLS; hex and base64 conversion. |

Two things are declared here but **owned at runtime by `esp_rmaker_neo`**:
`esp_rmaker_mqtt_impl` starts zeroed and is populated during RMNG init, and the
work queue is initialised at node init but only starts in `esp_rmaker_start()`
— tasks queued earlier run once it starts. Calling through either before that
point is a mistake dependents make.

## Credentials

`esp_rmaker_credentials_get_*()` is the read path. Every read goes through a
table of provider function pointers whose defaults read the factory NVS
partition (`CONFIG_ESP_RMAKER_FACTORY_PARTITION_NAME`, default `fctry`, in
namespace `CONFIG_ESP_RMAKER_FACTORY_NAMESPACE`, default `rmaker_creds`). The
partition handle is opened lazily on first read, so calling
`esp_rmaker_factory_part_init()` up front is optional. See
[factory NVS generation](../../tools/factory_nvs_gen/README.md) for generating
that partition.

`esp_rmaker_credentials_override()` replaces **only** the providers you set to
non-NULL, so a partial override (say just `client_key`) leaves everything else
reading from the factory partition;
`esp_rmaker_credentials_reset_to_default()` restores all of them. Provider
implementations must return heap-allocated data — the caller releases it with
`esp_rmaker_credentials_free_credential()` or `free()`.

Certificates and keys are validated as they are loaded: the certificate is
parsed with mbedTLS, and a raw 32-byte NIST P-256 key is converted to DER
first. The validation can be skipped with
`CONFIG_RMAKER_MQTT_SKIP_CREDENTIALS_CHECK`, which is only selectable when
`CONFIG_RMNG_CUSTOM_MQTT_CLIENT_PROVIDER` (owned by `esp_rmaker_neo`) is set.
`esp_rmaker_credentials_get_mqtt_conn_params()` assembles the osal MQTT
connection parameters; the port comes from the `ESP_RMAKER_MQTT_PORT` choice,
which defaults to 443 and then also requests the `x-amzn-mqtt-ca` ALPN protocol
(8883 requests none).

## Tests

`test_rmaker_neo_common/` doubles as a POSIX ctest executable (`rmng-common-tests`,
built when testing is enabled) and an ESP-IDF test component, both driven from
[`test/apps/`](../../test/apps/). Coverage targets on POSIX:
`rmng_common_coverage_test` then `rmng_common_coverage_gen`, report in
`build/rmng-common-coverage/`.

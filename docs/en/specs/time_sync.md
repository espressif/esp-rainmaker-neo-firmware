# Time Synchronization

Time synchronization is required for accurate scheduling and timeseries data timestamps. On ESP-IDF the node runs an SNTP (Simple Network Time Protocol) client. On POSIX the SDK runs **no** time client at all: the host OS owns the clock, and initialization only records whether that clock already looks valid.

## What "synchronised" means

Throughout the firmware, "time is synced" is a **wall-clock validity** test, not an SNTP success flag: the clock counts as valid once it reads later than a reference instant baked in at compile time. Consequences:

- A clock set by an external SNTP owner, by the host OS, or by the cloud `getTimeSync` response counts as synced — the SDK does not have to own SNTP.
- Any attempt to set the clock to a value at or before the build-time reference is rejected, so garbage values cannot make the clock "valid".

## Synchronization Flows

Startup behavior depends on whether mbedTLS validates the server certificate's validity dates (`CONFIG_MBEDTLS_HAVE_TIME_DATE`):

- **Synchronous flow** (`CONFIG_MBEDTLS_HAVE_TIME_DATE` set): a valid wall clock is a hard prerequisite for the MQTT TLS handshake, so startup blocks (indefinitely, polling every 2 s) before the MQTT connection until the clock syncs. The cloud `getTimeSync` coarse-set cannot help here — it arrives over MQTT, which cannot connect without a valid clock. Schedules arm inline once time is valid.
- **Decoupled flow** (`CONFIG_MBEDTLS_HAVE_TIME_DATE` unset): startup never blocks on time. MQTT and all non-wall-clock work proceed immediately. A flat-cadence poll (every `CONFIG_RMNG_TIME_SYNC_POLL_INTERVAL_S` seconds, default 2, no backoff and no jitter) observes synchronization later and arms deferred schedules then; wall-clock-sensitive work (timeseries timestamps, schedule arming) is deferred or dropped until the clock is valid. The poll stops as soon as it observes a valid clock.

Both flows poll regardless of whether this SDK initialized SNTP (`enable_time_sync`) — that flag only decides SNTP ownership, not whether the clock can become valid.

## Coarse Time from Cloud

- On the post-connect cloud handshake the node requests `getTimeSync` **only when the clock is not yet valid** at handshake time. In practice this is the decoupled flow — the synchronous flow already has a valid clock by the time it connects.
- The response carries server time (epoch ms) and is applied only if the system time is still not valid; SNTP remains authoritative and steps the clock when it eventually syncs.
- Accuracy is bounded by cloud-to-node delivery latency. See [Cloud Communication](networking/cloud_communication.md).

## SNTP Configuration (ESP-IDF)

- **Default Servers**: when no server is supplied, four servers are configured in order: `time.google.com`, `time.cloudflare.com`, `pool.ntp.org`, `time.windows.com`. Supplying a server name in the timesync configuration replaces the list with that single server.
- **Operating Mode**: polling mode (`SNTP_OPMODE_POLL`) with immediate step (`SNTP_SYNC_MODE_IMMED`) — the clock jumps rather than being slewed.
- **Sync Notification**: an SNTP notification callback is invoked whenever time is synchronized. The SDK installs a logging callback unless the application supplies its own.
- **Timezone Support**: the timezone is loaded from NVS (or the configured default) during timesync init, independently of whether the clock is valid — see [Timezone Service](services/optional.md#timezone).

## POSIX

No time client is started. `osal_timesync_init()` records whether the OS clock already reads as valid and initializes the timezone; keeping the clock correct is the host's responsibility.

## Time Sync Failure Handling

- **Retry**: on ESP-IDF, SNTP automatically retries at its configured interval
- **Synchronous flow**: startup blocks until sync succeeds (a valid clock is required for the TLS handshake)
- **Decoupled flow**: startup is never blocked; time-sensitive work waits for a valid clock
- **Fallback**: If time sync is disabled, the node proceeds without synchronized time (may affect schedule accuracy); the clock may still be set by an external SNTP owner or the cloud `getTimeSync` response

## Relationship to Services

- **Schedules**: require a valid clock. Schedule arming is deferred until the clock is valid; the details themselves are loaded from NVS regardless.
- **Automation**: trigger evaluation is purely value-based and needs no clock.
- **Timeseries**: timestamps are included in timeseries data points, and points stamped before the clock became valid are dropped rather than published.
- **Timezone Service**: setting `TZ`/`TZ-POSIX` works with or without a valid clock; the timezone only affects how an existing clock reading is interpreted.

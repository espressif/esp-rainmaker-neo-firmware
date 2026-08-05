# app_network

Network provisioning and bring-up for the examples, on **both** platforms.

An example calls `app_network_provision()` unconditionally and gets:

- **ESP-IDF** — Wi-Fi/Thread provisioning via the `espressif/rmaker_app_network` component,
  wrapped around the RMNG pre-provisioning lifecycle. One call covers the whole sequence:
  `esp_rmaker_pre_prov_init()` → `app_network_set_custom_mfg_data_neo()` →
  `app_network_start_neo()` → `esp_rmaker_pre_prov_deinit()`.
- **POSIX** — there is no provisioning and no stored credentials on the host, so the calls
  are successful no-ops.

Everything is declared in `include/app_network_neo.h`. The portable surface
(`app_network_provision()`, `app_network_reset_credentials()`,
`app_network_set_prov_hooks()`, the `NEO_MFG_DATA_*` device types) exists on both platforms;
`app_network_set_custom_mfg_data_neo()` and `app_network_start_neo()` are ESP-only, because
there is no BLE advertisement or provisioning session to drive on the host.

Pass `NEO_MFG_DATA_DEVICE_TYPE_NONE` to skip the manufacturer-data step, i.e. advertise no
device type/subtype for phone-app filtering.

## Provisioning-lifecycle hooks

`app_network_set_prov_hooks()` lets an example drive user feedback (typically an LED
animation) around the otherwise-opaque provisioning call, without touching any ESP-only
provisioning event: `on_begin` before the network starts, `on_session_start` when a phone app
connects, `on_end` when provisioning finishes either way. All are optional. They never fire on
POSIX, where the setter is a no-op.

## Connecting to IPv4 Servers on Thread

Thread networks are IPv6-only, hence **DNS64 is required** to connect to IPv4 servers:
- The Kconfig option `APP_NETWORK_OVER_THREAD_IPV4_NETWORKING` attempts to configure OpenThread's DNS64 client for this purpose when selected.
- This option might **fail to configure DNS64 properly if selected via `idf.py menuconfig`**, in which case users will have to manually ensure the following are set:
    - `OPENTHREAD_DNS64_CLIENT=y`
    - `LWIP_HOOK_DNS_EXT_RESOLVE_CUSTOM=y`

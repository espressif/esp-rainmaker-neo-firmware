# Changes

## 20-Nov-2026: Create the node before provisioning

**IMPORTANT BREAKING CHANGE**: Call `esp_rmaker_node_init()` before network provisioning starts. The
build fails on the removed functions, and `app_network_provision()` fails at runtime on the old order.

Provisioning creates its endpoints from the endpoints that are registered when it starts. In earlier
versions, the application provisioned the network first and created the node after it. In that
order, the provisioning session now has no `ch_resp` endpoint, so the RainMaker Neo phone app cannot
associate the node, and provisioning does not end. This failure occurs only on a node that has no
network credentials, for example on the first boot, or after a network reset or a factory reset.

| API | Old behavior | New behavior |
|---|---|---|
| `esp_rmaker_pre_prov_init()` | Opened the factory partition and registered the provisioning endpoints | Removed. A call fails the build. |
| `esp_rmaker_pre_prov_deinit()` | Waited for the claim, then released the provisioning components | Removed. A call fails the build. |
| `esp_rmaker_node_init()` | Required the cloud credentials, so it ran after provisioning | Registers `ch_resp` and, on an unclaimed node, `rmaker_claim`. Succeeds without the cloud credentials. |
| `esp_rmaker_start()` | Read the credentials that `esp_rmaker_node_init()` loaded | The start task waits for the claim, then reads the credentials and connects. On a node with assisted claiming, call it after provisioning ends. |
| `app_network_provision()` (`examples/common/app_network`) | Called `esp_rmaker_pre_prov_init()` and `esp_rmaker_pre_prov_deinit()` | Returns `OSAL_ERR_INVALID_STATE` if `esp_rmaker_node_init()` was not called. |
| `esp_rmaker_local_ctrl_service_enable()`, `esp_rmaker_chal_resp_service_enable()` | Required the node ID, so they ran after `esp_rmaker_node_init()` | Called before the start task starts the local endpoints, the request is recorded and the start task starts the service. |

To migrate an application:

1. Remove the calls to `esp_rmaker_pre_prov_init()` and `esp_rmaker_pre_prov_deinit()`.
2. Call `esp_rmaker_node_init()`, create the devices and enable the services.
3. Start the network and provisioning, for example with `app_network_provision()`.
4. Call `esp_rmaker_start()`.

An application with its own copy of `app_network` that does not call the removed functions gets no
build error. Examine the order of these calls in each application.

#### Old

```c
app_network_provision(device_type, device_subtype); /* blocks until provisioning ends */
esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "Light", "light");
/* Create the devices and enable the services. */
esp_rmaker_start();
```

#### New

```c
esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "Light", "light");
/* Create the devices and enable the services. */
app_network_provision(device_type, device_subtype); /* blocks until provisioning ends */
esp_rmaker_start();
```

The node is up during provisioning, so the free heap during provisioning is lower. Measured with
the light example, BLE provisioning and assisted claiming:

| ESP32-C3, free internal heap | Old order | New order | Change |
|---|---|---|---|
| Minimum while BLE advertises, before the phone connects | 99,288 B | 87,516 B | -11.8 KB |
| Minimum during the provisioning session, at the start of the claim | 89,920 B | 78,148 B | -11.8 KB |
| After the MQTT connection | 116,112 B | 114,720 B | -1.4 KB |

| ESP32-C2, free internal heap | Old order | New order | Change |
|---|---|---|---|
| Minimum while BLE advertises, before the phone connects | 22,204 B | 13,464 B | -8.5 KB |
| Minimum during the provisioning session, at the start of the claim | 16,288 B | 7,716 B | -8.6 KB |
| Largest free block at the session minimum | 22,016 B | 10,240 B | -11.5 KB |
| Minimum over the whole boot | 9,716 B (MQTT TLS connect) | 7,716 B (claim) | -2.0 KB |
| After the MQTT connection | 38,216 B | 40,672 B | +2.4 KB |

On ESP32-C2, the minimum over the whole boot is only 2.0 KB lower, but it now occurs during the
provisioning session, not at the MQTT connection. An allocation failure at that time stops the
provisioning. Check the heap margin of the application during provisioning before you update. The
largest item is the timeseries data queue (4.0 KB at the default length of 100). To make it smaller,
set `CONFIG_ESP_RMAKER_NEO_TIMESERIES_DATA_QUEUE_LENGTH`.

See [Network Provisioning](docs/en/specs/provisioning.md#migration-from-the-pre-provisioning-order)
and [Initialization](docs/en/specs/initialization.md) for the new sequence.

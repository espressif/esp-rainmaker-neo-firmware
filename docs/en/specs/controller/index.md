# Group Controller

A controller is a normal RainMaker Neo node that the cloud registers with the `controller` capability. Scoped to the group it belongs to, it holds that group's node graph, data model and param values, and drives those params. It is a phone app running on firmware. The device X.509 certificate is the only identity.

The firmware feature lives in `esp_rmaker_neo` behind `CONFIG_ESP_RMAKER_NEO_CONTROLLER`, which defaults to off.

- [Controller Identity and Data Plane](data_plane.md) -- certificate-signed REST calls, how the controller learns its group, the REST data APIs it may call, and the MQTT topics it reads and writes.

## What a Hub UI Reads

A controller that draws a screen needs more per param than a controller that prints a table. These accessors give it that, all from the config the SDK already parsed:

| Call | Answers |
|---|---|
| `esp_rmaker_controller_param_get_ui_type()` | Which widget the node asks for, as an `esp_rmaker_ui_type_t` rather than a string. `RMAKER_UI_TYPE_NONE` when the config named none, `RMAKER_UI_TYPE_UNKNOWN` for a string this SDK does not map. |
| `esp_rmaker_controller_param_get_device()` | The device or service a param belongs to. |
| `esp_rmaker_controller_device_get_node()` | The node a device belongs to. |
| `esp_rmaker_controller_device_get_primary_param()` | The one param a card shows as the device's state, resolved by id every call, so a primary the latest config dropped reports as gone. |

The two parentage calls are what a change callback needs: it hands out a bare param handle, and without them a consumer rescans the whole group to name the owner.

`online` is a shadow field rather than a param, so `esp_rmaker_controller_watch_params()` never reports it. `esp_rmaker_controller_watch_nodes()` reports it instead, on the controller task like every other callback. A card greys out on the transition rather than on the next manual refresh.

## Capability Discovery

The controller role is set by the cloud at registration, not advertised by the node. A controller publishes no capability attribute of its own, so a client determines that a node is a controller from the cloud's node record, never from the node configuration payload.

## Related Documentation

- [Node Configuration](../configuration.md) -- the node configuration payload a controller reads for each group member.
- [State Management](../state_management.md) -- named shadows, including the `params-<group_id>` shadows a controller reads and writes, and the partial updates its live observation depends on.
- [MQTT Topics](../networking/mqtt_topics.md) -- the wider topic namespace these topics sit in.

# Group Controller

Turns a device into a **RainMaker Neo group controller**: a node that observes and controls the *other*
nodes in the group it belongs to, from an interactive console. The device certificate is the only
identity.

All controller logic lives in `esp_rmaker_neo`, behind `CONFIG_ESP_RMAKER_NEO_CONTROLLER`; this example is
the console front end for it. The SDK owns the group graph, the data model and the wire formats, so
the console never builds a topic or a JSON body. Builds for ESP-IDF and POSIX.
ESP-IDF target: **esp32s3** (`idf.py set-target esp32s3`).

## What a controller can do

Scoped to its group:

1. **Hold the group graph** — which nodes are in it, and which subgroups each sits under. The SDK
   derives every node's shadow name from that path, so no command takes a subgroup argument
   (`sync`, `nodes`). The group lists the controller itself, and the SDK keeps that entry out of
   the inventory, so every node the example iterates is a node the controller drives.
2. **Hold each node's data model** — devices, services, params, data types, bounds and properties,
   as opaque handles (`sync`).
3. **Read params** — a named-shadow get seeds them once, then a group-wide subscription to the
   shadow update plane keeps them current with no polling (`sync`). `watch` only prints those
   changes as they arrive.
4. **Drive params** at node or group/subgroup level (`grouppower`, `set`, `groupset`).

## How it works

See [the controller spec](../../../docs/en/specs/controller/data_plane.md) for the full contract. In
short:

```
MQTT  ◄── getGroupInfo {"pgrp": "<g>", "subgrps": [...]}       ← learns its group here, like any node
MQTT  ◄── getServerConfig {ApiGatewayUrl, CredentialProviderEndpoint}
cert  ──► IoT Credential Provider (controller role alias) ──► SigV4 credentials
REST  ──► GET {ApiGatewayUrl}/v1/groups/<g>                    (SigV4-signed)
REST  ◄── { node_ids, subgroups }                              ← learns every node's shadow name
REST  ──► GET {ApiGatewayUrl}/v1/groups/<g>/nodes/<n>/config   (per node, when ncfg_ver moves)
MQTT  ──► shadow get + update observe + control publishes      (scoped to <g> by the IoT policy)
```

- **No session token.** Every REST call is signed with the certificate's SigV4 credentials, and the
  cloud identifies the controller from them. The signer re-fetches the credentials before they
  expire; revoking the certificate revokes everything.
- **The group comes from the node's own group info**, pushed by the cloud whenever the node is
  added to, moved between or removed from a group. The `group` command prints it.
- The cloud grants a controller only `GET` on `/v1/groups/<g>/...`; anything else gets a `403`.
- After a move to another group, REST calls follow at once and MQTT follows after the reconnect
  the SDK makes.
- `standby on` is a device-local switch: the controller keeps reading but refuses to set params.
- Every blocking call runs its work on one dedicated controller task, sized by
  `CONFIG_ESP_RMAKER_NEO_CONTROLLER_TASK_STACK_DEPTH`, so no caller needs a deep stack of its own.

## Configuration

None beyond a normal node. The REST API base URL and the credential-provider endpoint come from the
cloud's server config. The role alias (default `rmng-controller-role-v1`, must match the RainMaker
Neo CDK) is in menuconfig.

Enable the feature itself with `CONFIG_ESP_RMAKER_NEO_CONTROLLER=y` (this example's `sdkconfig.defaults`
already does). It defaults to off, so an image that is not a controller pays nothing for it.

## Registration

The node gets the **controller** capability when it is claimed. Assisted claiming is on by default,
and `CONFIG_ESP_RMAKER_NEO_CLAIM_CONTROLLER_SUPPORT` adds `controller` to the claim-verify request,
so the cloud attaches the controller policy to the certificate it issues. The deployment must
include the optional controller stack, or claiming fails.

To use pre-flashed credentials instead, set `CONFIG_ESP_RMAKER_NEO_ASSISTED_CLAIM=n` and register
the node with the controller capability (admin dashboard → Nodes → Register → Node Type =
Controller, or the admin API with `capabilities: ["controller"]`). Register controllers in their own
batch — bulk registration applies the type to every node in the CSV.

## Build and flash

ESP-IDF:

```bash
idf.py set-target esp32s3
idf.py menuconfig        # optional: set the controller role alias
idf.py build
idf.py -p <PORT> flash monitor
```

POSIX:

```bash
cmake -S . -B build_posix -G Ninja -DPython3_EXECUTABLE=<venv>/bin/python3
cmake --build build_posix
```

Provision the controller with the phone app like any node, then add it to the group it should
control.

## CLI commands

```
group-controller> group                       # print group, subgroups, standby state
group-controller> standby [on|off]            # local standby: refuse the write commands
group-controller> sync                        # fetch the group graph + every data model, print it
group-controller> nodes                       # reprint the table, with no cloud call
group-controller> state <node_id>             # print one node's cached params
group-controller> grouppower on               # power every device type in the group
group-controller> set <node_id> <device> <param> <value>
group-controller> groupset <device_type> <param_type> <value> [subgroup_id]
group-controller> watch [off]                 # print the changes, which apply either way
group-controller> raw <path_suffix>           # signed GET, for debugging
```

Run `sync`, then the write commands. `sync` resolves each node's subgroup path, so no command takes
one. It prints one row per node, which stays readable for a group of any size:

```
Group ujtgnh - 2 node(s), 2 subgroup(s)

  NODE                    STATE     SUBGROUPS
  f8kjs6VGkJBZ229WZ3hgXo  online    pey
  EqNmosMhG9KVDMcPfhJssm  offline   pey, wbo

  pey  Basement
  wbo  Front Yard
```

`state` prints one node's params, under a header that repeats its state and its subgroups:

```
group-controller> state f8kjs6VGkJBZ229WZ3hgXo
f8kjs6VGkJBZ229WZ3hgXo  online   pey
  Light [esp.device.lightbulb]
    esp.param.name              = Light240             "Name"
    esp.param.power             = true                 "Power"
  Local Control [esp.service.local_control] service
    esp.param.local_control_pop = 6edf6567             "POP" ro
```

The param type leads each line because that is what `groupset` addresses; the quoted name is what
`set` takes.

A node's `online` flag and its `ncfg_ver` both live in its shadow, so `sync` reads one shadow per
node. It fetches a config only for a node it holds none for, or one whose `ncfg_ver` moved past the
tree it holds, so re-syncing an unchanged group makes no REST call per node at all. `nodes` and
`state` print the cache and read nothing.

`groupset` addresses devices *and* services by type in one publish:

```
group-controller> groupset esp.service.time esp.param.tz Asia/Shanghai
group-controller> groupset esp.device.lightbulb esp.param.power false
```

A group publish addresses one device type, so `grouppower` reads the cached data models for every
device type that holds `esp.param.power`, then publishes one group write per type.

`watch` is a printing toggle, not a subscription. `sync` subscribes the shadow update plane, so the
cached values follow the group whether `watch` is on or off. With it on, each param change prints
the node, the device and the param. A group or config change prints one line for each node,
subgroup, device or param that the SDK adds, updates or removes:

```
group-controller> watch
watch on - changes are printed as they arrive
[watch] f8kjs6VGkJBZ229WZ3hgXo: Light.Power = true
[watch] added param Brightness
[watch] updated node EqNmosMhG9KVDMcPfhJssm
```

The example runs its own REPL rather than calling `esp_rmaker_console_init()`, because the SDK
console has no line editing, and registers the SDK built-in commands (`get-node-id`, `node-info`)
on it. The REPL keeps the default task stack: the TLS and SigV4 work runs on the controller task,
not on the REPL task.

## Limitations

- `sync` subscribes two topic filters per distinct shadow name, and those share one table with
  the agent's own, bounded by `CONFIG_OSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS`. Its
  default of 10 is too small for a controller, so this example sets 64: a group of 5 subgroups
  with a node in at most 3 reaches 26 distinct shadow names (1 root plus C(5,1)+C(5,2)+C(5,3)
  combinations), 52 filters, plus the agent's 7. A full table logs
  `Subscribe to '<filter>' failed: 259` and names the option, and `sync` fails with the group
  partly observed. Check the figure against your broker's per-connection subscription limit.
- A rejected write is not observed. `update/rejected` would cost a third filter per shadow name,
  so a write the node refuses is silent.
- `groupset` takes bare type strings with no param handle, so it cannot refuse a read-only param
  the way `set` does.
- No writes to group membership. A controller reads the group; it does not change it.

# RainMaker Neo Matter Mirror

Derives a **Matter data model from the RainMaker Neo data model** at init and keeps both sides in
sync at runtime. The RainMaker Neo param store remains the single canonical state store; Matter is
a mirror, not an alternative data model.

- **Declarative mapping library** (`mapping/lib/`): standard param types map to Matter
  clusters/attributes with value transforms (linear, kelvin/mireds, enum maps, string,
  scale) and param-subset rules resolving devices to Matter device types.
  `scripts/gen_mapping_table.py` emits compiled-in C tables at build time — adding a
  device type is a JSON-only change (given its clusters exist in the port's cluster
  factory). The library is a copy of the canonical mapping repo; see "Mapping source".
- **Profiles** (`mapping/profiles/`): a profile names the device types a product ships,
  and the generator emits the tables for that selection alone — library entries nothing
  kept references are dropped. Selected through the `Mapping profile` Kconfig choice
  (default: every device type in the library).
- **Composites** (N params ↔ M attributes with enumerated conversions): `xy_from_hs`
  derives Matter `CurrentX`/`CurrentY` from hue+saturation (integer sRGB/D65 color math,
  gamma included) — Extended Color Light mandates XY and Matter has no HS-only light
  device type. Inbound XY writes inject hue+saturation atomically (one write callback
  invocation); a hue/sat light *without* CCT falls through to 0x0101 with color
  cloud-only (0x010D mandates CT, 0x010C forbids HS — no valid Matter target).
- **Platform-neutral engine** (`src/engine`, plain C): lowering, bidirectional sync with
  self-write echo consumption, same-value suppression and coalescing of Matter transition
  step storms. Unit-tested on POSIX against a recording fake port (`test_rmaker_neo_matter_mirror/`).
- **esp-matter port** (`src/port`, C++): per-cluster factory (ColorControl accumulates
  CT/HS/XY FeatureMap bits across capabilities), thread-safe attribute reports, identification
  callback (real IdentifyTime, TriggerEffect mapped to nominal durations), read-only
  FixedLabel correlation entries (`rmng.device` = device id per endpoint; Neo ids are at most
  15 chars, so they fit Matter's 16-char label value) and
  Name/NodeLabel sync. It carries no per-attribute Matter semantics: value wrapping,
  shadow attributes, `StartUp*` policies and deferred NVS persistence are all declared in
  the mapping and driven by the engine.

An OTA restart skips `StartUpOnOff`, as CHIP does. CHIP detects only a Matter OTA. The mirror also
detects a RainMaker OTA by its unverified image, so that needs `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.

Deliberate non-goals (curated vocabulary, not Matter feature parity): Scenes Management,
EnhancedHue/ColorLoop, and Ballast Configuration are not mirrored; RainMaker scenes stay a
cloud-level service.

## Usage

```c
/* after all devices/params are added, before esp_rmaker_start() */
esp_rmaker_matter_mirror_enable(node, NULL);
```

Matter controller commands arrive at the device's regular RainMaker Neo write callback with
`src = ESP_RMAKER_REQ_SRC_EXTERNAL`, exactly like cloud writes. Hardware is driven only via
that callback.

Outbound (cloud/app/schedule → Matter) writes invoke the capability's `write_as` command from the
mapping table — On/Off, MoveToLevelWithOnOff, MoveToColorTemperature — so the cluster runs its own
logic and the derived attributes follow. Writing the attribute directly is the fallback, used for
capabilities with no `write_as` (NodeLabel) and before the Matter stack is up.

With `defer_matter_start`, call `esp_rmaker_matter_mirror_register_hooks()` right after starting
Matter yourself. The mirror's Matter-side hooks allocate through the CHIP platform allocator, so
they cannot be registered until the stack is up. Call it from an application task, not from a CHIP event handler:
with the CHIP stack lock held it returns `ESP_RMAKER_INVALID_STATE`, and controller writes never
reach the params.

## Devices added at runtime

A bridge adds and removes devices after `enable()`, as children join and leave:

```c
esp_rmaker_node_add_device(node, child);
esp_rmaker_matter_mirror_add_device(child);         /* new Matter endpoint, seeded from the params */
esp_rmaker_matter_mirror_remove_device(child);      /* unbinds, then destroys the endpoint */
esp_rmaker_node_remove_device(node, child);
esp_rmaker_device_delete(child);
```

Call `remove_device()` before `esp_rmaker_node_remove_device()`, and call none of these from a device
write callback or from a task that holds the CHIP stack lock. On `ESP_RMAKER_TIMEOUT` the device is
still mirrored: keep it alive and call `remove_device()` again.

The new endpoint is seeded from the params before controllers can see it. Its clusters then apply
their StartUp* attributes when it is enabled, and those values reach the params: Matter wins, as at
boot. A removed endpoint stays allocated, disabled, for a few seconds until its pending attribute
writes are stored.

Endpoint IDs are stable by node ID and device ID. The mirror reads the node from the device, so add
the device to its node (the mirrored node or a bridged child node) before `add_device()`. The mirror
stores which endpoint each device had, in the `rm_mirror` NVS namespace, so a device added again gets
the same endpoint back, also after a reboot.
When the node rebooted without removing the device, its endpoint still holds the stored Matter state,
and the mirror adopts that state as it does at init. A removal erases that state, so a device added
again in the same boot seeds from its params. Right after a removal, `add_device()` waits until the
old endpoint is freed.

The node ID and device ID must always name the same physical device. The same device ID on two
nodes gives two endpoints. A pair used again for another device gives that device the old endpoint
ID, and that device can adopt the old device's state.

The devices that `enable()` lowers keep the IDs that their order gives them: before Matter starts,
esp-matter cannot use a fixed ID. The map keeps `CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ENDPOINT_MAP_MAX`
devices (default 32). When it is full, the mirror drops the oldest entry of a device that is not
mirrored now.

Known limit: esp-matter keeps the LevelControl and ColorControl state of an endpoint by its position
among the endpoints with that cluster. When a removed endpoint is freed, each later endpoint with
those clusters reads the state of the one before it: its stored On level and any running transition
can be wrong until its level next changes.

### Matter bridge topology

With `CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_BRIDGE`, `enable()` first creates an Aggregator endpoint.
A device that `add_device()` adds from a node other than the mirrored node goes under the Aggregator
as a Bridged Node. Its endpoint also gets a Bridged Device Basic Information cluster:

- `UniqueID`: the device's map key in hex, so it stays the same for the same node ID and device ID.
- `NodeLabel`: the value of the device's `esp.param.name` param at the add, or the device ID.
- `Reachable`: true, until the application sets it.

```c
esp_rmaker_matter_mirror_set_reachable(child, false);  /* the child left the network */
```

The mirror keeps the value that `set_reachable()` gives, so you can set it before `add_device()`,
and a device added again gets it back. The mirrored node's own devices stay top-level endpoints.
The Aggregator takes the first endpoint ID, so the option moves their IDs up by one.

Known limit: a later change of the name param does not update `NodeLabel`.

## Adding a device type

Device types are data: for most devices the whole addition is JSON, compiled into C
tables at build time (the generator validates the schema and fails the build on errors).
The steps, in order of increasing rarity:

1. **Pick the Matter target.** From the Matter device library, find the device type id +
   revision for the RainMaker Neo standard params the device carries, and note its mandatory
   clusters and FeatureMap bits. Not every RainMaker Neo shape has a valid target (e.g. an
   HS-only light: 0x010D mandates CT, 0x010C forbids HS) — in that case rule ordering
   should let the device fall through to a simpler type, with the extra params staying
   cloud-only.
2. **Map the params** (`mapping/lib/rmng_matter_mapping.json` → `capabilities`). One entry
   per standard param type: `matter_match` (cluster/attribute/`feature_map`/`value_type`), a `transform`
   (`identity`, `linear`, `kelvin_mireds`, `enum_map` — the vocabulary grows by
   enumeration, never expressions), optional `write_as` commands (outbound writes invoke
   these so the cluster maintains its derived attributes), optional `bounds_from`,
   optional `shadow_attributes` (written with the same value as the matched one, e.g.
   EnhancedColorMode tracking ColorMode), an optional `startup` block naming the
   capability's `StartUp*` attribute and its boot policy, `deferred_persistence: true` for
   values that ramp during transitions, and `rule_gated: true` when the capability's
   cluster must not appear on other device types the same params could match.
3. **Composites, if state is not 1:1** (`composites`). N params ↔ M attributes through
   an enumerated `conversion` (e.g. `xy_from_hs`). A new conversion type needs a matching
   engine function (see `rm_mirror_color.c` for the pattern) — this is the one case where
   vocabulary growth includes engine code.
4. **Add the device rule** (`device_types`). Param-subset rules ordered
   most-specific-first, with `optional_params` for gate-listed extras and
   `mandatory_clusters` (Identify/Groups/FixedLabel at minimum). A rule may also list
   `additional_device_types` — further device types the same endpoint declares, each with
   its own mandatory clusters. Matter defines some clusters as belonging to a device type
   rather than to whatever endpoint they sit on (the electrical measurement clusters are
   an Electrical Sensor, 0x0510, which in turn mandates Power Topology), and a controller
   ignores clusters no device type on the endpoint claims:

   ```json
   "additional_device_types": [
     { "id": "0x0510", "version": 1, "mandatory_clusters": ["0x009C"] }
   ]
   ```
5. **Add a port cluster factory** (`src/port/clusters/<cluster>.inc`) for each cluster
   not yet constructed, and a row in `__cluster_factories` in the port. This is
   deliberate C code: each cluster pulls in KB-scale esp-matter/CHIP implementation, and
   only the factories this build's mapping needs are compiled in. Attribute wire types
   are not part of it — they come from the mapping's `value_type`. Forget this step and
   the build fails naming the cluster, not the device in the field.
6. **Enable the cluster code** in the product's sdkconfig (`CONFIG_SUPPORT_*_CLUSTER`)
   and add it to `src/port/clusters/cluster_kconfig.cmake` — cluster code, not the
   mapping table (~80 B/entry), is the footprint cost. Forgetting the sdkconfig entry
   fails the build; forgetting the cmake entry only makes the advisory below noisier.
7. **Test on POSIX** (`test_rmaker_neo_matter_mirror/`): rule matching, lowering (endpoint type,
   features, seeds, binding counts), a sync round-trip against the fake port and the
   golden-vector parity check — no hardware or esp-matter needed. The C emitters have
   their own suite: `pytest scripts/test_gen_mapping_table.py`, run from this directory,
   and in CI as `test_matter_mirror_mapping_gen`.

## Mapping source

The mapping is not owned here. `esp-rainmaker-neo-matter-mapping` is canonical, and the
firmware, the phone-app SDK and headless controllers all read the same vocabulary. Treat
the JSON like a `.proto`: this component commits a copy of it, and generates its own C.

| Path | |
|---|---|
| `mapping/lib/rmng_matter_mapping.json` | copied input: the vocabulary |
| `mapping/vectors/rmng_matter_mapping.vectors.json` | copied input: golden vectors, emitted upstream from its Python reference transforms |
| `scripts/mapping/` | copied input: the validator every consumer shares |
| `mapping/SOURCE` | what was copied, from which commit, with a hash of each file |
| `mapping/profiles/` | this component's own |
| `scripts/gen_mapping_table.py` | this component's own: the C emitters |

`scripts/sync_mapping.sh <mapping checkout>` refreshes the copies and stamps `SOURCE`;
`--check` re-hashes them and fails on local drift (it runs in CI). Nothing generated is
committed — the tables, the cluster cross-check and the vectors header are all build
products, so a build and its mapping cannot disagree.

The split of work follows the same line. The shared validator checks the mapping language:
cross-entry references, rule ordering, arity, and the int32/uint8 limits every consumer
honours. This component adds what only it can know — that each value fits the field its
tables give it (`_check_widths`, passed to `validate()` as an extra check), that every
cluster the mapping needs has a factory in the port, and that the generated field widths
still match `priv_include/rm_mirror_mapping.h`. The last two are enforced by the C
compiler, through `rm_mirror_cluster_check_gen.h` and `RM_MIRROR_STATIC_ASSERT`.

Adding a transform kind or a composite conversion is a code change here as well as
upstream. Importing the generator asserts its C symbol tables cover the shared
vocabulary, or list a word in `UNIMPLEMENTED`. A new word in neither fails the sync. A
profile that keeps a word in `UNIMPLEMENTED` fails the build, and the error names the entry.

## Profiles

A profile names the device types a product ships; everything else in the library is
dropped from the generated tables, which is what keeps unused vocabulary off the flash.

```json
{
  "profile_version": "1.0",
  "name": "light",
  "device_types": ["esp.device.lightbulb"]
}
```

`"device_types": "*"` keeps every device type the library defines. The `all` profile lists
the device types this port implements, because the generator refuses a kept entry that uses
a word the engine does not have yet (`UNIMPLEMENTED` in `scripts/gen_mapping_table.py`).
`capabilities` adds entries no kept rule references; `exclude_capabilities` drops ones
that are only ever optional — excluding a param a kept rule *requires* is a build error,
since the rule could never match again.

A capability is kept when it is node-scoped, referenced by a kept rule's `params` /
`optional_params`, or targets a cluster a kept rule lists as mandatory (Identify). A
composite is kept when all its member params are.

Select one with the `Mapping profile` Kconfig choice — `all` (default), `light`,
`switch`, or a custom profile JSON path, which lets a product keep its profile out of
the SDK tree.

To extend the vocabulary itself, point
`CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTRA_LIBS` at one or more library files
(semicolon-separated). They are merged after the shipped library, so a product adds its
own capabilities, composites and device types without editing it; a key defined by two
library files is a build error rather than a silent override.

## Cluster factories

The port builds a cluster per file in `src/port/clusters/`, and compiles in only the
ones this build's mapping needs — the generator emits `RM_MIRROR_USES_CLUSTER_<id>` from the
resolved mapping, and the port's factory table is guarded on them. This is where the flash goes: dropping LevelControl and
ColorControl from a lightbulb build takes ~19 KB off the image, because nothing
references the CHIP cluster code any more and the linker discards it. Trimming the
matching `CONFIG_SUPPORT_*_CLUSTER` options on top of that is still worth doing.

The engine is gated the same way. The generator also emits the mapping's vocabulary —
`RM_MIRROR_USES_XFORM_<type>`, `RM_MIRROR_USES_COMPOSITES`, `RM_MIRROR_USES_CONV_<type>`,
`RM_MIRROR_USES_STARTUP_<policy>` — and the engine compiles in only those: the hsv_xy
colour maths and its lookup tables, the linear/mireds/enum transforms, the composite
lowering path and the per-policy `StartUp*` arms. Worth ~3 KB on a power-only profile, on
top of the cluster factories. Vocabulary every profile uses anyway is deliberately *not*
gated — the node-scoped name binding and its string paths would be `#if`s that are always
true.

A mapping that needs a cluster with no factory **fails the build**, naming the cluster:
each factory file defines `RM_MIRROR_PORT_HAS_CLUSTER_<id>`, and the generated
`rm_mirror_cluster_check_gen.h` — included by the port after its factories — checks the
two against each other. Node-scoped capabilities are exempt: they bind on endpoint 0,
whose clusters the Matter stack provides.

CHIP's clusters also reach into each other whatever the build holds — LevelControl calls
into the ColorControl server for its coupled colour temperature, OnOff calls into
LevelControl for its transition effects — so trimming one of a pair breaks the link, not
the compile. The factory of the *calling* cluster carries a stub for each such callback,
compiled only when the callee's `CONFIG_SUPPORT_*_CLUSTER` is off; both are unreachable at
runtime, since CHIP checks the endpoint for the other cluster before calling.

Cluster code has to be enabled in esp-matter too, and the two checks run in opposite
directions:

- **Needed but disabled fails the build.** Each factory file `#error`s when its
  `CONFIG_SUPPORT_*_CLUSTER` is off, so a mapping asking for OnOff with
  `CONFIG_SUPPORT_ON_OFF_CLUSTER=n` stops at compile time instead of failing to link or,
  worse, producing an endpoint with a missing cluster.
- **Enabled but unused is reported.** At configure time the build lists
  `CONFIG_SUPPORT_*_CLUSTER` options that are on but that neither the port builds nor a
  commissionable node requires (`src/port/clusters/cluster_kconfig.cmake` holds both
  lists). Advisory, not a warning — an application may drive a cluster itself.

The second one is hygiene, not a flash lever: esp-matter defaults *new* clusters to `y`,
so a product's exclusion list goes stale as esp-matter grows, but a cluster nothing
references is already discarded by `--gc-sections` before it reaches the image. Measured
on `matter/matter_light`, turning off 22 such clusters moved code between `libCHIP.a` and
`libesp_matter.a` and changed the binary by under 100 bytes. Keep the list tidy for the
sake of an honest sdkconfig — but if you are hunting for flash, the levers are the
cluster factories above (a reference the linker can actually follow) and the mapping
profile, not this.

A product whose own library uses a cluster the SDK has no factory for can supply one at
runtime instead: enable `CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS` (which
relaxes the build check into a record of what is expected) and call
`esp_rmaker_matter_mirror_register_cluster_factory()` before `enable()`. The hook is
consulted only after the compiled-in factories, so it cannot override them, and a cluster
still missing at that point fails at init rather than silently producing a bare endpoint.

Two mapping features exist for capabilities Matter does not carry as a plain scalar:

- `transform: {type: scale, factor: N}` multiplies a RainMaker Neo value by `N` on the way out (and
  divides on the way in), which is how a float param in base units (W, Wh) reaches a Matter
  attribute in milli-units without losing its fractional part. Float params are read in the
  float domain for exactly this transform; every other transform rounds to an integer first.
- `report_as: {kind: ...}` routes the value to the handler registered with
  `esp_rmaker_matter_mirror_register_report_handler()` instead of writing the attribute, for
  clusters that own their value — Electrical Energy Measurement notifies a struct rather than
  exposing a writable attribute. The kind is a free string the handler dispatches on, so it
  lives with the code that builds the cluster. Such capabilities are not seeded during
  lowering (the notify path needs the running stack); their first param update pushes the
  value, and unchanged values are suppressed as everywhere else.

## Node identity in the CSR

`enable()` puts the RainMaker node id in the CSR's vendor-reserved1 field, so a
commissioner learns which cloud node it is commissioning from the exchange itself instead of a
separate association step. Best effort: a node that has not been claimed yet has no node id, and the
mirror logs a warning and carries on rather than blocking Matter commissioning.

## DAC-backed credentials

Optional: one identity for both stacks, so the factory image carries no copy of the DAC outside
the `chip-factory` namespace.

```c
/* before esp_rmaker_node_init(), which resolves the node id */
esp_rmaker_credentials_provider_override(esp_rmaker_matter_mirror_get_dac_credentials());
```

The MQTT client certificate and key come from the `dac-cert` / `dac-key` factory entries and the
node id from the DAC subject common name; the MQTT host and random bytes keep their defaults.
Needs Matter factory data on the device (`CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER`) — the
providers fail with `ESP_RMAKER_NOT_FOUND` otherwise, and there is no fallback to the RainMaker
credentials once installed.

Install it before *any* init that reads credentials, `esp_rmaker_node_init()` included.
Assisted claiming checks for an existing certificate at that point, so an override installed
later leaves it claiming a node that already has an identity.

## Factory reset

A factory reset has to clear both sides, or the node comes back factory-new to the cloud while
Matter controllers still hold valid fabric credentials for it. Both directions are wired:

- **RainMaker Neo-initiated** (cloud `System` service, console, button): the mirror registers a factory reset
  participant, so `esp_rmaker_system_ctrl_factory_reset()` wipes the RainMaker Neo data and then calls the
  mirror's wipe. Declared `reboots_on_wipe`, so it runs last and does not return -
  `esp_matter::factory_reset()` erases the Matter state and CHIP restarts the node.
- **Matter-initiated** (`matter esp factoryreset`, or an app policy on last-fabric removal): CHIP
  posts `kFactoryReset` before erasing anything, and the mirror's handler calls
  `esp_rmaker_system_ctrl_factory_reset_from_participant()`, passing its own wipe as `self` so the
  RainMaker Neo data and every *other* participant are wiped while the mirror's own state is left to CHIP.
  That runs on the Matter thread ahead of CHIP's own wipe, while MQTT is still up, so the cloud
  `node_reset` notification still goes out. A negative reboot timeout leaves the restart to CHIP.

`chip-factory` is never erased: it holds the manufacturing data (DAC, CD), and erasing it would make
the device uncommissionable.

ESP-IDF only (requires esp-matter via the project's `EXTRA_COMPONENT_DIRS`; see the
`matter/matter_light` example). The engine builds and is tested on POSIX without
esp-matter.

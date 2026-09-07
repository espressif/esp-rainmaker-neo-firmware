# Common Data Model (Draft)

Status: **draft — for discussion**

## 1. Purpose

Today a RainMaker Neo node speaks one of two wire data models to the cloud depending on how it
was built: the RainMaker Neo default model (`devices → params`) or a Matter pass-through model
(`endpoints → clusters → leaves`). The cloud understands only the former (for Alexa/GVA
export) and passes the latter through; phone apps must implement both for remote control.

This spec defines a **single common data model** as the only wire contract on the
RainMaker path (firmware ↔ cloud ↔ apps ↔ dashboards). Matter is repositioned as:

- a **local transport** (apps/controllers do native Matter on the LAN), and
- an **ecosystem compatibility layer** (Apple Home / Google Home / Alexa-local talk
  native Matter to the device directly).

The cloud never sees Matter. Precedent: Home Assistant, SmartThings, Alexa, Google Home
and Apple Home all normalize Matter into their own (less rich, sufficient) native models
at the edge; none exposes raw Matter above the transport layer.

### Goals

1. **Any device type, one effort** — building a Matter + RainMaker product must not
   require per-device-type code in apps, cloud, bridges, or the SDK's Matter layer.
   Device types are *data* (vocabulary + mapping entries), not code.
2. **Runtime translation** — controllers/bridges/apps that discover an arbitrary
   (possibly third-party, pure-Matter) node locally can *raise* its Matter data model
   into a common-model node config at runtime, store it (locally or on cloud), and
   render/control the device even when it is not currently reachable over Matter.

### Non-goals

- Abstracting Matter commissioning, fabric management, or third-party controllers.
  A node remains a fully native Matter device to Apple/Google/etc.
- 1:1 Matter feature parity. The vocabulary grows by **product decision**, not by
  Matter import (e.g., Identify is adopted; Ballast Configuration is not).
- Headless-controller completeness (no human looking at a device picture). Deferred;
  see §8. The design leaves room (match-confidence marker) but does not spec it now.

---

## 2. Architecture overview

```
                     ┌─ common model ──── RainMaker Neo cloud ───── phone app (upper layer:
 firmware core       │   (only model                        common model only)
 (common model =     │    cloud ever sees)                      │
 single state store) │                                          │
                     └─ Matter mirror ─── fabric ──┬── app lower layer (native
                        (optional,                 │   Matter controller, local)
                         derived from the          ├── Apple/Google/Alexa local
                         common model)             └── RainMaker Neo Matter bridge
                                                        (raising engine → cloud)
```

- **Firmware**: the common model is the always-present data model. When Matter is
  enabled, the SDK *derives* the Matter cluster tree from the declared devices/params
  (lowering) and keeps the two in bidirectional sync (mirror).
- **Cloud**: stores/serves common-model configs and state only. Node-level flag in GET
  node APIs states `rmng` vs `rmng+matter` (clients derive per-param Matter-ability
  from the shared mapping library — no per-param flags on the wire).
- **Apps**: layered. Upper layer speaks common model exclusively (remote control via
  cloud). Lower layer is a normal Matter controller for local control — it discovers
  the real cluster tree from the device; no endpoint-addressing metadata is needed in
  the common model.
- **Pure Matter nodes** (third-party): onboarded by the app or a bridge, which runs the
  **raising engine** to produce a *derived* common-model config, registers it with the
  cloud, and relays state. The device is thereafter renderable/controllable in the app
  like any RainMaker Neo node (control routed via the reporter while local; render-complete even
  when unreachable).

---

## 3. The common data model

### 3.1 Structure — unchanged

The model **is** the RainMaker Neo default model, hardened. Hierarchy, JSON shapes, param
reporting (`state.reported.params.{device}.{param}`), set-params payloads, group
control, and time-series flags are unchanged from
[Node Configuration](./configuration.md) and
[State Management](./state_management.md).

### 3.2 Versioning (new, required)

Node config gains:

```json
"data_model": "default",
"model_version": "1.0"
```

`model_version` versions the standard vocabulary. Clients render unknown standard
params generically from `data_type` + `bounds` + `ui_type` (which the config format
already makes possible — this is a deliberate advantage over Matter and must be
preserved: every param must remain render-complete without vocabulary knowledge).

### 3.3 Standard vocabulary — convention becomes contract

For every standard param type (`esp.param.*`), the spec normatively fixes:

| Aspect | Rule (example: `esp.param.cct`) |
|---|---|
| `data_type` | fixed (`int`) |
| Canonical unit | fixed (**kelvin** — never mireds on the wire) |
| `bounds` | **mandatory** (they become load-bearing: lowered to Matter physical min/max) |
| Cardinality | at most **one** param of a given standard type per device |
| Properties | allowed set fixed (e.g., `read`,`write`) |

Device types (`esp.device.*`) define required + optional standard params. Variants are
**implicit, feature-map style**: no `esp.device.cct_lightbulb`; a lightbulb with
`{power, brightness, cct}` *is* a CCT light. Lowering/raising key off params present,
mirroring how Matter's own feature maps distinguish light variants.

### 3.4 Custom params — the escape hatch

Free-form params remain first-class (RainMaker's differentiator; neither HA nor Matter
can express them). Rules:

- No Matter lowering: custom params do not exist on the fabric.
- Consequently **cloud-only** for control on Matter-enabled nodes (local-ctrl transport
  may be reintroduced per-product later if low-latency local custom control is needed).
- Must still be render-complete (`data_type`, `bounds`, `ui_type`).

### 3.5 Provenance (new)

```json
"origin": { "type": "native" }
"origin": { "type": "derived", "source": "matter", "mapping_version": "1.2" }
```

- `native`: authored and reported by RainMaker Neo firmware. Authoritative.
- `derived`: raised from a discovered Matter node by an app/bridge. Best-effort;
  re-derived (and enriched) when the mapping table updates or the source node is
  OTA-updated. Coverage gaps heal retroactively without re-onboarding.

### 3.6 Derived identity — stability rules

Raising must be deterministic and stable across rediscovery so cloud history and
automations survive:

- Node identity: anchored to the Matter node's operational identity recorded at
  commissioning (this is also the app's cloud-node ↔ fabric-node correlation record).
- Device id: derived from endpoint number (e.g., `Light-EP1`).
- Param id: the standard default id of the matched capability (`CCT`, `Brightness`).

### 3.7 Vocabulary additions forced by Matter mandates

- **`esp.param.identify`** (new standard param): `int` seconds, `properties: ["write"]`
  (write-only — the existing property flags already express this). Required on device
  types whose Matter device type mandates the Identify cluster. Firmware must implement
  a visible identify behavior. (Adopted on product merit — user-meaningful, HomeKit had
  it too — not merely because Matter mandates it.)
- Matter-mandatory *internal* clusters (Descriptor, Groups, Basic Information) are
  synthesized by the SDK during lowering and never appear in the common model.

---

## 4. The mapping table (the central artifact)

A **versioned, declarative, bidirectional** table — data, not code — consumed by three
runtime contexts from one source:

| Consumer | Direction | Form |
|---|---|---|
| Firmware SDK (end devices) | lowering (common → Matter tree) | C tables generated per *profile* at build time by the firmware's own emitter, from a vendored copy of the mapping |
| Headless controllers / RainMaker Neo Matter bridge | raising (discovered tree → derived config) | C runtime interpreter over the *runtime JSON* (cloud-deliverable) |
| Phone apps / dashboards | raising + value translation | TS runtime interpreter over the same runtime JSON |

Because a bridge-class device must run the raising engine, the rule language is
deliberately small: match rules + enumerated transforms. No scripting. The table
lives in its own repository, `esp-rainmaker-neo-matter-mapping`, together with the
generator/validator, JSON schemas, Python reference implementations of every primitive,
and golden test vectors (§4.6).

### 4.1 Capability entry schema

```json
{
  "capability": "esp.param.cct",
  "matter_match": { "cluster": "0x0300", "feature_map": "0x00000010", "attribute": "0x0007",
                    "value_type": "u16" },
  "transform": { "type": "kelvin_mireds" },
  "write_as": { "commands": [
    { "command": "0x0A", "name": "MoveToColorTemperature", "args": [
      { "field": 0, "name": "ColorTemperatureMireds", "type": "u16", "value": "$value" },
      { "field": 1, "name": "TransitionTime", "type": "u16", "value": 0 },
      { "field": 2, "name": "OptionsMask", "type": "u8", "value": 0 },
      { "field": 3, "name": "OptionsOverride", "type": "u8", "value": 0 }
    ] }
  ] },
  "bounds_from": { "min": "attr:0x400C", "max": "attr:0x400B", "value_type": "u16" },
  "defaults": { "data_type": "int", "ui_type": "esp.ui.slider",
                "properties": ["read", "write"] }
}
```

- `matter_match` — raising trigger and lowering target. `value_type` (`bool`, `u8`/`u16`/
  `u32`, `i8`/`i16`/`i32`/`i64`, `enum8`, `nullable_u8`/`nullable_u16`/`nullable_i64`,
  `string`) declares how
  the value is wrapped on the Matter side, so a consumer never has to carry a table of
  attribute widths keyed by cluster — the same enumeration types `write_as` arguments.
- `transform` — enumerated value conversions (`identity`, `linear{in,out ranges}`,
  `kelvin_mireds`, `enum_map{...}`, `bool_int`, `scale{factor}`). `scale` is the one
  conversion applied in the float domain for float params, so a reading in base units
  (W, Wh) reaches Matter's milli-units with its fractional part intact.
- `write_as` — bridges the write-model gap: common-model writes are *desired state*;
  Matter control is *command invocation*. Reports flow from the matched attribute.
  Each entry carries the command id and its payload fields as (TLV context tag, width,
  literal-or-`$value`) triples, so an engine can encode the invocation without a copy of
  the cluster definitions. An entry may add `"when": <matter value>` to select by value —
  On/Off rather than the non-idempotent Toggle; at most one entry may omit it, and that
  one applies to every other value. Writing the matched attribute directly is the
  fallback, and leaves cluster-derived attributes (`ColorMode`, OnOff coupling,
  `RemainingTime`) stale.
- `report_as` — the value is not a writable attribute but one the cluster owns and
  notifies (`CumulativeEnergyImported`, an `EnergyMeasurementStruct`). The `kind` names the
  delivery path for the consumer to implement; there is no attribute write, and no seeding
  before the stack that owns the cluster is running. Mutually exclusive with `write_as`.
- `bounds_from` — where the raising engine reads bounds on a third-party device;
  in lowering, the same fields are written from the param's mandatory bounds. `min`/`max`
  are the param's bounds; each names the attribute it maps to through the transform. A
  decreasing transform crosses them: the kelvin min is `ColorTempPhysicalMaxMireds`.
- `shadow_attributes` — attributes on the same cluster that take the matched attribute's
  value on every write. `ColorMode` → `EnhancedColorMode` is the case in hand: the two
  share values 0/2, and a curated vocabulary never writes an enhanced-only mode, so the
  pair must not drift. Declared here rather than hardcoded in a consumer.
- `startup` — which `StartUp*` attribute carries the capability's boot state, and how to
  read it: `passthrough`, `min_clamp` (a stored 0 means the `min`, as `StartUpCurrentLevel`
  defines it) or `toggle_previous` (`StartUpOnOff`'s 0/1/2). Enumerated like the transforms,
  so the policy name selects engine code. `skip_on_software_update: true` reproduces CHIP's
  rule that an OTA restart is not a power cycle. No `value_type`: these attributes are only
  ever read, and a read is width-agnostic.
- `deferred_persistence` — the value ramps during transitions (`CurrentLevel`,
  `ColorTemperatureMireds`, the composite's `CurrentX`/`CurrentY`), so a consumer that
  persists on write should batch instead of spending a flash write per step. Also valid on
  a composite, where it covers every derived attribute.
- `defaults` — presentation synthesis for derived configs (render-completeness).

### 4.2 Device-type entry schema

```json
{
  "device_type": "esp.device.lightbulb",
  "matter_device_types": ["0x0100", "0x0101", "0x010C", "0x010D"],
  "capabilities": {
    "required": ["esp.param.power"],
    "optional": ["esp.param.brightness", "esp.param.cct", "esp.param.hue",
                 "esp.param.saturation", "esp.param.identify"]
  },
  "primary": "esp.param.power"
}
```

A rule may carry `additional_device_types`: further Matter device types the endpoint
declares alongside the matched one, each with its own `mandatory_clusters`. Matter scopes
some clusters to a device type rather than to an endpoint — the electrical measurement
clusters belong to an Electrical Sensor (`0x0510`), which mandates Power Topology — and a
controller ignores clusters no device type on the endpoint claims. Composing them on one
endpoint keeps the correlation simple: one RainMaker Neo device stays one endpoint.

### 4.2.1 Composites: N params ↔ M attributes

Some Matter state has no single-param counterpart. The `composites` array binds an
ordered set of params to an ordered set of attributes through an **enumerated**
conversion (same policy as transforms: the name selects engine code, no scripting):

```json
{
  "id": "xy_from_hs",
  "params": ["esp.param.hue", "esp.param.saturation"],
  "matter_match": {"cluster": "0x0300", "feature_map": "0x00000008", "attributes": ["0x0003", "0x0004"],
                   "value_types": ["u16", "u16"]},
  "conversion": {"type": "hsv_xy"}
}
```

`hsv_xy` (integer sRGB/D65 with gamma) exists because Extended Color Light *mandates*
XY and Matter has no HS-only light device type — the firmware derives `CurrentX`/
`CurrentY` from hue+saturation and, inbound, injects hue+saturation **atomically**
(one write-callback invocation), so the app never sees a half-applied color. The same
mechanism is the intended home for the thermostat wave's shapes, e.g.
`{power, ac-mode} ↔ SystemMode` (many-to-one with an Off short-circuit) and a
mode-routed single-setpoint ↔ `Occupied{Heating,Cooling}Setpoint` — while
`setpoint-low/high-temperature` map 1:1 with a scale transform and need no composite.

Transform taxonomy (grows by enumeration, never expression): `identity`, `linear`,
`kelvin_mireds`, `enum_map` (int↔int pairs, with `matter_aliases` for many-to-one
foldings such as ColorMode XY→HSV), `string`; reserved next: `string_enum_map`
(string params like `ac-mode` ↔ Matter enums) and `scaled` (float params ↔
centi-unit ints, e.g. temperature ↔ `LocalTemperature`).

Two scaling controls keep the artifact's growth free for firmware:
- `rule_gated: true` on a capability lowers it only when the matched device rule
  lists it (in `params` or `optional_params`) — a hue param on a device that matched
  Dimmable Light must not sprout a non-conformant ColorControl cluster.
- Build-time subsetting: a product picks a *profile* naming the device types it ships,
  and the generator drops unlisted device types plus capabilities/composites only they
  reference. Tables are pure rodata (~80 B per capability) either way; the real
  footprint lever remains the Matter cluster code itself, enabled per product via
  `CONFIG_SUPPORT_*_CLUSTER`.

### 4.3 Two-level matching (raising)

1. **Device-type match**: a Matter device-type id on an endpoint selects the device
   entry; capability entries then populate params. Ideal result.
2. **Cluster-level floor**: if no device-type entry fires, capability entries still
   match individually — an unrecognized gadget still yields `Power`/`Brightness`
   params for the clusters it does have, on a generic device. Partial beats blank.

A derived param records which level matched (single field) — reserved for the future
headless-controller policy (§8), unused otherwise.

### 4.4 Endpoint correlation and naming

Clients (e.g. the phone app acting as a local Matter controller) must be able to
correlate a RainMaker Neo device with its Matter endpoint, including on nodes with several
identical devices. Two complementary mechanisms:

1. **Ordering contract (baseline, nothing on the wire):** mappable devices, taken in
   node-config order, correspond to the node's non-root endpoints in ascending order.
   Firmware is contractually bound to lower devices in declaration order, and a device
   added by an OTA update MUST be appended after existing devices (endpoint ids must
   stay stable across reboots and updates — controllers reference them in bindings,
   scenes and automations).
2. **Device-authored endpoint label (authoritative):** each mirrored endpoint carries
   a **read-only FixedLabel** entry `rmng.device` = the RainMaker Neo device id, served by the
   firmware from its binding table via an attribute-access override — tamper-proof by
   cluster definition (FixedLabel has no write access). Matter caps a label value at 16
   characters and RainMaker Neo caps a device id at 15, so the id is never truncated. Clients
   fall back to the ordering contract if the entry is absent (e.g. older firmware).

**Naming:** on a node with exactly one mapped device, the device's `esp.param.name`
syncs bidirectionally with `BasicInformation.NodeLabel` (endpoint 0, truncated to 32
chars) — the `node_scoped` capability in the mapping table. A multi-device node has no
node-level name: its per-device names stay cloud-side, and a client that wants them reads
the node config and matches each device to its endpoint through `rmng.device`.

Matter offers no per-endpoint name a non-bridge node can carry, and the candidates do not
hold up. `UserLabel` is a general-purpose list any controller on the fabric replaces
wholesale — other ecosystems put their own entries (room, orientation) there, so an entry
that changes or disappears cannot be told apart from a rename, and its 16-char cap would
clip a longer RainMaker name on the way back into the param store. `Descriptor.TagList` is
read-only to the device's own firmware and unread by ecosystems.
`BridgedDeviceBasicInformation`, which ecosystems *do* read per endpoint, declares that the
endpoint's functionality is bridged from a non-Matter technology — false for a natively
mirrored device. Ecosystems keep display names on their side regardless, and Home Assistant
names every endpoint from endpoint 0's NodeLabel, so nothing is lost by leaving the
per-endpoint name to the cloud until Matter offers a mechanism ecosystems consume.

### 4.5 Unmapped policy — both directions

- Common → Matter: custom params produce nothing on the fabric (§3.4).
- Matter → common: unknown clusters produce nothing (optional opaque diagnostic blob,
  invisible to normal clients). New vocabulary + mapping entries ship as a **table
  version bump**; derived configs enrich on re-derivation.

---

### 4.6 Distribution, versioning and primitive negotiation

**Distribution: copies, not dependencies.** The mapping repo is the canonical home of the
*language*: the JSON library and profiles, the JSON Schemas, the validator (an importable
Python package), the Python reference implementation of every primitive, the golden
vectors, and the emitter of the consumer-neutral *runtime JSON*. Consumers vendor what they
need — the library, the vectors, and a copy of the validator — stamped in `mapping/SOURCE`
(source commit, `mapping_version`, per-file hashes) and guarded in CI by a drift check
against those hashes. No submodules, no packages: consumers move at their own cadence.
What each consumer *emits* from the copy is its own business, kept next to the code that
consumes it: the firmware's C emitter mirrors the engine's table layout field by field, so
it lives in the firmware repo and runs at build time (an ESP-IDF build always has Python);
the phone SDK's build has no Python, so it commits the runtime JSON the shared emitter
produced when the copy was refreshed. A headless controller reads that same runtime JSON at
run time, delivered by the cloud.

**Two consumption forms of one vocabulary.** Constrained end devices *lower* from tables
generated per profile (a profile names the device types a product ships; unreferenced
vocabulary is dropped, and the engine compiles only the primitives the tables reference).
Raising consumers — the phone app and headless controllers — *interpret the runtime JSON*
(the resolved mapping with Matter ids as integers, notes stripped, a content hash, and
`requires`). Interpretation is what lets a controller support a device type it has never
seen: new vocabulary reaches it as data.

**Data vs code.** Everything device-type-specific is data — capabilities, rules, optional
params, enum pairs/aliases, linear ranges, `bounds_from`, `defaults`, composite wiring,
`write_as` payloads. Only the *enumerated primitives* are code: transform kinds
(identity, linear, kelvin_mireds, enum_map, scale, string; planned string_enum_map) and
composite conversions (hsv_xy; more with the thermostat wave). Adding a device type that
reuses existing primitives needs no consumer code change; adding a primitive does, in
every consumer.

**Negotiation.** The runtime JSON's `requires` block names the primitives it uses. A
consumer compares it with the set it implements before use: a cloud-delivered mapping a
controller cannot fully interpret is detected up front, and the controller either keeps its
previous mapping or degrades — capabilities needing an unsupported primitive are skipped
(the cluster-level floor of §4.3 still yields params; §4.5's opaque blob covers the rest).

**Determinism.** All primitives have a Python reference implementation in the mapping repo
with C-exact semantics (truncating division, round-half-away-from-zero, int32 wrap), and
the mapping repo emits golden vectors from it. Firmware and SDK assert bit-exact agreement
with those vectors in their own test suites, so a device lowered by firmware and raised by
any consumer — app or controller, at any mapping version — agrees on every value, and two
reporters raising the same third-party device produce identical derived configs (§3.6).

`mapping_version` is the contract version; the cloud must accept derived configs produced
from any supported version, and derived configs are re-raised when the mapping updates
(§3.5).

## 5. Worked example: CCT light

### 5.1 Native node config (RainMaker Neo firmware) — unchanged shape

```json
{
  "node_id": "<node id>",
  "config": {
    "data_model": "default", "model_version": "1.0",
    "origin": { "type": "native" },
    "info": { "name": "CCT Bulb", "fw_version": "1.0", "model": "bulb-cct-1" },
    "devices": [{
      "id": "Light", "type": "esp.device.lightbulb", "primary": "Power",
      "params": [
        { "id": "Power",      "type": "esp.param.power",      "data_type": "bool",
          "properties": ["read","write"], "ui_type": "esp.ui.toggle" },
        { "id": "Brightness", "type": "esp.param.brightness", "data_type": "int",
          "properties": ["read","write"], "bounds": {"min":0,"max":100,"step":1},
          "ui_type": "esp.ui.slider" },
        { "id": "CCT",        "type": "esp.param.cct",        "data_type": "int",
          "properties": ["read","write"], "bounds": {"min":2700,"max":6500,"step":100},
          "ui_type": "esp.ui.slider" },
        { "id": "Identify",   "type": "esp.param.identify",   "data_type": "int",
          "properties": ["write"] }
      ]
    }]
  }
}
```

### 5.2 Lowering table (firmware, when Matter enabled)

| Common model | Matter | Transform |
|---|---|---|
| params {power, brightness, cct} present | endpoint device type **0x010C** Color Temperature Light | — |
| `esp.param.power` | On/Off `0x0006` / `OnOff`; writes → On/Off/Toggle commands | 1:1 |
| `esp.param.brightness` | Level Control `0x0008` / `CurrentLevel`; writes → `MoveToLevelWithOnOff` | 0–100 ↔ 1–254 linear |
| `esp.param.cct` | Color Control `0x0300` (CT feature) / `ColorTemperatureMireds`; writes → `MoveToColorTemperature`; bounds → `ColorTempPhysicalMin/MaxMireds` | kelvin ↔ mireds |
| `esp.param.identify` | Identify `0x0003`; `Identify` command ↔ param write | seconds 1:1 |
| — (synthesized) | Descriptor, Groups, Basic Information | internal, never surfaced |

### 5.3 State flow examples

- **Cloud set-param** `{"Light":{"CCT":4000}}` → firmware updates canonical state →
  reports shadow `state.reported.params.Light.CCT = 4000` → mirror pushes
  `ColorTemperatureMireds = 250` to the fabric (no echo back to cloud... the mirror is
  a *reflection* of the one canonical store, with echo suppression both ways).
- **Apple Home** sends `MoveToColorTemperature(285 mireds)` → mirror raises to
  `CCT = 3509` → canonical store updates → shadow report to cloud → app UI updates.
  Transition timing executes on-device; the common model sees the settled state.

### 5.4 Derived config — third-party CCT bulb onboarded by the app

The app commissions the bulb, walks its tree (endpoint 1, device type 0x010C, clusters
0x0006/0x0008/0x0300 with CT feature), and the raising engine emits:

```json
{
  "node_id": "<cloud-assigned id>",
  "config": {
    "data_model": "default", "model_version": "1.0",
    "origin": { "type": "derived", "source": "matter", "mapping_version": "1.0" },
    "info": { "name": "Kitchen Bulb", "model": "<from Basic Information cluster>" },
    "devices": [{
      "id": "Light-EP1", "type": "esp.device.lightbulb", "primary": "Power",
      "params": [
        { "id": "Power",      "type": "esp.param.power",      "data_type": "bool",
          "properties": ["read","write"], "ui_type": "esp.ui.toggle" },
        { "id": "Brightness", "type": "esp.param.brightness", "data_type": "int",
          "properties": ["read","write"], "bounds": {"min":0,"max":100,"step":1},
          "ui_type": "esp.ui.slider" },
        { "id": "CCT",        "type": "esp.param.cct",        "data_type": "int",
          "properties": ["read","write"],
          "bounds": {"min":2000,"max":6535,"step":1},
          "ui_type": "esp.ui.slider" }
      ]
    }]
  }
}
```

(Bounds read from the bulb's `ColorTempPhysicalMin/MaxMireds`, converted to kelvin.
No Identify param if the product decision is to hide it for derived nodes, even though
the cluster exists — vocabulary is curated, not imported.)

The app registers this config with the cloud and relays state; the bulb renders in the
app from this config alone, including when it is unreachable over Matter.

---

## 6. Impact analysis

### 6.1 Firmware

The data model changes are small; the architectural change is positioning Matter as a
mirror.

1. **No alternative Matter data model.** The SDK carries a single data model (already
   the case in this repository); a Matter pass-through data model (endpoint/cluster
   config emission, TLV command handling toward the cloud) is explicitly not
   introduced.
2. **New lowering module** (Matter mirror component): walks devices/params at init and
   constructs the esp-matter endpoint/cluster tree from the mapping table (compiled-in
   C form). This absorbs the tree-building that apps otherwise do manually with
   esp-matter.
3. **Bidirectional mirror** on the existing state-changes / update-id plumbing. The
   param store is canonical; the mirror is strictly a reflector, and must provide:
   - *Self-write marking*: the mirror ignores attribute-change notifications caused
     by its own attribute writes, so a downward push is never raised back up.
   - *Same-value suppression* on the upward path: a raised value equal to the current
     param value produces no update and no report.
   - *Transition settling*: timed Matter transitions update attributes step-by-step;
     the mirror must not raise intermediate steps. Defer the upward raise while a
     transition is in flight (e.g., Level Control `RemainingTime` non-zero) and
     report the settled value, with a periodic keepalive for long-running
     transitions. MQTT budgeting remains a safety net, never the rate limiter.
4. **Vocabulary hardening**: standard param creators already match the spec (CCT is
   int, kelvin, bounds `{2700, 6500, 100}`); add an identify param creator + identify
   behavior hook; enforce cardinality and mandatory bounds when params are added to a
   device.
5. **App-facing API: unchanged** for the pure RainMaker Neo case; Matter case shrinks (no
   manual esp-matter tree construction).

### 6.2 Cloud backend

The cloud is **already effectively single-model**: the config schema declares a
`data_model` field and Matter endpoint structures, but no code path branches on them —
Matter configs are opaque pass-through storage, and all active logic (params shadow,
config REST, schedules/triggers, voice) is built exclusively on the
`devices[]/params[]` model. Alexa and GVA discovery iterate only `devices[]`, so Matter
nodes are already invisible to voice today. Adopting the common model *removes* that
gap rather than creating migration work.

Required changes:

1. **Schema validation (new)**: node config is currently stored as-is with no
   validation. Add validation for `data_model: "default"`, `model_version`, and the
   standard-param normative rules. Remove the Matter endpoint structures from the
   config schema.
2. **Node model flag** (`rmng` vs `rmng+matter`) in GET node responses — new field,
   sourced from registration/association metadata.
3. **Alexa/GVA**: no structural change (already default-model-only); their existing
   param-type → capability/trait tables become consumers of the shared vocabulary
   spec.
4. **Net-new: derived-node registration path** — the only genuinely new build. Today a
   node can only be created with a device certificate (node id is derived from the
   cert CN) and only the node itself may write its config. An app/bridge-authored node
   needs: (a) cert-less node creation bound to the registering user/bridge, (b) config
   write rights for the reporter, (c) shadow publish rights for state relay (IoT
   policy work in the assume-role flow). Partial precedent exists: the association
   flow already recognizes cert-less "pure Matter nodes", and the assisted
   commissioning controller already creates IoT Things when pairing Matter nodes to a
   fabric. Requires the reporter-trust model of §8.

### 6.3 Phone app SDK (TypeScript base SDK)

**There is no second data model to remove.** The base SDK carries no
endpoint/cluster/TLV parsing; the `data_model` field on its node-config type is
declared but never read — a reserved discriminator, inert today. Matter is already
architected as an external "satellite" SDK plugging into existing abstractions:
subscription channels and pluggable transports, all normalizing into one canonical
state shape (`{ device: { param: value } }`).

Required changes (all additive, at existing seams):

1. **Mapping library + raising engine (TS port)** — new package, consumed by the
   Matter satellite SDK. Raising output feeds the SDK's single centralized
   config-transform utility — no scattered changes.
2. **Matter transport/channel** (in the satellite SDK): implements the transport
   interface by translating `{device:{param:value}}` → cluster commands via the
   mapping library, and a `matter` subscription channel producing the canonical
   node-update shape. The delegated transport handler already supports custom
   transport modes — Matter slots into the per-node transport order without redesign.
3. **Derived-node flows**: commissioning-record ↔ cloud-node correlation, config
   registration and state relay against the new cloud APIs (§6.2, item 4).
4. **Config type additions**: `model_version`, `origin` fields in the config types
   and transform; trivial.

---

## 7. Deliverables & phasing

| # | Artifact | Depends on |
|---|---|---|
| 1 | Common model spec (this doc, hardened §3) | — |
| 2 | Mapping table v1.0 (CCT light entries end-to-end) + schema | 1 |
| 3 | Firmware: lowering module + mirror; delete matter data model | 2 |
| 4 | TS raising engine + mapping library; app derived-node flow | 2 |
| 5 | Cloud: schema validation, node model flag, derived-node registration | 1 |
| 6 | Second-wave vocabulary: thermostat + door lock (stress tests: mode
      interactions, command-heavy semantics, credentials) — spec on paper early | 2 |

Acceptance criterion for goal 2: a **third-party** CCT bulb (not our firmware) raised,
registered, rendered offline, and controlled via a reporter — not just our own device
round-tripping.

## 8. Open questions

1. Reporter trust model and lifecycle for derived nodes (cloud).
2. Headless controllers: policy for acting on cluster-level-matched (vs device-type-
   matched) capabilities. Field reserved (§4.3); spec deferred. (The *mechanism* for
   headless raising — runtime interpretation of the cloud-delivered runtime JSON with
   primitive negotiation — is in §4.6; only the trust policy remains open.)
3. Custom params + local latency: whether/when to reintroduce local-ctrl transport.
4. Whether derived nodes surface Identify (curation call per §5.4).
5. Thermostat/lock vocabulary stress test outcomes may feed back into §4 schema
   (e.g., multi-attribute capabilities, enum-rich modes).

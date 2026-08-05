# osal/json

The JSON area of osal. It contains no sources or headers of its own: JSON is
provided by the managed Espressif components `json_generator` and `json_parser`,
and this area exists so that both platforms get the same two APIs on the same
include path.

## Public API

`json_generator.h` and `json_parser.h`, from the upstream components — a
streaming/flush-callback generator (`json_gen_str_start()`,
`json_gen_obj_set_*()`, ...) and a jsmn-backed parser (`json_parse_start()`,
`json_obj_get_*()`, ...). Both are exposed as part of osal's public interface,
so anything linking osal can `#include` them directly.

## Per-platform implementations

There is only one implementation; the difference is purely how it is obtained.

- ESP-IDF: `espressif/json_generator` (`^1.2.0`) and `espressif/json_parser`
  (`^1.0.3`) are declared in [`../idf_component.yml`](../idf_component.yml) and
  listed as public `REQUIRES` of the osal component, so the component manager
  fetches and builds them.
- POSIX: the same versioned dependencies are imported at configure time by
  `rmng_idf_components_import()` (`cmake/esp_component_manager.cmake`). The osal
  `CMakeLists.txt` then compiles `json_generator.c` and `json_parser.c` straight
  into the osal library and adds the generator, parser and `jsmn` include
  directories to osal's public includes — `jsmn` contributes headers only, used
  by `json_parser`.

## Build gating

Always built. There is no `OSAL_INCLUDE_*` switch.

## Notes

Because the POSIX path resolves the dependencies through the ESP component
manager, configuring a POSIX build needs the component registry to be reachable
(or an already-populated managed-components cache).

[`test-json-common/`](test-json-common/) holds the unit tests, which build for
both platforms and are added when testing is enabled.

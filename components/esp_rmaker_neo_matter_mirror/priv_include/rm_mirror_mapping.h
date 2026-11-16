/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_mapping.h
 * @brief The mapping tables' types: what the engine reads, and the shape
 *        scripts/gen_mapping_table.py must fill.
 *
 * Fields are grouped by alignment class - pointers, then 32-bit values, then the
 * byte-wide enums, counts and flags - because C does not reorder them. The assertions
 * at the end of each type hold the packing to that.
 */

#ifndef __RM_MIRROR_MAPPING_H__
#define __RM_MIRROR_MAPPING_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define RM_MIRROR_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define RM_MIRROR_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

/* Each type below asserts its own size against the sum of its fields, allowing only the
 * trailing padding the alignment forces. A field added out of class shows up as internal
 * padding and fails the assertion; the sums are per pointer width, so the 32-bit target
 * and the 64-bit host tests both hold. */

/* Packed so a table field costs a byte rather than an int, without giving up the
 * enum type at the call sites. */
#define RM_MIRROR_PACKED_ENUM __attribute__((packed))

#define RM_MIRROR_COMPOSITE_MAX_PARAMS 4
#define RM_MIRROR_COMPOSITE_MAX_ATTRS  4

/** Value transform between the RainMaker Neo domain and the Matter domain */
typedef enum {
    RM_MIRROR_XFORM_IDENTITY,
    RM_MIRROR_XFORM_LINEAR,
    RM_MIRROR_XFORM_KELVIN_MIREDS,
    RM_MIRROR_XFORM_ENUM_MAP,
    RM_MIRROR_XFORM_STRING,
    RM_MIRROR_XFORM_SCALE,
} RM_MIRROR_PACKED_ENUM rm_mirror_xform_type_t;

/** Matter-domain value type: an attribute's wire type, or a write_as argument's TLV width */
typedef enum {
    RM_MIRROR_VAL_NONE,
    RM_MIRROR_VAL_BOOL,
    RM_MIRROR_VAL_U8,
    RM_MIRROR_VAL_U16,
    RM_MIRROR_VAL_U32,
    RM_MIRROR_VAL_I8,
    RM_MIRROR_VAL_I16,
    RM_MIRROR_VAL_I32,
    RM_MIRROR_VAL_I64,
    RM_MIRROR_VAL_ENUM8,
    RM_MIRROR_VAL_NULLABLE_U8,
    RM_MIRROR_VAL_NULLABLE_U16,
    RM_MIRROR_VAL_NULLABLE_I64,
    RM_MIRROR_VAL_STRING,
} RM_MIRROR_PACKED_ENUM rm_mirror_val_type_t;

/** How a StartUp* attribute's stored value becomes the boot state (semantics in the engine) */
typedef enum {
    RM_MIRROR_STARTUP_NONE, /* the capability declares no startup policy */
    RM_MIRROR_STARTUP_PASSTHROUGH,
    RM_MIRROR_STARTUP_MIN_CLAMP,
    RM_MIRROR_STARTUP_TOGGLE_PREVIOUS,
} RM_MIRROR_PACKED_ENUM rm_mirror_startup_policy_t;

/** Composite conversion between N params and M attributes (enumerated, no scripting) */
typedef enum {
    RM_MIRROR_COMPOSITE_CONV_HSV_XY,
} RM_MIRROR_PACKED_ENUM rm_mirror_composite_conv_t;

/** One rmng-domain <-> matter-domain pair of an enum_map transform */
typedef struct {
    int32_t rmng;
    int32_t matter;
} rm_mirror_enum_pair_t;

/** Value transform. Only the payload the type names is populated; the rest of the
 *  union is absent, which is what keeps an identity transform free. */
typedef struct {
    rm_mirror_xform_type_t type;
    uint8_t n_pairs;
    uint8_t n_aliases;
    union {
        struct {                     /* RM_MIRROR_XFORM_LINEAR */
            int32_t in_min;          /* rmng-domain range */
            int32_t in_max;
            int32_t out_min;         /* matter-domain range */
            int32_t out_max;
        } linear;
        struct {                     /* RM_MIRROR_XFORM_ENUM_MAP */
            const rm_mirror_enum_pair_t *pairs;   /* bidirectional */
            const rm_mirror_enum_pair_t *aliases; /* extra matter -> rmng foldings */
        } enum_map;
        int32_t scalar;              /* max_len (STRING) or factor (SCALE) */
    } u;
} rm_mirror_xform_t;

/** One field of a write_as command payload */
typedef struct {
    int32_t literal;
    uint8_t field_id;          /* TLV context tag */
    rm_mirror_val_type_t type;
    uint8_t from_value : 1;    /* the transformed matter value, ignoring literal */
} rm_mirror_cmd_arg_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_cmd_arg_t) <= 4 + 3 + sizeof(void *) - 1,
                        "rm_mirror_cmd_arg_t has internal padding");

/** A command to invoke in place of writing the matched attribute */
typedef struct {
    const rm_mirror_cmd_arg_t *args;
    uint32_t command_id;
    int32_t when;
    uint8_t n_args;
    uint8_t has_when : 1;            /* only for this matter-domain value (e.g. On vs Off) */
} rm_mirror_write_cmd_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_write_cmd_t) <= sizeof(void *) + 8 + 2 + sizeof(void *) - 1,
                        "rm_mirror_write_cmd_t has internal padding");

/** An attribute written alongside the matched one, with the same value */
typedef struct {
    uint32_t attribute_id;           /* on the capability's cluster */
    rm_mirror_val_type_t value_type;
} rm_mirror_shadow_attr_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_shadow_attr_t) <= 4 + 1 + sizeof(void *) - 1,
                        "rm_mirror_shadow_attr_t has internal padding");

/** One standard-param capability and its Matter binding */
typedef struct {
    const char *param_type;                      /* e.g. "esp.param.cct" */
    const rm_mirror_write_cmd_t *write_cmds;     /* outbound writes invoke these; NULL = write the attribute */
    const rm_mirror_write_cmd_t *seed_cmds;      /* invoked once after the reseed, for a server holding its own copy */
    const rm_mirror_shadow_attr_t *shadow_attrs; /* written with the same value as the attribute */
    const char *report_as;                       /* port report hook kind; NULL = write the attribute */
    uint32_t cluster_id;
    uint32_t feature_map;                        /* cluster FeatureMap bits to enable */
    uint32_t attribute_id;
    rm_mirror_xform_t xform;                     /* rmng -> matter; inverse applied for matter -> rmng */
    uint32_t startup_attr;                       /* attribute holding the boot policy; 0 = none */
    int32_t startup_min;                         /* RM_MIRROR_STARTUP_MIN_CLAMP: what a stored 0 means */
    uint32_t bounds_min_attr;                    /* attribute the param's min bound lands on (transformed); 0 = none */
    uint32_t bounds_max_attr;                    /* attribute the param's max bound lands on (transformed); 0 = none */
    rm_mirror_val_type_t value_type;             /* wire type of attribute_id */
    uint8_t n_write_cmds;
    uint8_t n_seed_cmds;
    uint8_t n_shadow_attrs;
    rm_mirror_startup_policy_t startup_policy;
    rm_mirror_val_type_t bounds_value_type;      /* wire type of the bounds attributes */
    uint8_t startup_skip_on_sw_update : 1;       /* an OTA restart is not a power cycle */
    uint8_t deferred_persistence : 1;            /* changes fast enough that per-write flash storage hurts */
    uint8_t node_scoped : 1;                     /* bound on endpoint 0 (node level), not per device endpoint */
    uint8_t rule_gated : 1;                      /* lowers only when listed in the matched rule's (optional_)params */
} rm_mirror_capability_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_capability_t) <= 6 * sizeof(void *) + 48 + 7 + sizeof(void *) - 1,
                        "rm_mirror_capability_t has internal padding");

/** N params <-> M attributes derived binding (e.g. hue+saturation <-> CurrentX/CurrentY) */
typedef struct {
    const char *id;                          /* e.g. "xy_from_hs" */
    const char *const *param_types;          /* ordered per the conversion's contract */
    const uint32_t *attribute_ids;           /* ordered per the conversion's contract */
    const rm_mirror_val_type_t *value_types; /* wire type per attribute_ids entry */
    uint32_t cluster_id;
    uint32_t feature_map;                    /* cluster FeatureMap bits to enable */
    uint8_t n_params;
    uint8_t n_attributes;
    rm_mirror_composite_conv_t conversion;
    uint8_t deferred_persistence : 1;        /* derived attributes ramp during transitions */
} rm_mirror_composite_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_composite_t) <= 4 * sizeof(void *) + 8 + 4 + sizeof(void *) - 1,
                        "rm_mirror_composite_t has internal padding");

/** A Matter device type an endpoint carries in addition to the rule's own */
typedef struct {
    const uint32_t *mandatory_clusters; /* this device type's own mandatory clusters */
    uint32_t device_type_id;
    uint8_t device_type_version;
    uint8_t n_mandatory_clusters;
} rm_mirror_extra_devtype_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_extra_devtype_t) <= sizeof(void *) + 4 + 2 + sizeof(void *) - 1,
                        "rm_mirror_extra_devtype_t has internal padding");

/** Param-subset rule resolving a device to a Matter device type */
typedef struct {
    const char *const *param_types;                      /* all must be present on the device */
    const char *const *optional_params;                  /* gate-listed but not required for the match */
    const uint32_t *mandatory_clusters;                  /* created even without a bound param */
    const rm_mirror_extra_devtype_t *extra_device_types; /* declared on the same endpoint */
    uint32_t matter_device_type_id;
    uint8_t n_params;
    uint8_t n_optional_params;
    uint8_t matter_device_type_version;
    uint8_t n_mandatory_clusters;
    uint8_t n_extra_device_types;
} rm_mirror_devtype_rule_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_devtype_rule_t) <= 4 * sizeof(void *) + 4 + 5 + sizeof(void *) - 1,
                        "rm_mirror_devtype_rule_t has internal padding");

/** One RainMaker Neo device type and the rules that resolve it */
typedef struct {
    const char *device_type;               /* e.g. "esp.device.lightbulb" */
    const rm_mirror_devtype_rule_t *rules; /* ordered most-specific-first */
    uint8_t n_rules;
} rm_mirror_device_entry_t;
RM_MIRROR_STATIC_ASSERT(sizeof(rm_mirror_device_entry_t) <= 2 * sizeof(void *) + 1 + sizeof(void *) - 1,
                        "rm_mirror_device_entry_t has internal padding");

extern const rm_mirror_capability_t rm_mirror_capabilities[];
extern const size_t rm_mirror_n_capabilities;
extern const rm_mirror_composite_t rm_mirror_composites[];
extern const size_t rm_mirror_n_composites;
extern const rm_mirror_device_entry_t rm_mirror_device_entries[];
extern const size_t rm_mirror_n_device_entries;
extern const char *const rm_mirror_mapping_version;

/* Last, because it asserts the types above against the widths gen_mapping_table.py fills
 * them with, and carries the RM_MIRROR_USES_* macros the engine gates on. Including this
 * header is enough; the include guards make the pair order-independent. */
#include "rm_mirror_mapping_gen.h"

#ifdef __cplusplus
}
#endif

#endif /* __RM_MIRROR_MAPPING_H__ */

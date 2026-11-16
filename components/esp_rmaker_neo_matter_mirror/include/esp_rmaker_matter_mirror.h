/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_matter_mirror.h
 * @brief Matter mirror: derive a Matter data model from the RainMaker Neo data model
 *        and keep the two in sync.
 *
 * The RainMaker Neo param store remains the single canonical state store; the mirror
 * lowers the node's devices/params into an esp-matter endpoint/cluster tree
 * (per the compiled-in mapping tables) and reflects state changes both ways:
 * cloud/app/schedule-originated changes appear on the Matter fabric, and
 * Matter controller commands are delivered to the device's regular RainMaker Neo write
 * callback (with source ESP_RMAKER_REQ_SRC_EXTERNAL).
 */

#ifndef __ESP_RMAKER_MATTER_MIRROR_H__
#define __ESP_RMAKER_MATTER_MIRROR_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "esp_rmaker_credentials_provider.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_error_types.h"
#include "esp_rmaker_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What happens to the commissioning window when the node's last fabric is removed */
typedef enum {
    /** Re-open the commissioning window, so the node can be commissioned again */
    ESP_RMAKER_MATTER_MIRROR_COMMISSIONING_WINDOW_REOPEN = 0,
    /** Leave it closed. The node stays uncommissionable until it reboots or is
     * factory reset: with CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART, CHIP re-opens the
     * window at boot while the node has no fabric. The node keeps running: its
     * RainMaker side, including the cloud association, is untouched by a fabric
     * being removed. */
    ESP_RMAKER_MATTER_MIRROR_COMMISSIONING_WINDOW_NEVER,
} esp_rmaker_matter_mirror_commissioning_window_policy_t;

/** Matter mirror configuration */
typedef struct {
    /** true: the application calls esp_matter::start() after enable() returns, and adds or removes no device while it
     *  runs. false (default): enable() starts Matter. */
    bool defer_matter_start;
    /** What to do when the last fabric is removed. Applies in every onboarding
     * mode: CHIP opens a commissioning window by itself only at boot, while the
     * node has no fabric, so a node that loses its last fabric at runtime is
     * left uncommissionable in every mode. */
    esp_rmaker_matter_mirror_commissioning_window_policy_t commissioning_window_policy;
} esp_rmaker_matter_mirror_config_t;

/**
 * @brief Enable the Matter mirror for a node.
 *
 * Builds the Matter node/endpoint/cluster tree from the RainMaker Neo node's devices
 * and params, seeds attribute values from current param values, registers the
 * state sync hooks, and (unless deferred) starts the Matter stack.
 *
 * Call after all devices and params have been added to @p node and before
 * esp_rmaker_start().
 *
 * @param[in] node Node handle.
 * @param[in] config Configuration. NULL for defaults.
 *
 * @return ESP_RMAKER_OK on success, error code otherwise.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_enable(const esp_rmaker_node_t *node,
        const esp_rmaker_matter_mirror_config_t *config);

/**
 * @brief Mirror a device added after enable(); its node ID and device ID keep its endpoint ID.
 * @note Add @p device to its node first. Not from a write callback or with the CHIP stack lock held.
 * @note Right after a removal, it can wait about 4 s.
 * @note Experimental: not yet tested on hardware, and the API may change.
 *
 * @return ESP_RMAKER_INVALID_ARG when @p device is in no node or its node has no node ID,
 *         ESP_RMAKER_NOT_INITIALIZED before enable() returned, ESP_RMAKER_ALREADY_EXISTS when mirrored,
 *         ESP_RMAKER_NOT_SUPPORTED when the mapping or the port cannot mirror the device,
 *         ESP_RMAKER_INVALID_STATE when the caller holds the CHIP stack lock, ESP_RMAKER_TIMEOUT when the
 *         device stays bound (remove it, then add again).
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_add_device(const esp_rmaker_device_t *device);

/**
 * @brief Stop mirroring a device: the mirror unbinds it first, then destroys its Matter endpoint.
 * @note Call before esp_rmaker_node_remove_device(), not from a write callback or with the CHIP stack lock held.
 * @note Experimental: not yet tested on hardware, and the API may change.
 *
 * @return ESP_RMAKER_NOT_INITIALIZED before enable() returned, ESP_RMAKER_NOT_FOUND when not mirrored,
 *         ESP_RMAKER_TIMEOUT when the device stays mirrored, ESP_RMAKER_INVALID_STATE when the caller holds the
 *         CHIP stack lock.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_remove_device(const esp_rmaker_device_t *device);

/**
 * @brief Set the Matter Reachable value of a bridged device, now and for each later add of it.
 * @note Needs CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_BRIDGE. Add @p device to its node first.
 * @note Experimental: not yet tested on hardware, and the API may change.
 *
 * @return ESP_RMAKER_NOT_INITIALIZED before enable() returned, ESP_RMAKER_NOT_SUPPORTED when there is no bridge or
 *         @p device is in the mirrored node, ESP_RMAKER_INVALID_STATE when the caller holds the CHIP stack lock.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_set_reachable(const esp_rmaker_device_t *device, bool reachable);

/**
 * @brief Build a cluster the port has no factory for.
 *
 * @param[in] endpoint_id Endpoint to create the cluster on. Resolve it to an
 *            esp-matter handle with esp_matter::endpoint::get().
 * @param[in] cluster_id Matter cluster id, from the mapping.
 * @param[in] feature_map Cluster FeatureMap bits the mapping asks for; 0 when none.
 *
 * @return ESP_RMAKER_OK when the cluster exists on the endpoint afterwards. Must be
 *         idempotent: it is called once per bound param on the endpoint.
 */
typedef esp_rmaker_error_t (*esp_rmaker_matter_mirror_cluster_create_t)(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t feature_map);

#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS
/**
 * @brief Register a factory for clusters the port does not build itself.
 *
 * Needed when a product's own mapping library uses a cluster with no factory
 * under @c src/port/clusters/. Consulted only after the compiled-in factories,
 * so it cannot override them. Call before esp_rmaker_matter_mirror_enable(),
 * which is where the tree is built.
 *
 * The build otherwise fails on a mapping that needs a cluster the port cannot
 * build; enable @c CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS to
 * turn that check off in favour of this hook.
 *
 * @param[in] factory Factory to call. Must not be NULL.
 *
 * @return ESP_RMAKER_OK, ESP_RMAKER_INVALID_ARG for NULL, ESP_RMAKER_INVALID_STATE once the
 *         mirror is enabled.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_register_cluster_factory(esp_rmaker_matter_mirror_cluster_create_t factory);

/** @brief Remove the factory set by esp_rmaker_matter_mirror_register_cluster_factory(). */
esp_rmaker_error_t esp_rmaker_matter_mirror_unregister_cluster_factory(void);
#endif /* CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS */

/**
 * @brief Deliver a value the mapping routes through @c report_as instead of an
 *        attribute write.
 *
 * Called from the mirror's task or from the task that adds a device, never with the CHIP stack lock held.
 *
 * @param[in] endpoint_id Endpoint the value belongs to.
 * @param[in] cluster_id Matter cluster id, from the mapping.
 * @param[in] attribute_id Attribute the mapping matched, for handlers that
 *            serve several values of one cluster.
 * @param[in] kind The capability's @c report_as.kind string, verbatim from the mapping
 *            library that declares it. The shipped library declares no kinds.
 * @param[in] value Matter-domain value, after the capability's transform. The engine
 *            carries every value as int32_t, whatever wire type the mapping declares.
 *
 * @return ESP_RMAKER_OK when the value reached the cluster.
 */
typedef esp_rmaker_error_t (*esp_rmaker_matter_mirror_report_t)(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, const char *kind, int32_t value);

/**
 * @brief Register the handler for the mapping's @c report_as kinds.
 *
 * A cluster that owns its value - Electrical Energy Measurement notifies a
 * struct rather than exposing a writable attribute - is reported through this
 * hook, by the same code that builds the cluster. One handler serves every kind:
 * the kind arrives per call, so this API takes none. Values pushed with no
 * handler registered are dropped with an error.
 *
 * @param[in] handler Handler to call. Must not be NULL.
 *
 * @return ESP_RMAKER_OK, ESP_RMAKER_INVALID_ARG for NULL.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_register_report_handler(esp_rmaker_matter_mirror_report_t handler);

/** @brief Remove the handler set by esp_rmaker_matter_mirror_register_report_handler(). */
esp_rmaker_error_t esp_rmaker_matter_mirror_unregister_report_handler(void);

/**
 * @brief What a device is being asked to do to identify itself.
 *
 * The Identify cluster's EffectIdentifier values (0x0003, TriggerEffect), plus the mirror's own
 * marker for a plain Identify command, which names no effect. A controller may send an effect
 * this enum does not list (manufacturer-specific ids are allowed): handle those in the default
 * branch, or decline them and let the mirror fall back to the identify param.
 */
typedef enum {
    ESP_RMAKER_MATTER_MIRROR_EFFECT_BLINK          = 0x00, /**< A single blink */
    ESP_RMAKER_MATTER_MIRROR_EFFECT_BREATHE        = 0x01, /**< Cycle 15 s, or as long as asked */
    ESP_RMAKER_MATTER_MIRROR_EFFECT_OKAY           = 0x02, /**< Acknowledgement, e.g. two flashes */
    ESP_RMAKER_MATTER_MIRROR_EFFECT_CHANNEL_CHANGE = 0x0b, /**< Channel change, e.g. orange for 8 s */
    ESP_RMAKER_MATTER_MIRROR_EFFECT_FINISH         = 0xfe, /**< Complete the current sequence, then stop */
    ESP_RMAKER_MATTER_MIRROR_EFFECT_STOP           = 0xff, /**< Stop as soon as possible */
    ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN      = 0xFFFF, /**< An Identify command: no effect named */
} esp_rmaker_matter_mirror_identify_effect_t;

/**
 * @brief Render a Matter identify request on the device.
 *
 * Called from the Matter thread for Identify and TriggerEffect alike, so it must not block.
 *
 * @param[in] device_id RainMaker Neo device behind the endpoint.
 * @param[in] effect_id What to render, @c ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN for an Identify
 *            command (and for the stop that follows one, which carries @p seconds 0).
 * @param[in] effect_variant TriggerEffect's EffectVariant, 0 for a plain Identify.
 * @param[in] seconds IdentifyTime for a plain Identify, the effect's nominal duration otherwise,
 *            0 to stop.
 *
 * @return ESP_RMAKER_OK when the device rendered the request. Anything else falls back to writing
 *         the device's esp.param.identify param with @p seconds, from the mirror's task rather
 *         than the Matter thread.
 */
typedef esp_rmaker_error_t (*esp_rmaker_matter_mirror_identify_t)(const char *device_id,
        esp_rmaker_matter_mirror_identify_effect_t effect_id, uint8_t effect_variant, int32_t seconds);

/**
 * @brief Register the handler for Matter identify requests.
 *
 * Only needed by a device that renders the Identify effects itself. Without a handler - or when
 * the handler declines a request - the mirror writes that param, which stays the path for
 * cloud-originated identify either way.
 *
 * @param[in] handler Handler to call. Must not be NULL.
 *
 * @return ESP_RMAKER_OK, ESP_RMAKER_INVALID_ARG for NULL.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_register_identify_handler(esp_rmaker_matter_mirror_identify_t handler);

/** @brief Remove the handler set by esp_rmaker_matter_mirror_register_identify_handler(). */
esp_rmaker_error_t esp_rmaker_matter_mirror_unregister_identify_handler(void);

/**
 * @brief Register the mirror's Matter-stack hooks.
 * @note Not with the CHIP stack lock held, e.g. from a CHIP event handler.
 *
 * Only needed with @c defer_matter_start: call this right after starting Matter yourself.
 * esp_rmaker_matter_mirror_enable() calls it itself when it starts Matter.
 *
 * The hooks cannot be registered before the Matter stack is initialised (they allocate through the
 * CHIP platform allocator), which is why this is a separate call rather than part of enable(). Skip
 * it and a Matter-initiated factory reset will erase the Matter state without clearing the RainMaker
 * Neo side.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_STATE if the mirror is not enabled or the caller
 *         holds the CHIP stack lock, error code otherwise. Safe to call more than once.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_register_hooks(void);

/**
 * @def ESP_RMAKER_MATTER_MIRROR_RMNG_PROV_REQUIRED
 * @brief 1 if the application has to run RainMaker provisioning, 0 if Matter
 *        commissioning brings up the network itself.
 *
 * Guard the provisioning call with @c #if on this, rather than on the onboarding
 * Kconfig symbols: it keeps the mode in one place, and compiling the call out is
 * what lets the linker drop the provisioning stack in the Matter-first mode.
 */
#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ONBOARD_MATTER_FIRST
#define ESP_RMAKER_MATTER_MIRROR_RMNG_PROV_REQUIRED 0
#else
#define ESP_RMAKER_MATTER_MIRROR_RMNG_PROV_REQUIRED 1
#endif

/**
 * @def ESP_RMAKER_MATTER_MIRROR_COMMISSIONING
 * @brief 1 if the mirror manages the Matter commissioning lifecycle: the onboarding
 *        readiness gate, and re-opening the commissioning window when the last fabric
 *        is removed.
 *
 * Derived from the onboarding mode, since only the modes that commission over BLE have
 * a RainMaker agent to hold back and a CHIPoBLE teardown to wait for.
 */
#if defined(CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ONBOARD_MATTER_FIRST) \
    || defined(CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ONBOARD_CONCURRENT)
#define ESP_RMAKER_MATTER_MIRROR_COMMISSIONING 1
#else
#define ESP_RMAKER_MATTER_MIRROR_COMMISSIONING 0
#endif

#if ESP_RMAKER_MATTER_MIRROR_COMMISSIONING

/**
 * @brief Wait until onboarding is complete, i.e. the network is up, Matter
 *        commissioning has finished if it is the one bringing the network up, and
 *        CHIPoBLE has returned its memory to the heap.
 *
 * Call between esp_rmaker_matter_mirror_enable() and esp_rmaker_start(): the
 * RainMaker agent needs the heap CHIPoBLE holds during commissioning. Returns
 * immediately in modes where nothing is outstanding, so it is safe to call
 * unconditionally.
 *
 * @param[in] timeout_ms How long to wait, or 0 for
 *            CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_READY_TIMEOUT_MS.
 *
 * @return ESP_RMAKER_OK once ready, ESP_RMAKER_TIMEOUT if the timeout elapses first,
 *         ESP_RMAKER_INVALID_STATE if the Matter stack was never started.
 */
esp_rmaker_error_t esp_rmaker_matter_mirror_wait_ready(uint32_t timeout_ms);

/**
 * @brief Whether onboarding is complete, without blocking.
 *
 * @return true once esp_rmaker_matter_mirror_wait_ready() would return at once, false while
 *         something is outstanding or the Matter stack was never started.
 */
bool esp_rmaker_matter_mirror_is_ready(void);

#endif /* ESP_RMAKER_MATTER_MIRROR_COMMISSIONING */

/**
 * @brief Get credential providers backed by the Matter DAC.
 *
 * Serves the MQTT client certificate and key from the @c dac-cert / @c dac-key entries of the
 * chip-factory NVS namespace, and the node id from the DAC subject common name. The MQTT host and
 * the random bytes keep their defaults (the RainMaker namespace of the same factory partition), so
 * the factory image needs no copy of the DAC outside chip-factory.
 *
 * Pass the returned table to esp_rmaker_credentials_provider_override() before any init that reads
 * credentials.
 *
 * Requires Matter factory data on the device: the providers fail with ESP_RMAKER_NOT_FOUND if the
 * DAC entries are missing, and nothing falls back to the RainMaker credentials once installed.
 *
 * @return Provider table, owned by the mirror. Never NULL.
 */
const esp_rmaker_credentials_providers_t *esp_rmaker_matter_mirror_get_dac_credentials(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_MATTER_MIRROR_H__ */

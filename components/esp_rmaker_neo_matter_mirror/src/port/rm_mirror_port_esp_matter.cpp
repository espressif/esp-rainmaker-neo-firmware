/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_port_esp_matter.cpp
 * @brief esp-matter port of the mirror engine: table-driven cluster factory,
 *        attribute access, and the Matter-side callbacks.
 */

#include <algorithm>
#include <optional>
#include <string.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>
#include <esp_matter_attribute_utils.h>
#include <esp_matter_data_model_provider.h>

#include <esp_matter_providers.h>

#include <platform/DeviceInfoProvider.h>
#include <platform/DiagnosticDataProvider.h>
#ifdef CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
#include <platform/ESP32/ESP32DeviceInfoProvider.h>
#endif
#include <platform/ESP32/ESP32Config.h>
#include <platform/KeyValueStoreManager.h>
#include <platform/PlatformManager.h>
#include <access/SubjectDescriptor.h>
#include <app/AttributeValueDecoder.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/CommandHandler.h>
#include <app/ConcreteCommandPath.h>
#include <app/InteractionModelEngine.h>
#include <app/clusters/operational-credentials-server/OperationalCredentialsCluster.h>
#include <app/data-model/EncodableToTLV.h>
#include <app/data-model-provider/OperationTypes.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <lib/core/TLVReader.h>
#include <lib/core/TLVWriter.h>
#include <lib/support/TypeTraits.h>
#include <protocols/interaction_model/StatusCode.h>

#include "esp_rmaker_matter_mirror.h"
#include "esp_rmaker_system_ctrl.h"
#include "osal_ota.h"
#include "rm_mirror_engine.h"
#include "rm_mirror_internal.h"

using namespace esp_matter;

/* How long to let CHIP restart the system itself during a factory reset before finishing the job
 * here. A healthy Matter thread gets there in a couple of event-queue turns. */
#define RM_CHIP_RESET_GRACE_MS 3000

/* Encoded write_as payload: a handful of scalar fields in an anonymous TLV structure. */
#define RM_CMD_TLV_MAX 48

/* Encoded string attribute write: the longest string the mirror writes, plus TLV overhead. */
#define RM_STR_TLV_MAX (RM_MIRROR_NODE_LABEL_MAX_LEN + 8)

/* CSR + nonce + every vendor-reserved field must fit RESP_MAX (900 bytes), and the CSR alone takes
 * ~256 - a node id is an order of magnitude shorter than this budget. */
#define RM_CSR_VENDOR_DATA_MAX_LEN 128

static const char *TAG = "rm_mirror_port";

static node_t *__matter_node;

/* Set once the Matter-side hooks are registered, which also means the Matter stack is up. */
static bool __matter_hooks_registered;

/* Taken from the enable() config, applied when the last fabric is removed. */
static bool __reopen_window = true;

/* Sampled in enable(), before esp_rmaker_start() lets RainMaker OTA validate the image. */
static bool __boot_unverified_image;

/* Backs the ByteSpan the Operational Credentials cluster holds for the CSR's vendor-reserved1. */
static char *__csr_node_id;

/** Holds the CHIP stack lock once esp_matter::start() has returned; before that the lock may not exist. */
class RmMirrorStackLock {
public:
    RmMirrorStackLock()
    {
        if (esp_matter::is_started()) {
            mLock.emplace(portMAX_DELAY);
        }
    }

private:
    std::optional<lock::ScopedChipStackLock> mLock;
};

/* Value conversion ************************************************************/

/**
 * @brief Wrap the engine's int32 representation in the esp-matter value type the
 *        mapping declared for the attribute.
 */
static esp_matter_attr_val_t __attr_val(rm_mirror_val_type_t type, int32_t value)
{
    switch (type) {
    case RM_MIRROR_VAL_BOOL:
        return esp_matter_bool(value != 0);
    case RM_MIRROR_VAL_U8:
        return esp_matter_uint8((uint8_t)value);
    case RM_MIRROR_VAL_U32:
        return esp_matter_uint32((uint32_t)value);
    case RM_MIRROR_VAL_I8:
        return esp_matter_int8((int8_t)value);
    case RM_MIRROR_VAL_I16:
        return esp_matter_int16((int16_t)value);
    case RM_MIRROR_VAL_I32:
        return esp_matter_int32(value);
    case RM_MIRROR_VAL_ENUM8:
        return esp_matter_enum8((uint8_t)value);
    case RM_MIRROR_VAL_NULLABLE_U8:
        return esp_matter_nullable_uint8((uint8_t)value);
    case RM_MIRROR_VAL_NULLABLE_U16:
        return esp_matter_nullable_uint16((uint16_t)value);
    case RM_MIRROR_VAL_U16:
        return esp_matter_uint16((uint16_t)value);
    case RM_MIRROR_VAL_I64:
        return esp_matter_int64(value);
    case RM_MIRROR_VAL_NULLABLE_I64:
        return esp_matter_nullable_int64(nullable<int64_t>(value));
    default:
        /* RM_MIRROR_VAL_NONE / _STRING: the mapping declared no numeric wrapping,
         * so the write fails rather than landing as a guessed width. */
        ESP_LOGE(TAG, "No numeric wrapping for mapping value type %d.", (int)type);
        return esp_matter_invalid(NULL);
    }
}

/**
 * @brief True when a nullable value holds Matter's null marker for its type.
 *
 * @note The marker is per-type, so it is asked of esp-matter's own `nullable<T>`
 *       rather than compared against a hard-coded sentinel.
 */
static bool __attr_val_is_null(const esp_matter_attr_val_t *val)
{
    switch (val->type) {
    case ESP_MATTER_VAL_TYPE_NULLABLE_BOOLEAN:
        /* nullable<bool> writes its marker into the bool's byte, so a null reads as true */
        return nullable<uint8_t>(val->val.u8).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT8:
        return nullable<int8_t>(val->val.i8).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_ENUM8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_BITMAP8:
        return nullable<uint8_t>(val->val.u8).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT16:
        return nullable<int16_t>(val->val.i16).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT16:
    case ESP_MATTER_VAL_TYPE_NULLABLE_ENUM16:
    case ESP_MATTER_VAL_TYPE_NULLABLE_BITMAP16:
        return nullable<uint16_t>(val->val.u16).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT32:
        return nullable<int32_t>(val->val.i32).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT32:
    case ESP_MATTER_VAL_TYPE_NULLABLE_BITMAP32:
        return nullable<uint32_t>(val->val.u32).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT64:
        return nullable<int64_t>(val->val.i64).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT64:
        return nullable<uint64_t>(val->val.u64).is_null();
    case ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT:
        return nullable<float>(val->val.f).is_null();
    default:
        return false;
    }
}

/**
 * @brief Reduce an esp-matter attribute value to the engine's int32
 *        representation. Returns false for unsupported/null values.
 */
static bool __attr_val_to_i32(const esp_matter_attr_val_t *val, int32_t *out)
{
    if (__attr_val_is_null(val)) {
        return false;
    }
    switch (val->type) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
        *out = val->val.b ? 1 : 0;
        return true;
    case ESP_MATTER_VAL_TYPE_INT32:
        *out = val->val.i32;
        return true;
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_ENUM8:
        *out = val->val.u8;
        return true;
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT8:
        *out = val->val.u8;
        return true;
    case ESP_MATTER_VAL_TYPE_INT16:
        *out = val->val.i16;
        return true;
    case ESP_MATTER_VAL_TYPE_UINT16:
        *out = val->val.u16;
        return true;
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT16:
        *out = val->val.u16;
        return true;
    case ESP_MATTER_VAL_TYPE_INT64:
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT64:
        /* Measurands are milli-units; anything past int32 is far outside a device's range */
        *out = (int32_t)val->val.i64;
        return true;
    default:
        return false;
    }
}

/* Matter-side callbacks *******************************************************/

static esp_err_t __attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                       uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    (void)priv_data;
    /* PRE_UPDATE intentionally accepts everything: hardware is driven only via
     * the RainMaker Neo device write callback (the app's single driver path). */
    if (type != attribute::POST_UPDATE) {
        return ESP_OK;
    }
    if (val->type == ESP_MATTER_VAL_TYPE_CHAR_STRING) {
        char str[RM_MIRROR_NODE_LABEL_MAX_LEN + 1];
        uint16_t len = val->val.a.s;
        if (len > RM_MIRROR_NODE_LABEL_MAX_LEN) {
            len = RM_MIRROR_NODE_LABEL_MAX_LEN;
        }
        if (val->val.a.b && len > 0) {
            memcpy(str, val->val.a.b, len);
        } else {
            len = 0;
        }
        str[len] = '\0';
        rm_mirror_engine_handle_matter_update_str(endpoint_id, cluster_id, attribute_id, str);
        return ESP_OK;
    }
    int32_t value;
    if (!__attr_val_to_i32(val, &value)) {
        return ESP_OK;
    }
    rm_mirror_engine_handle_matter_update(endpoint_id, cluster_id, attribute_id, value);
    return ESP_OK;
}

static esp_err_t __identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                     uint8_t effect_variant, void *priv_data)
{
    (void)priv_data;
    if (type == identification::START) {
        /* IdentifyTime is not in the callback signature and the attribute is
         * MANAGED_INTERNALLY (attribute::get_val rejects it); read it from the
         * CHIP Identify cluster object, which stores it before invoking this
         * callback, on the Matter thread. */
        int32_t seconds = 10; /* fallback */
        chip::app::Clusters::IdentifyCluster *identify_cluster = FindIdentifyClusterOnEndpoint(endpoint_id);
        if (identify_cluster) {
            seconds = identify_cluster->GetIdentifyTime();
        }
        rm_mirror_engine_handle_identify(endpoint_id, seconds);
    } else if (type == identification::STOP) {
        rm_mirror_engine_handle_identify(endpoint_id, 0);
    } else if (type == identification::EFFECT) {
        /* TriggerEffect carries no duration: map each effect to its spec nominal seconds, which
         * the identify param falls back to where the device renders no effect of its own. */
        using chip::app::Clusters::Identify::EffectIdentifierEnum;
        int32_t seconds;
        switch (static_cast<EffectIdentifierEnum>(effect_id)) {
        case EffectIdentifierEnum::kBlink:
            seconds = 1;
            break;
        case EffectIdentifierEnum::kBreathe:
            seconds = 15;
            break;
        case EffectIdentifierEnum::kOkay:
            seconds = 1;
            break;
        case EffectIdentifierEnum::kChannelChange:
            seconds = 8;
            break;
        case EffectIdentifierEnum::kFinishEffect:
        case EffectIdentifierEnum::kStopEffect:
            seconds = 0;
            break;
        default:
            seconds = 2;
            break;
        }
        /* An effect id the enum does not list is still handed over: the spec allows
         * manufacturer ones, and the handler's default branch decides what to do with them. */
        rm_mirror_engine_handle_identify_effect(
            endpoint_id, static_cast<esp_rmaker_matter_mirror_identify_effect_t>(effect_id),
            effect_variant, seconds);
    }
    return ESP_OK;
}

/* FixedLabel: read-only correlation entry *************************************/

/**
 * @brief Yields the single device-authored correlation entry
 *        (rmng.device = RainMaker Neo device id) for one mirrored endpoint. The
 *        FixedLabel cluster reads its list through the device info provider,
 *        so the entry is served from there rather than from an
 *        AttributeAccessInterface, which the cluster never consults.
 */
class RmMirrorFixedLabelIterator : public chip::DeviceLayer::DeviceInfoProvider::FixedLabelIterator {
public:
    explicit RmMirrorFixedLabelIterator(const char *device_id)
    {
        strlcpy(mDeviceId, device_id, sizeof(mDeviceId));
    }

    size_t Count() override
    {
        return 1;
    }

    bool Next(chip::DeviceLayer::DeviceInfoProvider::FixedLabelType &output) override
    {
        if (mDone) {
            return false;
        }
        mDone = true;
        output.label = chip::CharSpan::fromCharString(RM_MIRROR_LABEL_KEY_DEVICE_ID);
        output.value = chip::CharSpan::fromCharString(mDeviceId);
        return true;
    }

    void Release() override
    {
        chip::Platform::Delete(this);
    }

private:
    /* A copy: the device can be removed while the iterator lives */
    char mDeviceId[RM_MIRROR_LABEL_VALUE_MAX_LEN + 1];
    bool mDone = false;
};

/* Two esp-matter options, neither implying the other: ENABLE_ESP32_DEVICE_INFO_PROVIDER compiles in the provider
 * subclassed below, CUSTOM_DEVICE_INFO_PROVIDER makes esp-matter install ours rather than its own. Both sit in
 * choices this component's Kconfig cannot select, so the pairing is enforced here. */
#if defined(CONFIG_CUSTOM_DEVICE_INFO_PROVIDER) && !defined(CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER)
#error "Matter mirror: CONFIG_CUSTOM_DEVICE_INFO_PROVIDER needs CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER, which is "\
"what compiles in the provider the mirror extends."
#endif
#if !defined(CONFIG_CUSTOM_DEVICE_INFO_PROVIDER)
#warning "Matter mirror: without CONFIG_CUSTOM_DEVICE_INFO_PROVIDER the rmng.device FixedLabel entry is not served, "\
"leaving clients the endpoint ordering contract alone to correlate a device with its endpoint. Set "\
"CONFIG_CUSTOM_DEVICE_INFO_PROVIDER=y and CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER=y to serve it."
#else

/** @brief The ESP32 device info provider, extended with the mirror's correlation entry. */
class RmMirrorDeviceInfoProvider : public chip::DeviceLayer::ESP32DeviceInfoProvider {
public:
    FixedLabelIterator *IterateFixedLabel(chip::EndpointId endpoint) override
    {
        char device_id[RM_MIRROR_LABEL_VALUE_MAX_LEN + 1];
        if (!rm_mirror_engine_copy_device_id(endpoint, device_id, sizeof(device_id))) {
            return ESP32DeviceInfoProvider::IterateFixedLabel(endpoint);
        }
        return chip::Platform::New<RmMirrorFixedLabelIterator>(device_id);
    }
};

static RmMirrorDeviceInfoProvider __device_info_provider;
#endif /* CONFIG_CUSTOM_DEVICE_INFO_PROVIDER */

/* Port ops ********************************************************************/

/** Add the parent link, the Descriptor and the device type to a new endpoint. */
static bool __endpoint_build(endpoint_t *endpoint, uint16_t parent_endpoint_id, uint32_t device_type_id,
                             uint8_t device_type_version)
{
    if (parent_endpoint_id != RM_MIRROR_ENDPOINT_NONE) {
        endpoint_t *parent = endpoint::get(__matter_node, parent_endpoint_id);
        if (!parent || endpoint::set_parent_endpoint(endpoint, parent) != ESP_OK) {
            ESP_LOGE(TAG, "Could not put endpoint %u under endpoint %u.", endpoint::get_id(endpoint),
                     parent_endpoint_id);
            return false;
        }
    }
    /* Descriptor is mandatory on every endpoint, and is what a controller reads to learn the
     * endpoint's device types and cluster lists: without it the endpoint is invisible. esp-matter
     * adds it inside its device-type helpers, which the mirror does not use - it builds the
     * endpoint from the mapping instead. */
    cluster::descriptor::config_t descriptor_config;
    if (!cluster::descriptor::create(endpoint, &descriptor_config, CLUSTER_FLAG_SERVER)) {
        ESP_LOGE(TAG, "Descriptor cluster create failed; the endpoint would be unreadable.");
        return false;
    }
    return endpoint::add_device_type(endpoint, device_type_id, device_type_version) == ESP_OK;
}

/** create() after start, skipping endpoint IDs in use. */
static endpoint_t *__endpoint_create_unused(void)
{
    uint16_t max_id = 0;
    for (endpoint_t *ep = endpoint::get_first(__matter_node); ep; ep = endpoint::get_next(ep)) {
        max_id = std::max(max_id, endpoint::get_id(ep));
    }
    for (;;) {
        endpoint_t *endpoint = endpoint::create(__matter_node, ENDPOINT_FLAG_DESTROYABLE, NULL);
        if (!endpoint) {
            return NULL;
        }
        const uint16_t id = endpoint::get_id(endpoint);
        if (id > max_id && id != chip::kInvalidEndpointId) {
            return endpoint;
        }
        (void)endpoint::destroy(__matter_node, endpoint);
        if (id == chip::kInvalidEndpointId) {
            ESP_LOGE(TAG, "No endpoint ID left.");
            return NULL;
        }
    }
}

static esp_rmaker_error_t __pending_destroy_wait(uint16_t endpoint_id);

static esp_rmaker_error_t __port_endpoint_create(uint16_t parent_endpoint_id, uint16_t endpoint_id,
        uint32_t device_type_id, uint8_t device_type_version, uint16_t *out_endpoint_id)
{
    if (endpoint_id != RM_MIRROR_ENDPOINT_NONE) {
        /* resume() checks the ID against the counter that start() restores */
        if (!esp_matter::is_started()) {
            return ESP_RMAKER_NOT_SUPPORTED;
        }
        esp_rmaker_error_t err = __pending_destroy_wait(endpoint_id);
        if (err != ESP_RMAKER_OK) {
            return err;
        }
    }
    RmMirrorStackLock stack_lock;
    endpoint_t *endpoint;
    if (endpoint_id != RM_MIRROR_ENDPOINT_NONE) {
        /* Created disabled */
        endpoint = endpoint::resume(__matter_node, ENDPOINT_FLAG_DESTROYABLE, endpoint_id, NULL);
    } else if (esp_matter::is_started()) {
        endpoint = __endpoint_create_unused();
        /* create() enables it, but controllers must not see it before endpoint_enable */
        if (endpoint) {
            (void)endpoint::disable(endpoint);
        }
    } else {
        endpoint = endpoint::create(__matter_node, ENDPOINT_FLAG_DESTROYABLE, NULL);
    }
    if (!endpoint) {
        return ESP_RMAKER_FAIL;
    }
    if (!__endpoint_build(endpoint, parent_endpoint_id, device_type_id, device_type_version)) {
        (void)endpoint::destroy(__matter_node, endpoint);
        return ESP_RMAKER_FAIL;
    }
    *out_endpoint_id = endpoint::get_id(endpoint);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __port_endpoint_enable(uint16_t endpoint_id)
{
    /* Enabled by esp_matter::start() */
    if (!esp_matter::is_started()) {
        return ESP_RMAKER_OK;
    }
    RmMirrorStackLock stack_lock;
    endpoint_t *endpoint = endpoint::get(__matter_node, endpoint_id);
    if (!endpoint) {
        return ESP_RMAKER_NOT_FOUND;
    }
    if (endpoint::is_enabled(endpoint)) {
        return ESP_RMAKER_OK;
    }
    return (endpoint::enable(endpoint) == ESP_OK) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/* Removed endpoints that wait for their deferred attribute writes; used under the CHIP stack lock. */
typedef struct {
    endpoint_t *endpoint;
    chip::System::Clock::Timestamp due;
} __pending_destroy_t;

static __pending_destroy_t __pending_destroys[CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT];
static size_t __n_pending_destroys;

static void __pending_destroys_sweep(chip::System::Layer *layer, void *arg);

/** Arm the sweep for the earliest due endpoint; when the arm fails, the next destroy tries again. */
static void __pending_destroys_arm(void)
{
    if (__n_pending_destroys == 0) {
        return;
    }
    chip::System::Clock::Timestamp due = __pending_destroys[0].due;
    for (size_t i = 1; i < __n_pending_destroys; i++) {
        due = std::min(due, __pending_destroys[i].due);
    }
    const chip::System::Clock::Timestamp now = chip::System::SystemClock().GetMonotonicTimestamp();
    const chip::System::Clock::Timeout delay = (due > now) ? (due - now) : chip::System::Clock::kZero;
    if (chip::DeviceLayer::SystemLayer().StartTimer(delay, __pending_destroys_sweep, nullptr) != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "%u removed endpoint(s) wait for a timer to free them.", (unsigned)__n_pending_destroys);
    }
}

/** Free every removed endpoint that is due, then arm the sweep for the rest. */
static void __pending_destroys_sweep(chip::System::Layer *layer, void *arg)
{
    (void)layer;
    (void)arg;
    const chip::System::Clock::Timestamp now = chip::System::SystemClock().GetMonotonicTimestamp();
    size_t kept = 0;
    for (size_t i = 0; i < __n_pending_destroys; i++) {
        if (__pending_destroys[i].due > now) {
            __pending_destroys[kept++] = __pending_destroys[i];
        } else if (endpoint::destroy(__matter_node, __pending_destroys[i].endpoint) != ESP_OK) {
            ESP_LOGE(TAG, "Could not destroy a removed endpoint.");
        }
    }
    __n_pending_destroys = kept;
    __pending_destroys_arm();
}

/** Wait until no removed endpoint holds @p endpoint_id: free it once it is due. Not with the stack lock held. */
static esp_rmaker_error_t __pending_destroy_wait(uint16_t endpoint_id)
{
    for (;;) {
        chip::System::Clock::Milliseconds64 wait;
        {
            RmMirrorStackLock stack_lock;
            size_t i = 0;
            while (i < __n_pending_destroys && endpoint::get_id(__pending_destroys[i].endpoint) != endpoint_id) {
                i++;
            }
            if (i == __n_pending_destroys) {
                return ESP_RMAKER_OK;
            }
            const chip::System::Clock::Timestamp now = chip::System::SystemClock().GetMonotonicTimestamp();
            if (__pending_destroys[i].due <= now) {
                endpoint_t *endpoint = __pending_destroys[i].endpoint;
                memmove(&__pending_destroys[i], &__pending_destroys[i + 1],
                        (__n_pending_destroys - i - 1) * sizeof(__pending_destroys[0]));
                __n_pending_destroys--;
                return (endpoint::destroy(__matter_node, endpoint) == ESP_OK) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
            }
            wait = std::chrono::duration_cast<chip::System::Clock::Milliseconds64>(__pending_destroys[i].due - now);
        }
        ESP_LOGI(TAG, "Endpoint %u is freed in %u ms; waiting to resume it.", (unsigned)endpoint_id,
                 (unsigned)wait.count());
        vTaskDelay(pdMS_TO_TICKS(wait.count()) + 1);
    }
}

static esp_rmaker_error_t __port_endpoint_destroy(uint16_t endpoint_id)
{
    RmMirrorStackLock stack_lock;
    endpoint_t *endpoint = endpoint::get(__matter_node, endpoint_id);
    if (!endpoint) {
        return ESP_RMAKER_NOT_FOUND;
    }
    if (!esp_matter::is_started()) {
        return (endpoint::destroy(__matter_node, endpoint) == ESP_OK) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
    }
    if (__n_pending_destroys == sizeof(__pending_destroys) / sizeof(__pending_destroys[0])) {
        return ESP_RMAKER_NO_MEM;
    }
    /* Stop its cluster timers now and free it once its pending deferred attribute writes have run */
    for (cluster_t *cluster = cluster::get_first(endpoint); cluster; cluster = cluster::get_next(cluster)) {
        const uint8_t flags = cluster::get_flags(cluster);
        if ((flags & CLUSTER_FLAG_SERVER) && (flags & CLUSTER_FLAG_SHUTDOWN_FUNCTION)) {
            cluster::function_cluster_shutdown_t shutdown =
                (cluster::function_cluster_shutdown_t)cluster::get_function(cluster, CLUSTER_FLAG_SHUTDOWN_FUNCTION);
            if (shutdown) {
                shutdown(endpoint_id);
            }
        }
    }
    (void)endpoint::disable(endpoint);
    __pending_destroys[__n_pending_destroys++] = {
        endpoint,
        chip::System::SystemClock().GetMonotonicTimestamp() +
        chip::System::Clock::Milliseconds64(CONFIG_ESP_MATTER_DEFERRED_ATTR_PERSISTENCE_TIME_MS + 1000),
    };
    __pending_destroys_arm();
    return ESP_RMAKER_OK;
}

#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_BRIDGE
static esp_rmaker_error_t __port_aggregator_create(uint16_t *out_endpoint_id)
{
    return __port_endpoint_create(RM_MIRROR_ENDPOINT_NONE, RM_MIRROR_ENDPOINT_NONE,
                                  endpoint::aggregator::get_device_type_id(),
                                  endpoint::aggregator::get_device_type_version(), out_endpoint_id);
}

static esp_rmaker_error_t __port_endpoint_bridge(uint16_t endpoint_id, const char *unique_id, const char *label,
        bool reachable)
{
    RmMirrorStackLock stack_lock;
    endpoint_t *endpoint = endpoint::get(__matter_node, endpoint_id);
    if (!endpoint) {
        return ESP_RMAKER_NOT_FOUND;
    }
    if (endpoint::add_device_type(endpoint, endpoint::bridged_node::get_device_type_id(),
                                  endpoint::bridged_node::get_device_type_version()) != ESP_OK) {
        return ESP_RMAKER_FAIL;
    }
    cluster::bridged_device_basic_information::config_t config;
    config.reachable = reachable;
    strlcpy(config.unique_id, unique_id, sizeof(config.unique_id));
    cluster_t *cluster = cluster::bridged_device_basic_information::create(endpoint, &config, CLUSTER_FLAG_SERVER);
    if (!cluster) {
        return ESP_RMAKER_FAIL;
    }
    char node_label[cluster::bridged_device_basic_information::k_max_node_label_length + 1];
    strlcpy(node_label, label, sizeof(node_label));
    return cluster::bridged_device_basic_information::attribute::create_node_label(cluster, node_label,
            strlen(node_label)) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/* update(), not report(): the cluster emits ReachableChanged from its attribute change callback */
static esp_rmaker_error_t __port_endpoint_reachable_set(uint16_t endpoint_id, bool reachable)
{
    using namespace chip::app::Clusters;
    RmMirrorStackLock stack_lock;
    esp_matter_attr_val_t val = esp_matter_bool(reachable);
    return (attribute::update(endpoint_id, BridgedDeviceBasicInformation::Id,
                              BridgedDeviceBasicInformation::Attributes::Reachable::Id, &val) == ESP_OK)
           ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}
#endif /* CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_BRIDGE */

/* Per-cluster factories. Only the ones this build's mapping needs are compiled
 * in: each cluster pulls in KB-scale esp-matter/CHIP implementation. Every file
 * defines RM_MIRROR_PORT_HAS_CLUSTER_<id>, which the generated cross-check at
 * the end of this block verifies against what the mapping asked for. */
#if RM_MIRROR_USES_CLUSTER_0003
#include "clusters/identify.inc"
#endif
#if RM_MIRROR_USES_CLUSTER_0004
#include "clusters/groups.inc"
#endif
#if RM_MIRROR_USES_CLUSTER_0006
#include "clusters/on_off.inc"
#endif
#if RM_MIRROR_USES_CLUSTER_0008
#include "clusters/level_control.inc"
#endif
#if RM_MIRROR_USES_CLUSTER_0040
#include "clusters/fixed_label.inc"
#endif
#if RM_MIRROR_USES_CLUSTER_0300
#include "clusters/color_control.inc"
#endif

#include "rm_mirror_cluster_check_gen.h"

typedef cluster_t *(*__cluster_create_fn_t)(endpoint_t *endpoint, uint32_t feature_map);

typedef struct {
    uint32_t cluster_id;
    __cluster_create_fn_t create;
    /** FeatureMap bits arrive across calls, so re-enter the factory to merge them in */
    bool accumulates;
} __cluster_factory_t;

static const __cluster_factory_t __cluster_factories[] = {
#if RM_MIRROR_USES_CLUSTER_0003
    {RM_CLUSTER_IDENTIFY, __cluster_create_identify, false},
#endif
#if RM_MIRROR_USES_CLUSTER_0004
    {RM_CLUSTER_GROUPS, __cluster_create_groups, false},
#endif
#if RM_MIRROR_USES_CLUSTER_0006
    {RM_CLUSTER_ON_OFF, __cluster_create_on_off, false},
#endif
#if RM_MIRROR_USES_CLUSTER_0008
    {RM_CLUSTER_LEVEL_CONTROL, __cluster_create_level_control, false},
#endif
#if RM_MIRROR_USES_CLUSTER_0040
    {RM_CLUSTER_FIXED_LABEL, __cluster_create_fixed_label, false},
#endif
#if RM_MIRROR_USES_CLUSTER_0300
    {RM_CLUSTER_COLOR_CONTROL, __cluster_create_color_control, true},
#endif
};

#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS
/* Set by esp_rmaker_matter_mirror_register_cluster_factory(), for clusters the
 * port does not build itself. */
static esp_rmaker_matter_mirror_cluster_create_t __external_cluster_factory;
#endif

/* Set by esp_rmaker_matter_mirror_register_report_handler(), for the mapping's report_as kinds. */
static esp_rmaker_matter_mirror_report_t __report_handler;

static esp_rmaker_error_t __port_endpoint_device_type_add(uint16_t endpoint_id, uint32_t device_type_id,
        uint8_t device_type_version)
{
    RmMirrorStackLock stack_lock;
    endpoint_t *endpoint = endpoint::get(__matter_node, endpoint_id);
    if (!endpoint) {
        return ESP_RMAKER_NOT_FOUND;
    }
    return (endpoint::add_device_type(endpoint, device_type_id, device_type_version) == ESP_OK)
           ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

static esp_rmaker_error_t __port_cluster_create(uint16_t endpoint_id, uint32_t cluster_id, uint32_t feature_map)
{
    RmMirrorStackLock stack_lock;
    endpoint_t *endpoint = endpoint::get(__matter_node, endpoint_id);
    if (!endpoint) {
        return ESP_RMAKER_FAIL;
    }

    for (size_t i = 0; i < sizeof(__cluster_factories) / sizeof(__cluster_factories[0]); i++) {
        const __cluster_factory_t *factory = &__cluster_factories[i];
        if (factory->cluster_id != cluster_id) {
            continue;
        }
        if (!factory->accumulates && cluster::get(endpoint, cluster_id)) {
            return ESP_RMAKER_OK; /* already present */
        }
        return factory->create(endpoint, feature_map) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
    }

#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS
    if (__external_cluster_factory) {
        return __external_cluster_factory(endpoint_id, cluster_id, feature_map);
    }
#endif

    ESP_LOGE(TAG, "No factory for cluster 0x%04X - add one under src/port/clusters/, or enable "
             "CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS and register one at runtime.",
             (unsigned)cluster_id);
    return ESP_RMAKER_NOT_SUPPORTED;
}

static esp_rmaker_error_t __port_attribute_set(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value, rm_mirror_val_type_t type)
{
    RmMirrorStackLock stack_lock;
    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (!attribute) {
        ESP_LOGW(TAG, "Attribute 0x%04X/0x%04X not found on endpoint %u.",
                 (unsigned)cluster_id, (unsigned)attribute_id, endpoint_id);
        return ESP_RMAKER_NOT_FOUND;
    }
    esp_matter_attr_val_t val = __attr_val(type, value);
    return (attribute::set_val(attribute, &val) == ESP_OK) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

static esp_rmaker_error_t __port_attribute_update(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value, rm_mirror_val_type_t type)
{
    RmMirrorStackLock stack_lock;
    esp_matter_attr_val_t val = __attr_val(type, value);
    return (attribute::report(endpoint_id, cluster_id, attribute_id, &val) == ESP_OK)
           ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/** Write a string attribute: report() when esp-matter stores it, a local Write when a cluster manages it. */
static esp_rmaker_error_t __port_attribute_write_str(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, const char *value)
{
    RmMirrorStackLock stack_lock;
    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (!attribute) {
        ESP_LOGW(TAG, "Attribute 0x%04X/0x%04X not found on endpoint %u.",
                 (unsigned)cluster_id, (unsigned)attribute_id, endpoint_id);
        return ESP_RMAKER_NOT_FOUND;
    }
    if (!(attribute::get_flags(attribute) & ATTRIBUTE_FLAG_MANAGED_INTERNALLY)) {
        esp_matter_attr_val_t val = esp_matter_char_str((char *)value, strlen(value));
        return (attribute::report(endpoint_id, cluster_id, attribute_id, &val) == ESP_OK)
               ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
    }

    uint8_t buf[RM_STR_TLV_MAX];
    chip::TLV::TLVWriter writer;
    writer.Init(buf);
    if (writer.PutString(chip::TLV::AnonymousTag(), value) != CHIP_NO_ERROR ||
            writer.Finalize() != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "String attribute 0x%04X/0x%04X does not fit the %u-byte write buffer.",
                 (unsigned)cluster_id, (unsigned)attribute_id, (unsigned)sizeof(buf));
        return ESP_RMAKER_NO_MEM;
    }

    chip::TLV::TLVReader reader;
    reader.Init(buf, writer.GetLengthWritten());
    if (reader.Next() != CHIP_NO_ERROR) {
        return ESP_RMAKER_FAIL;
    }

    /* Local write: no fabric, so no ACL subject to describe. */
    chip::Access::SubjectDescriptor subject;
    chip::app::AttributeValueDecoder decoder(reader, subject);
    chip::app::DataModel::WriteAttributeRequest request(
        chip::app::ConcreteDataAttributePath(endpoint_id, cluster_id, attribute_id), subject);

    chip::app::DataModel::ActionReturnStatus status =
        data_model::provider::get_instance().WriteAttribute(request, decoder);
    if (!status.IsSuccess()) {
        ESP_LOGE(TAG, "Write of string attribute 0x%04X/0x%04X on endpoint %u failed.",
                 (unsigned)cluster_id, (unsigned)attribute_id, endpoint_id);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/**
 * @brief Read a numeric attribute the mirror does not own (a persisted StartUp*
 *        policy attribute). Fails on absent, null and non-numeric alike - all
 *        mean "no configured value".
 */
static esp_rmaker_error_t __port_attribute_get(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t *out)
{
    RmMirrorStackLock stack_lock;
    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (!attribute) {
        return ESP_RMAKER_NOT_FOUND;
    }
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    if (attribute::get_val(attribute, &val) != ESP_OK) {
        return ESP_RMAKER_FAIL;
    }
    if (__attr_val_is_null(&val)) {
        return ESP_RMAKER_INVALID_STATE;
    }
    switch (val.type) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
        *out = val.val.b ? 1 : 0;
        return ESP_RMAKER_OK;
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_ENUM8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_ENUM8:
        *out = val.val.u8;
        return ESP_RMAKER_OK;
    case ESP_MATTER_VAL_TYPE_UINT16:
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT16:
        *out = val.val.u16;
        return ESP_RMAKER_OK;
    default:
        return ESP_RMAKER_NOT_SUPPORTED;
    }
}

/**
 * @brief Stop writing an attribute to NVS on every change: Matter transition
 *        storms would otherwise cost one flash write per step.
 */
static esp_rmaker_error_t __port_attribute_defer_persistence(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id)
{
    RmMirrorStackLock stack_lock;
    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (!attribute) {
        return ESP_RMAKER_NOT_FOUND;
    }
    return (attribute::set_deferred_persistence(attribute) == ESP_OK) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/**
 * @brief Hand a value the mapping marked report_as to the application's handler: these clusters
 *        own their value (a struct they notify) instead of exposing a writable attribute, so the
 *        code that builds such a cluster also delivers its values.
 */
static esp_rmaker_error_t __port_report_as(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, const char *kind, int32_t value)
{
    if (!__report_handler) {
        ESP_LOGE(TAG, "No report handler for kind %s - register one with "
                 "esp_rmaker_matter_mirror_register_report_handler().", kind);
        return ESP_RMAKER_NOT_SUPPORTED;
    }
    return __report_handler(endpoint_id, cluster_id, attribute_id, kind, value);
}

/* Command invocation **********************************************************/

/* Local CommandHandler: no exchange and no responses. The mirror invokes commands only so the
 * cluster's own logic runs and the derived attributes follow (OnOff coupling, ColorMode,
 * RemainingTime), which a direct attribute write leaves stale. */
class RmMirrorLocalCommandHandler : public chip::app::CommandHandler {
public:
    CHIP_ERROR FallibleAddStatus(const chip::app::ConcreteCommandPath &,
                                 const chip::Protocols::InteractionModel::ClusterStatusCode &,
                                 const char * = nullptr) override
    {
        return CHIP_NO_ERROR;
    }
    void AddStatus(const chip::app::ConcreteCommandPath &,
                   const chip::Protocols::InteractionModel::ClusterStatusCode &,
                   const char * = nullptr) override {}
    chip::FabricIndex GetAccessingFabricIndex() const override
    {
        return chip::kUndefinedFabricIndex;
    }
    CHIP_ERROR AddResponseData(const chip::app::ConcreteCommandPath &, chip::CommandId,
                               const chip::app::DataModel::EncodableToTLV &) override
    {
        return CHIP_NO_ERROR;
    }
    void AddResponse(const chip::app::ConcreteCommandPath &, chip::CommandId,
                     const chip::app::DataModel::EncodableToTLV &) override {}
    bool IsTimedInvoke() const override
    {
        return false;
    }
    void FlushAcksRightAwayOnSlowCommand() override {}
    chip::Access::SubjectDescriptor GetSubjectDescriptor() const override
    {
        return chip::Access::SubjectDescriptor{};
    }
    chip::Messaging::ExchangeContext *GetExchangeContext() const override
    {
        return nullptr;
    }
};

struct rm_mirror_invoke_ctx_t {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t command_id;
    size_t tlv_len;
    uint8_t tlv[RM_CMD_TLV_MAX];
};

/* Encode the command fields as an anonymous TLV structure of context-tagged args, substituting the
 * Matter-domain value wherever the mapping marked from_value. */
static CHIP_ERROR __encode_cmd_payload(const rm_mirror_write_cmd_t *cmd, int32_t value,
                                       uint8_t *buf, size_t buf_len, size_t *out_len)
{
    chip::TLV::TLVWriter writer;
    writer.Init(buf, buf_len);
    chip::TLV::TLVType outer;
    ReturnErrorOnFailure(writer.StartContainer(chip::TLV::AnonymousTag(),
                         chip::TLV::kTLVType_Structure, outer));
    for (size_t i = 0; i < cmd->n_args; i++) {
        const rm_mirror_cmd_arg_t *arg = &cmd->args[i];
        const int32_t v = arg->from_value ? value : arg->literal;
        const chip::TLV::Tag tag = chip::TLV::ContextTag(arg->field_id);
        switch (arg->type) {
        case RM_MIRROR_VAL_U8:
            ReturnErrorOnFailure(writer.Put(tag, (uint8_t)v));
            break;
        case RM_MIRROR_VAL_U16:
            ReturnErrorOnFailure(writer.Put(tag, (uint16_t)v));
            break;
        case RM_MIRROR_VAL_U32:
            ReturnErrorOnFailure(writer.Put(tag, (uint32_t)v));
            break;
        case RM_MIRROR_VAL_I8:
            ReturnErrorOnFailure(writer.Put(tag, (int8_t)v));
            break;
        case RM_MIRROR_VAL_I16:
            ReturnErrorOnFailure(writer.Put(tag, (int16_t)v));
            break;
        case RM_MIRROR_VAL_I32:
            ReturnErrorOnFailure(writer.Put(tag, (int32_t)v));
            break;
        case RM_MIRROR_VAL_BOOL:
            ReturnErrorOnFailure(writer.PutBoolean(tag, v != 0));
            break;
        default:
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
    }
    ReturnErrorOnFailure(writer.EndContainer(outer));
    ReturnErrorOnFailure(writer.Finalize());
    *out_len = writer.GetLengthWritten();
    return CHIP_NO_ERROR;
}

/* Runs on the Matter thread. */
static void __invoke_work(intptr_t arg)
{
    rm_mirror_invoke_ctx_t *ctx = reinterpret_cast<rm_mirror_invoke_ctx_t *>(arg);
    chip::app::DataModel::Provider *provider =
        chip::app::InteractionModelEngine::GetInstance()->GetDataModelProvider();
    if (!provider) {
        ESP_LOGE(TAG, "Invoke: no data model provider.");
        free(ctx);
        return;
    }

    chip::TLV::TLVReader reader;
    reader.Init(ctx->tlv, ctx->tlv_len);
    CHIP_ERROR err = reader.Next();
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Invoke: TLV read failed: %" CHIP_ERROR_FORMAT, err.Format());
        free(ctx);
        return;
    }

    RmMirrorLocalCommandHandler handler;
    chip::app::ConcreteCommandPath path(ctx->endpoint_id,
                                        static_cast<chip::ClusterId>(ctx->cluster_id),
                                        static_cast<chip::CommandId>(ctx->command_id));
    chip::app::DataModel::InvokeRequest request(path, handler.GetSubjectDescriptor());
    std::optional<chip::app::DataModel::ActionReturnStatus> result =
        provider->InvokeCommand(request, reader, &handler);
    if (result.has_value() && !result->IsSuccess()) {
        ESP_LOGE(TAG, "Command 0x%04X/0x%02X on endpoint %u failed: 0x%02X",
                 (unsigned)ctx->cluster_id, (unsigned)ctx->command_id, ctx->endpoint_id,
                 (unsigned)result->GetStatusCode().GetStatus());
    }
    free(ctx);
}

static esp_rmaker_error_t __port_command_invoke(uint16_t endpoint_id, uint32_t cluster_id,
        const rm_mirror_write_cmd_t *cmd, int32_t value)
{
    if (!cmd) {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* Before the stack is up there is no provider to invoke through, and no controller to observe
     * the derived attributes either: the engine writes the attribute instead. */
    if (!__matter_hooks_registered) {
        return ESP_RMAKER_INVALID_STATE;
    }
    /* A disabled endpoint rejects commands, so the engine writes the attribute instead */
    {
        RmMirrorStackLock stack_lock;
        endpoint_t *endpoint = endpoint::get(__matter_node, endpoint_id);
        if (!endpoint || !endpoint::is_enabled(endpoint)) {
            return ESP_RMAKER_INVALID_STATE;
        }
    }

    rm_mirror_invoke_ctx_t *ctx = (rm_mirror_invoke_ctx_t *)malloc(sizeof(rm_mirror_invoke_ctx_t));
    if (!ctx) {
        return ESP_RMAKER_NO_MEM;
    }
    ctx->endpoint_id = endpoint_id;
    ctx->cluster_id = cluster_id;
    ctx->command_id = cmd->command_id;
    CHIP_ERROR err = __encode_cmd_payload(cmd, value, ctx->tlv, sizeof(ctx->tlv), &ctx->tlv_len);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to encode the payload of command 0x%02X: %" CHIP_ERROR_FORMAT,
                 (unsigned)cmd->command_id, err.Format());
        free(ctx);
        return ESP_RMAKER_FAIL;
    }

    err = chip::DeviceLayer::PlatformMgr().ScheduleWork(__invoke_work, reinterpret_cast<intptr_t>(ctx));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to schedule the invoke: %" CHIP_ERROR_FORMAT, err.Format());
        free(ctx);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* Platform facts **************************************************************/

static bool __port_boot_is_software_update(void)
{
    chip::app::Clusters::GeneralDiagnostics::BootReasonEnum boot_reason =
        chip::app::Clusters::GeneralDiagnostics::BootReasonEnum::kUnspecified;
    (void)chip::DeviceLayer::GetDiagnosticDataProvider().GetBootReason(boot_reason);
    if (boot_reason == chip::app::Clusters::GeneralDiagnostics::BootReasonEnum::kSoftwareUpdateCompleted) {
        return true;
    }
    /* CHIP sees only a Matter OTA */
    return __boot_unverified_image;
}

/* A RainMaker OTA image stays unverified until its diagnostics pass after MQTT connects. */
static bool __running_image_unverified(void)
{
    const osal_ota_partition_t *running = osal_ota_get_running_partition();
    osal_ota_img_states_t state;
    return running && osal_ota_get_state_partition(running, &state) == OSAL_ERR_OK &&
           state == OSAL_OTA_IMG_PENDING_VERIFY;
}

static bool __port_stack_lock_held(void)
{
#if CHIP_STACK_LOCK_TRACKING_ENABLED
    return esp_matter::is_started() && chip::DeviceLayer::PlatformMgr().IsChipStackLockedByCurrentThread();
#else
    return false;
#endif
}

/* Onboarding *******************************************************************/

#ifdef CONFIG_ENABLE_CHIPOBLE
/* Set once CHIP has released the BLE memory to the heap: it cannot be brought back without a
 * reboot, so a window re-opened afterwards can only advertise over DNS-SD. */
static bool __ble_reclaimed;
#endif

static size_t __fabric_count(void)
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount();
}

static esp_rmaker_error_t __port_onboarding_state_get(rm_mirror_onboarding_state_t *out)
{
    if (!out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    const bool commissioned = __fabric_count() > 0;
#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ONBOARD_MATTER_FIRST
    out->wait_for_commissioning = !commissioned;
#else
    out->wait_for_commissioning = false;
#endif
    out->commissioned = commissioned;
    out->network_up = chip::DeviceLayer::ConnectivityMgr().IsWiFiStationConnected();
    /* CHIP skips BLE entirely on a commissioned node, and only tears it down when BLE is
     * commissioning-only - so those are the cases where the event is never coming. */
#if defined(CONFIG_ENABLE_CHIPOBLE) && defined(CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING)
    out->ble_teardown_expected = !commissioned;
#else
    out->ble_teardown_expected = false;
#endif
#ifdef CONFIG_ENABLE_CHIPOBLE
    out->ble_reclaimed = __ble_reclaimed;
#else
    out->ble_reclaimed = false;
#endif
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __port_commissioning_window_open(uint16_t timeout_s)
{
    chip::CommissioningWindowManager &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (mgr.IsCommissioningWindowOpen()) {
        return ESP_RMAKER_OK;
    }
    /* Wi-Fi credentials survive fabric removal, so DNS-SD is enough; BLE is offered only while its
     * memory has not been reclaimed. */
    chip::CommissioningWindowAdvertisement advertisement = chip::CommissioningWindowAdvertisement::kDnssdOnly;
#ifdef CONFIG_ENABLE_CHIPOBLE
    if (!__ble_reclaimed) {
        advertisement = chip::CommissioningWindowAdvertisement::kAllSupported;
    }
#endif
    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(timeout_s),
                     advertisement);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open the commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static const rm_mirror_port_ops_t __port_ops = {
    .endpoint_create = __port_endpoint_create,
    .endpoint_device_type_add = __port_endpoint_device_type_add,
    .cluster_create = __port_cluster_create,
    .endpoint_enable = __port_endpoint_enable,
    .endpoint_destroy = __port_endpoint_destroy,
#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_BRIDGE
    .aggregator_create = __port_aggregator_create,
    .endpoint_bridge = __port_endpoint_bridge,
    .endpoint_reachable_set = __port_endpoint_reachable_set,
#endif
    .attribute_set = __port_attribute_set,
    .attribute_update = __port_attribute_update,
    .attribute_write_str = __port_attribute_write_str,
    .attribute_get = __port_attribute_get,
    .attribute_defer_persistence = __port_attribute_defer_persistence,
    .report_as = __port_report_as,
    .command_invoke = __port_command_invoke,
    .boot_is_software_update = __port_boot_is_software_update,
    .stack_lock_held = __port_stack_lock_held,
    .onboarding_state_get = __port_onboarding_state_get,
    .commissioning_window_open = __port_commissioning_window_open,
};

/* Factory reset ***************************************************************/

/* CHIP's own factory reset clears the Wi-Fi credentials, so the RainMaker Neo wipe must not do it again. */
static esp_rmaker_error_t __noop_network_reset(void)
{
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __matter_wipe(void *priv);

/* Hand a Matter-initiated reset to RainMaker Neo: it wipes its own data and every other participant, skipping
 * ours, and leaves the restart to CHIP. A nested call from inside our own wipe is a no-op there. */
static void __wipe_rmng(void)
{
    esp_rmaker_error_t err = esp_rmaker_system_ctrl_factory_reset_from_participant(__matter_wipe, __noop_network_reset, -1);
    if (err != ESP_RMAKER_OK) {
        ESP_LOGE(TAG, "RainMaker Neo factory reset wipe reported errors: %d", (int)err);
    }
}

/* Erase what CHIP's DoFactoryReset would have erased, for when it is never going to run. Keep in
 * sync with chip::DeviceLayer::ConfigurationManagerImpl::DoFactoryReset. chip-factory is deliberately
 * left alone: it holds the manufacturing data (DAC, CD), and erasing it would be unrecoverable. */
static void __erase_chip_namespace(const char *name_space)
{
    CHIP_ERROR err = chip::DeviceLayer::Internal::ESP32Config::ClearNamespace(name_space);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to clear the '%s' namespace.", name_space);
    }
}

static void __erase_chip_state(void)
{
    using chip::DeviceLayer::Internal::ESP32Config;
    __erase_chip_namespace(ESP32Config::kConfigNamespace_ChipConfig);
    __erase_chip_namespace(ESP32Config::kConfigNamespace_ChipCounters);
    if (chip::DeviceLayer::PersistedStorage::KeyValueStoreMgrImpl().EraseAll() != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to clear the CHIP key-value store.");
    }
}

/* Factory reset participant wipe: RainMaker Neo drives this one, so the Matter state goes here.
 * Declared reboots_on_wipe, so it must not return. */
static esp_rmaker_error_t __matter_wipe(void *priv)
{
    (void)priv;
    /* Erases esp_matter's KVS namespace and queues CHIP's own reset, which erases chip-config /
     * chip-counters, restores Wi-Fi and restarts. It only returns once queued, so wait for it. */
    esp_matter::factory_reset();

    if (__matter_hooks_registered) {
        vTaskDelay(pdMS_TO_TICKS(RM_CHIP_RESET_GRACE_MS));
    }

    /* Still here: the Matter stack was never started, or its event loop is not draining. Finish the
     * job rather than restart on a half-erased Matter state. */
    ESP_LOGW(TAG, "CHIP did not restart; erasing its state directly.");
    __erase_chip_state();
    esp_restart();
    return ESP_RMAKER_OK; /* unreachable */
}

static const esp_rmaker_system_ctrl_factory_reset_participant_t __reset_participant = {
    .name = "matter",
    .reboots_on_wipe = true,
    .wipe = __matter_wipe,
    .priv = nullptr,
};

/* Public API ******************************************************************/

static void __matter_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kFactoryReset:
        /* Matter-initiated reset: CHIP is already wiping its own state and will restart itself, so
         * only the RainMaker Neo side is left to clear. Runs on the Matter thread, ahead of CHIP's queued
         * wipe, while MQTT is still up - so the cloud node_reset notification still goes out. */
        ESP_LOGW(TAG, "Matter factory reset started; wiping the RainMaker Neo data.");
        __wipe_rmng();
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        rm_mirror_engine_handle_fabric_removed(__fabric_count());
        break;
#if ESP_RMAKER_MATTER_MIRROR_COMMISSIONING
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        rm_mirror_engine_handle_network_up();
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        rm_mirror_engine_handle_commissioning_complete();
        break;
    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        __ble_reclaimed = true;
        rm_mirror_engine_handle_ble_reclaimed();
        break;
#endif /* ESP_RMAKER_MATTER_MIRROR_COMMISSIONING */
    default:
        break;
    }
}

/* Carry the RainMaker node id in the CSR's vendor-reserved1 field, so a commissioner learns which
 * cloud node it is talking to from the commissioning exchange itself. Best effort: a node that has
 * not been claimed yet has no node id, and Matter commissioning still has to work without it.
 *
 * The cluster only enters the registry when the stack enables the endpoints, so this runs after
 * the stack is up - hence the stack lock. */
static void __set_csr_node_id(void)
{
    using chip::app::Clusters::OperationalCredentialsCluster;

    lock::ScopedChipStackLock stack_lock(portMAX_DELAY);

    chip::app::ServerClusterInterface *cluster = data_model::provider::get_instance().registry().Get(
                chip::app::ConcreteClusterPath(chip::kRootEndpointId, chip::app::Clusters::OperationalCredentials::Id));
    if (!cluster) {
        ESP_LOGW(TAG, "No Operational Credentials cluster; the CSR will not carry the RainMaker identity.");
        return;
    }

    char *node_id = esp_rmaker_get_node_id();
    if (!node_id) {
        ESP_LOGW(TAG, "No node id yet; the CSR will not carry the RainMaker identity.");
        return;
    }
    const size_t len = strlen(node_id);
    if (len > RM_CSR_VENDOR_DATA_MAX_LEN) {
        ESP_LOGE(TAG, "Node id is %u bytes, over the %u-byte vendor-reserved budget.", (unsigned)len,
                 (unsigned)RM_CSR_VENDOR_DATA_MAX_LEN);
        free(node_id);
        return;
    }

    /* Only this cluster registers on the path, and the build has no RTTI. */
    CHIP_ERROR err = static_cast<OperationalCredentialsCluster *>(cluster)->SetCSRVendorReserved(
                         OperationalCredentialsCluster::CSRVendorReservedField::kVendorReserved1,
                         chip::ByteSpan(reinterpret_cast<const uint8_t *>(node_id), len));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to set the CSR node id: %" CHIP_ERROR_FORMAT, err.Format());
        free(node_id);
        return;
    }
    /* The cluster keeps the span rather than a copy, so the id has to outlive this call; the
     * previous one is only released once the cluster no longer points at it. */
    free(__csr_node_id);
    __csr_node_id = node_id;
}

extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_register_hooks(void)
{
    if (!__matter_node || __port_stack_lock_held()) {
        return ESP_RMAKER_INVALID_STATE;
    }
    /* CHIP de-duplicates by (handler, arg), so this is idempotent - and a no-op when
     * esp_matter::start() already registered the same callback. */
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().AddEventHandler(__matter_event_cb, 0);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to register the Matter event handler.");
        return ESP_RMAKER_FAIL;
    }
    __matter_hooks_registered = true;

    /* The stack has applied its StartUp* attributes and restored the NONVOLATILE ones by now.
     * First honor the StartUp policy attributes (Matter spec: on power-up the device SHALL
     * assume the configured state) by injecting the implied values into the RainMaker Neo param store,
     * then re-assert the canonical values and let inbound changes through - the reseed now
     * carries the post-StartUp state, so Matter and RainMaker Neo agree. */
    rm_mirror_engine_start_inbound();

    __set_csr_node_id();

    /* Sampled here rather than in enable(): the fabric table only becomes readable once the stack
     * is up, and it decides whether commissioning and a BLE teardown are still to come. Armed in
     * every onboarding mode - the readiness gate is BLE-only, but re-opening the commissioning
     * window when the last fabric goes is not: an on-network node is just as uncommissionable
     * afterwards, since CHIP only opens the window by itself at boot. */
    rm_mirror_onboarding_state_t state = {};
    if (__port_onboarding_state_get(&state) == ESP_RMAKER_OK) {
        rm_mirror_onboarding_init(&__port_ops, &state, __reopen_window,
                                  CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_WINDOW_TIMEOUT_S);
    }
    return ESP_RMAKER_OK;
}

#if ESP_RMAKER_MATTER_MIRROR_COMMISSIONING
extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_wait_ready(uint32_t timeout_ms)
{
    if (!__matter_hooks_registered) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (timeout_ms == 0) {
        timeout_ms = CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_READY_TIMEOUT_MS;
    }
    return rm_mirror_onboarding_wait(timeout_ms);
}

extern "C" bool esp_rmaker_matter_mirror_is_ready(void)
{
    return __matter_hooks_registered && rm_mirror_onboarding_ready();
}
#endif /* ESP_RMAKER_MATTER_MIRROR_COMMISSIONING */

#ifdef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS
extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_register_cluster_factory(
    esp_rmaker_matter_mirror_cluster_create_t factory)
{
    if (!factory) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (__matter_node) {
        ESP_LOGE(TAG, "Register the cluster factory before enabling the mirror: the tree is already built.");
        return ESP_RMAKER_INVALID_STATE;
    }
    __external_cluster_factory = factory;
    return ESP_RMAKER_OK;
}

extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_unregister_cluster_factory(void)
{
    __external_cluster_factory = NULL;
    return ESP_RMAKER_OK;
}
#endif /* CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS */

extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_register_report_handler(
    esp_rmaker_matter_mirror_report_t handler)
{
    if (!handler) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __report_handler = handler;
    return ESP_RMAKER_OK;
}

extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_unregister_report_handler(void)
{
    __report_handler = NULL;
    return ESP_RMAKER_OK;
}

extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_register_identify_handler(
    esp_rmaker_matter_mirror_identify_t handler)
{
    if (!handler) {
        return ESP_RMAKER_INVALID_ARG;
    }
    rm_mirror_engine_set_identify_handler(handler);
    return ESP_RMAKER_OK;
}

extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_unregister_identify_handler(void)
{
    rm_mirror_engine_set_identify_handler(NULL);
    return ESP_RMAKER_OK;
}

extern "C" esp_rmaker_error_t esp_rmaker_matter_mirror_enable(const esp_rmaker_node_t *node,
        const esp_rmaker_matter_mirror_config_t *config)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (__matter_node) {
        return ESP_RMAKER_INVALID_STATE;
    }
    __boot_unverified_image = __running_image_unverified();
    if (config && config->commissioning_window_policy == ESP_RMAKER_MATTER_MIRROR_COMMISSIONING_WINDOW_NEVER) {
        __reopen_window = false;
    }

#ifdef CONFIG_CUSTOM_DEVICE_INFO_PROVIDER
    /* Before esp_matter::start(): the label clusters capture whichever provider is installed when the stack enables
     * the endpoints, and only ours serves the rmng.device entry. */
    esp_matter::set_custom_device_info_provider(&__device_info_provider);
#endif

    node::config_t node_config;
    __matter_node = node::create(&node_config, __attribute_update_cb, __identification_cb);
    if (!__matter_node) {
        ESP_LOGE(TAG, "Failed to create Matter node.");
        return ESP_RMAKER_FAIL;
    }

    esp_rmaker_error_t err = rm_mirror_engine_init(node, &__port_ops);
    if (err != ESP_RMAKER_OK) {
        ESP_LOGE(TAG, "Mirror engine init failed: %d", err);
        return err;
    }

    err = rm_mirror_engine_start();
    if (err != ESP_RMAKER_OK) {
        ESP_LOGE(TAG, "Mirror engine start failed: %d", err);
        rm_mirror_engine_deinit();
        return err;
    }

    /* Registered last: an earlier failure return must not leave a participant wiping a mirror that
     * was never enabled. */
    err = esp_rmaker_system_ctrl_factory_reset_participant_register(&__reset_participant);
    if (err != ESP_RMAKER_OK) {
        ESP_LOGE(TAG, "Failed to register the factory reset participant: %d", err);
        rm_mirror_engine_deinit();
        return err;
    }

    if (!config || !config->defer_matter_start) {
        esp_err_t matter_err = esp_matter::start(__matter_event_cb);
        if (matter_err != ESP_OK) {
            ESP_LOGE(TAG, "Matter start failed: %d", matter_err);
            (void)esp_rmaker_system_ctrl_factory_reset_participant_unregister(__reset_participant.wipe);
            rm_mirror_engine_deinit();
            return ESP_RMAKER_FAIL;
        }
        /* esp_matter::start() already registered __matter_event_cb; this records that the stack is
         * up and keeps the deferred path on the same code. Logs on failure; the mirror still works,
         * only Matter-initiated resets stop clearing the RainMaker Neo side. */
        (void)esp_rmaker_matter_mirror_register_hooks();
    } else {
        ESP_LOGW(TAG, "Matter start deferred; call esp_rmaker_matter_mirror_register_hooks() after starting it.");
    }
    rm_mirror_engine_open_devices();
    ESP_LOGI(TAG, "Matter mirror enabled.");
    return ESP_RMAKER_OK;
}

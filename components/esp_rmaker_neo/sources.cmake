set(RMNG_NAME esp_rmaker_neo)
set(RMNG_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(RMNG_SRCS
    # Local configuration
    "${RMNG_SRC_DIR}/local_config/local_config.c"
    "${RMNG_SRC_DIR}/esp_rmaker_credentials_provider.c"
    # Provisioning helpers (glue over network_provisioning on ESP; stub on POSIX)
    "${RMNG_SRC_DIR}/prov_helpers/prov_helpers.c"
    # Network
    "${RMNG_SRC_DIR}/network/network_common.c"
    "${RMNG_SRC_DIR}/network/mqtt_topics.c"
    "${RMNG_SRC_DIR}/network/cloud/cloud_manager.c"
    "${RMNG_SRC_DIR}/network/cloud/cloud_events.c"
    "${RMNG_SRC_DIR}/network/network_state_report.c"
    "${RMNG_SRC_DIR}/network/notify.c"
    # Core
    "${RMNG_SRC_DIR}/esp_rmaker_core.c"
    # Challenge-Response endpoint
    "${RMNG_SRC_DIR}/chal_resp/chal_resp.c"
    "${RMNG_SRC_DIR}/chal_resp/chal_resp_pb-c.c"
    # Services
    "${RMNG_SRC_DIR}/services/schedules.c"
    "${RMNG_SRC_DIR}/services/automation.c"
    # Node model
    "${RMNG_SRC_DIR}/data/esp_rmaker_val.c"
    "${RMNG_SRC_DIR}/node/node_core.c"
    "${RMNG_SRC_DIR}/node/node_config.c"
    "${RMNG_SRC_DIR}/node/node_timeseries.c"
    # Retry
    "${RMNG_SRC_DIR}/retry/retry_manager.c"
    # Event loop
    "${RMNG_SRC_DIR}/esp_rmaker_event_loop.c"
    # Checksum
    "${RMNG_SRC_DIR}/esp_rmaker_checksum.c"
    # System control
    "${RMNG_SRC_DIR}/esp_rmaker_system_ctrl.c"
    # Utilities
    "${RMNG_SRC_DIR}/util/esp_rmaker_trigger_codec.c"
    # Console entry point
    "${RMNG_SRC_DIR}/console/esp_rmaker_console.c"
    # esp_schedule port (osal-backed; see overrides/esp_schedule/CMakeLists.txt)
    "${CMAKE_CURRENT_LIST_DIR}/overrides/esp_schedule/port/esp_schedule_port_osal.c"
)

# Versioning
include(${CMAKE_CURRENT_LIST_DIR}/versioning.cmake)
set(RMNG_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include" "${RMNG_VERSION_INCLUDE_DIR}")
set(RMNG_PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/priv_include"
                           "${CMAKE_CURRENT_LIST_DIR}/overrides/esp_schedule/port"
)
set(RMNG_PRIV_STUB_HOST_CTRL_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/priv_include/stubs/host_ctrl")

set(RMNG_REQUIRES osal esp_rmaker_neo_common)
set(RMNG_PRIV_REQUIRES mbedtls protobuf-c esp_rmaker_neo_common)
if (ESP_PLATFORM)
    list(APPEND RMNG_PRIV_REQUIRES protocomm)
else ()
    # protocomm-posix is a separate static lib built by osal; the local endpoints service is its only consumer, so link
    # it here rather than making every osal consumer pay for it.
    list(APPEND RMNG_PRIV_REQUIRES protocomm-posix)
endif ()
# esp_schedule is the same target name on both platforms now: the managed espressif/esp_schedule component on ESP-IDF
# (see idf_component.yml), and the static lib built by overrides/esp_schedule/CMakeLists.txt on POSIX.
list(APPEND RMNG_PRIV_REQUIRES esp_schedule)

# Serial console support. Provides esp_rmaker_console_init() which sets up the common console (via the
# espressif/rmaker_console component) and registers the RMNG built-in commands. Unified across ESP-IDF and POSIX: POSIX
# is served by the osal console shim + vendored rmaker_console (see overrides/rmaker_console).
if (NOT DEFINED RMNG_CONSOLE_ENABLED)
    set(RMNG_CONSOLE_ENABLED ON)
endif ()
if (DEFINED CONFIG_RMNG_CONSOLE_ENABLED)
    set(RMNG_CONSOLE_ENABLED ${CONFIG_RMNG_CONSOLE_ENABLED})
endif ()
message(STATUS "RMNG_CONSOLE_ENABLED: ${RMNG_CONSOLE_ENABLED}")
# Assisted claiming. Lets a node without pre-flashed cloud credentials obtain them at first setup, via the phone app
# over the provisioning session. claim_rules.c carries the state-machine guards, fragment bounds and JSON escaping. It
# is compiled unconditionally, and on every platform, so the host tests link the same object the firmware does rather
# than reaching into claim.c (which is BLE-only, hence ESP-only).
list(APPEND RMNG_SRCS "${RMNG_SRC_DIR}/claim/claim_rules.c" "${RMNG_SRC_DIR}/claim/claim_pb-c.c")
if (CONFIG_ESP_RMAKER_ASSISTED_CLAIM)
    list(APPEND RMNG_SRCS "${RMNG_SRC_DIR}/claim/claim.c")
endif ()

if (RMNG_CONSOLE_ENABLED)
    # Backend only. esp_rmaker_console.c is always compiled (see RMNG_SRCS above); these are the built-in commands + the
    # common-console dependency, pulled in only when the console is enabled.
    list(APPEND RMNG_SRCS "${RMNG_SRC_DIR}/console/esp_rmaker_commands.c")
    if (ESP_PLATFORM)
        # esp_console is provided by the IDF `console` component; the common console + commands come from the managed
        # espressif/rmaker_console component.
        list(APPEND RMNG_PRIV_REQUIRES console espressif__rmaker_console)
    else ()
        # POSIX: vendored rmaker_console target (publicly links the osal console shim).
        list(APPEND RMNG_PRIV_REQUIRES rmaker_console)
    endif ()
endif ()

get_property(_rmng_checks_done GLOBAL PROPERTY _RMNG_CHECKS_CONFIGURED)
if (NOT _rmng_checks_done)
    # Configure pre-processor checks to enforce minimum stack sizes for the SDK.
    set(MIN_FREERTOS_TIMER_TASK_STACK_DEPTH 3072)
    if (ESP_PLATFORM)
        if (CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH LESS ${MIN_FREERTOS_TIMER_TASK_STACK_DEPTH})
            message(
                WARNING
                    "This SDK requires CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH >= ${MIN_FREERTOS_TIMER_TASK_STACK_DEPTH} "
                    "(currently ${CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH}). "
                    "Add 'CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=${MIN_FREERTOS_TIMER_TASK_STACK_DEPTH}' to your "
                    "project's sdkconfig.defaults, or raise it in menuconfig under "
                    "Component config → FreeRTOS → Kernel."
            )
        endif ()
    endif ()

    set(RMNG_CHECKS_MIN_STACK_SIZES_SRC ${CMAKE_CURRENT_BINARY_DIR}/src/checks/min_stack_sizes.c)
    configure_file(${CMAKE_CURRENT_LIST_DIR}/src/checks/min_stack_sizes.c.in ${RMNG_CHECKS_MIN_STACK_SIZES_SRC})

    set(RMNG_CHECKS_SRCS ${RMNG_CHECKS_MIN_STACK_SIZES_SRC})
    set_property(GLOBAL PROPERTY _RMNG_CHECKS_CONFIGURED TRUE)
endif ()

# Host control sources. Defining them is unconditional; only consumption is gated on RMNG_HOST_CTRL, which is resolved
# in CMakeLists.txt. bridge_handlers.c is not here: it depends on CONFIG_RMNG_BRIDGE_ENABLED, and on POSIX no CONFIG_*
# exists yet at the point this file is included.
set(RMNG_HOST_CTRL_SRCS
    "${RMNG_SRC_DIR}/host_ctrl/host_ctrl_core.c"
    "${RMNG_SRC_DIR}/host_ctrl/host_ctrl_handler.c"
    "${RMNG_SRC_DIR}/host_ctrl/host_ctrl_processing_impl.c"
    "${RMNG_SRC_DIR}/host_ctrl/host_ctrl_event_flags.c"
    "${RMNG_SRC_DIR}/host_ctrl/network/shadows.c"
    "${RMNG_SRC_DIR}/host_ctrl/network/mqtt_control.c"
    # Data model remote handlers and services
    "${RMNG_SRC_DIR}/host_ctrl/data_model/dm_handlers.c"
    "${RMNG_SRC_DIR}/host_ctrl/services/host_ctrl_latency.c"
)
set(RMNG_HOST_CTRL_PRIV_INCLUDE_DIRS "${RMNG_SRC_DIR}/host_ctrl/priv_include")

set(RMNG_DATA_MODEL_SRC_BASE_DIR "${RMNG_SRC_DIR}/data_model")

# Data model headers live in the component's include/ and priv_include/ roots.
set(RMNG_DATA_MODEL_INCLUDE_DIRS "")
set(RMNG_DATA_MODEL_PRIV_INCLUDE_DIRS "")

set(RMNG_DATA_MODEL_SRCS
    # Data model
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/dm_device.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/dm_param.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/dm_node.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/dm_state_changes.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/dm_timeseries.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/dm_path.c"
    # Standard types
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/standard_types/esp_rmaker_standard_params.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/standard_types/esp_rmaker_standard_devices.c"
    # Standard service creation
    "${RMNG_SRC_DIR}/services/svc_timezone.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/service_creation/dm_svc_timezone.c"
    "${RMNG_SRC_DIR}/services/svc_system.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/service_creation/dm_svc_system.c"
    # Local endpoints service: local control and/or challenge-response on one protocomm instance
    "${RMNG_SRC_DIR}/services/svc_local_endpoints.c"
    "${RMNG_DATA_MODEL_SRC_BASE_DIR}/service_creation/dm_svc_local_ctrl.c"
    "${RMNG_SRC_DIR}/local_ctrl/local_ctrl_endpoints.c"
    "${RMNG_SRC_DIR}/local_ctrl/local_ctrl_pb-c.c"
)

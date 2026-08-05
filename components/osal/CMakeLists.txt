# osal — the OS abstraction layer for the RMNG SDK.
#
# One component covering: platform primitives (tasks, queues, semaphores, event groups, event loop, scheduler, time,
# ticks, random, crc, log, mem, sysinfo, sysctrl), error codes, storage (key-value persistence), JSON, time sync,
# network status, discovery (mDNS/SRP), HTTP client (optional), console shim + CA bundle (POSIX) and (optional) heap
# monitoring.
#
# Layout: one directory per area, `include/` public headers, `src/` with per-platform implementations. Builds as a
# single ESP-IDF component or a single POSIX CMake library.

set(OSAL_NAME osal)
set(OSAL_DIR ${CMAKE_CURRENT_LIST_DIR})

# Common sources / includes (platform-independent) ###
set(OSAL_SRCS
    # platform
    "${OSAL_DIR}/platform/src/common/random_common.c"
    "${OSAL_DIR}/platform/src/common/crc_common.c"
    # errors
    "${OSAL_DIR}/errors/src/os_err_strerror.c"
    # timesync
    "${OSAL_DIR}/timesync/src/timezone_db.c"
    "${OSAL_DIR}/timesync/src/timesync_common_impl.c"
)
set(OSAL_INCLUDE_DIRS
    "${OSAL_DIR}/platform/include"
    "${OSAL_DIR}/errors/include"
    "${OSAL_DIR}/storage/include"
    "${OSAL_DIR}/timesync/include"
    "${OSAL_DIR}/netstatus/include"
    "${OSAL_DIR}/discovery/include"
    "${OSAL_DIR}/ota/include"
    "${OSAL_DIR}/mqtt/include"
)
set(OSAL_PRIV_INCLUDE_DIRS "")

# HTTP client (optional) ###
if (NOT DEFINED OSAL_INCLUDE_HTTP)
    set(OSAL_INCLUDE_HTTP OFF)
endif ()
if (DEFINED CONFIG_OSAL_INCLUDE_HTTP)
    set(OSAL_INCLUDE_HTTP ${CONFIG_OSAL_INCLUDE_HTTP})
endif ()
if (OSAL_INCLUDE_HTTP)
    list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/http/include")
endif ()

# External serial I/O (optional) ###
if (NOT DEFINED OSAL_INCLUDE_EXT_IO)
    set(OSAL_INCLUDE_EXT_IO OFF)
endif ()
if (DEFINED CONFIG_OSAL_INCLUDE_EXT_IO)
    set(OSAL_INCLUDE_EXT_IO ${CONFIG_OSAL_INCLUDE_EXT_IO})
endif ()
if (OSAL_INCLUDE_EXT_IO)
    list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/ext-io/include")
endif ()

# JSON (managed json_generator / json_parser) ###
if (NOT ESP_PLATFORM)
    include("${OSAL_DIR}/../../cmake/esp_component_manager.cmake")
    rmng_idf_components_import(
        OSAL_JSON_COMPONENTS_DIR DEPENDENCIES "espressif/json_generator:^1.2.0" "espressif/json_parser:^1.0.3"
    )
    set(_JSON_GENERATOR_DIR "${OSAL_JSON_COMPONENTS_DIR}/espressif__json_generator")
    set(_JSON_PARSER_DIR "${OSAL_JSON_COMPONENTS_DIR}/espressif__json_parser")
    set(_JSMN_DIR "${OSAL_JSON_COMPONENTS_DIR}/espressif__jsmn") # used by json_parser
    list(APPEND OSAL_SRCS "${_JSON_GENERATOR_DIR}/src/json_generator.c" "${_JSON_PARSER_DIR}/src/json_parser.c")
    list(APPEND OSAL_INCLUDE_DIRS "${_JSON_GENERATOR_DIR}/include" "${_JSON_PARSER_DIR}/include" "${_JSMN_DIR}/include")
endif ()

# Heap monitor (optional) ###
if (NOT DEFINED OSAL_INCLUDE_HEAP_MONITOR)
    set(OSAL_INCLUDE_HEAP_MONITOR OFF)
endif ()
if (DEFINED CONFIG_OSAL_INCLUDE_HEAP_MONITOR)
    set(OSAL_INCLUDE_HEAP_MONITOR ${CONFIG_OSAL_INCLUDE_HEAP_MONITOR})
endif ()
if (OSAL_INCLUDE_HEAP_MONITOR)
    list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/heap-monitor/include")
endif ()

# Virtual scheduler (testing) ###
if (NOT DEFINED OSAL_USING_VIRTUAL_SCHEDULER)
    set(OSAL_USING_VIRTUAL_SCHEDULER OFF)
endif ()
if (CMAKE_TESTING_ENABLED)
    set(OSAL_USING_VIRTUAL_SCHEDULER ON)
endif ()
if (DEFINED CONFIG_OSAL_USING_VIRTUAL_SCHEDULER)
    set(OSAL_USING_VIRTUAL_SCHEDULER ${CONFIG_OSAL_USING_VIRTUAL_SCHEDULER})
endif ()
message(STATUS "OSAL_USING_VIRTUAL_SCHEDULER: ${OSAL_USING_VIRTUAL_SCHEDULER}")
if (OSAL_USING_VIRTUAL_SCHEDULER)
    list(APPEND OSAL_SRCS "${OSAL_DIR}/platform/virtual_scheduler/scheduler_virtual.c")
    list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/platform/virtual_scheduler")
else ()
    list(APPEND OSAL_SRCS "${OSAL_DIR}/platform/src/common/time_common.c")
endif ()

# Timesync compile-time reference time ### Generated into a *public* include dir so consumers (and the timesync tests,
# on any platform) get it straight from osal's interface includes, with no per-target plumbing. The dir exists before
# idf_component_register() below.
set(OSAL_TIMESYNC_BINARY_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/timesync/include)
string(TIMESTAMP OSAL_TIMESYNC_REF_TIME "%s" UTC)
configure_file(
    ${OSAL_DIR}/timesync/include/osal_timesync_ref_time.h.in
    ${OSAL_TIMESYNC_BINARY_INCLUDE_DIR}/osal_timesync_ref_time.h
)
list(APPEND OSAL_INCLUDE_DIRS "${OSAL_TIMESYNC_BINARY_INCLUDE_DIR}")
list(APPEND OSAL_PRIV_INCLUDE_DIRS "${OSAL_DIR}/timesync/priv_include")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/errors/include/esp-idf" "${OSAL_DIR}/platform/src/esp")
    list(
        APPEND
        OSAL_SRCS
        # platform (FreeRTOS + ESP)
        "${OSAL_DIR}/platform/src/esp/task_esp.c"
        "${OSAL_DIR}/platform/src/esp/semaphore_esp.c"
        "${OSAL_DIR}/platform/src/esp/queue_esp.c"
        "${OSAL_DIR}/platform/src/esp/event_group_esp.c"
        "${OSAL_DIR}/platform/src/esp/ticks_esp.c"
        "${OSAL_DIR}/platform/src/esp/event_loop_esp.c"
        "${OSAL_DIR}/platform/src/esp/sysinfo_esp.c"
        "${OSAL_DIR}/platform/src/esp/sysinfo_mac_esp.c"
        "${OSAL_DIR}/platform/src/esp/sysctrl_esp.c"
        "${OSAL_DIR}/platform/src/esp/random_esp.c"
        # storage
        "${OSAL_DIR}/storage/src/nvs_esp_impl.c"
        # timesync
        "${OSAL_DIR}/timesync/src/time_esp.c"
        # netstatus
        "${OSAL_DIR}/netstatus/src/netstatus_esp.c"
        # ota
        "${OSAL_DIR}/ota/src/osal_ota_esp.c"
        # mqtt (common layer; the impl is Kconfig-selected below)
        "${OSAL_DIR}/mqtt/src/osal_mqtt_events.c"
        "${OSAL_DIR}/mqtt/src/osal_mqtt_util.c"
        "${OSAL_DIR}/mqtt/src/osal_mqtt_subscription_manager.c"
        "${OSAL_DIR}/mqtt/src/osal_mqtt_impl.c"
    )
    if (CONFIG_OSAL_MQTT_IMPL_ESP)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/mqtt/src/osal_mqtt_esp_impl.c")
    endif ()
    if (CONFIG_OSAL_MQTT_IMPL_CORE)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/mqtt/src/osal_mqtt_core_impl.c")
    endif ()
    if (OSAL_INCLUDE_HEAP_MONITOR)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/heap-monitor/src/heap_monitor_esp.c")
    endif ()
    if (OSAL_INCLUDE_HTTP)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/http/src/http_esp_impl.c")
    endif ()
    if (OSAL_INCLUDE_EXT_IO)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/ext-io/src/osal_ext_io_esp.c")
        list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/ext-io/esp/include")
    endif ()
    # discovery (mDNS or Thread SRP, per Kconfig choice)
    if (CONFIG_OSAL_DISCOVERY_DISCOVERY_TYPE_MDNS)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/discovery/src/mdns_esp.c")
    elseif (CONFIG_OSAL_DISCOVERY_DISCOVERY_TYPE_SRP)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/discovery/src/srp_esp.c")
    endif ()

    # Timer implementation
    if (NOT OSAL_USING_VIRTUAL_SCHEDULER)
        if (CONFIG_OSAL_TIMER_IMPL_ESP)
            message(STATUS "OSAL_TIMER_IMPL: Using esp_timer timer implementation")
            list(APPEND OSAL_SRCS "${OSAL_DIR}/platform/src/esp/scheduler_esp.c")
        elseif (CONFIG_OSAL_TIMER_IMPL_FREERTOS)
            message(STATUS "OSAL_TIMER_IMPL: Using FreeRTOS software timers timer implementation")
            list(APPEND OSAL_SRCS "${OSAL_DIR}/platform/src/esp/scheduler_freertos.c")
        endif ()
    endif ()

    idf_component_register(
        SRCS ${OSAL_SRCS}
        INCLUDE_DIRS ${OSAL_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${OSAL_PRIV_INCLUDE_DIRS}
        REQUIRES espressif__json_generator espressif__json_parser esp_event esp_netif
        PRIV_REQUIRES
            esp_app_format
            log
            nvs_flash
            mbedtls
            esp_partition
            app_update
            esp_timer
    )

    if (NOT OSAL_USING_VIRTUAL_SCHEDULER AND CONFIG_OSAL_TIMER_IMPL_ESP)
        idf_component_optional_requires(PRIVATE esp_timer)
    endif ()

    if (OSAL_INCLUDE_HTTP)
        idf_component_optional_requires(PRIVATE esp_http_client)
    endif ()
    if (OSAL_INCLUDE_EXT_IO)
        idf_component_optional_requires(PRIVATE esp_driver_uart)
    endif ()
    # mqtt: optional dependencies for the selected implementation
    if (CONFIG_OSAL_MQTT_IMPL_ESP)
        idf_component_optional_requires(PRIVATE espressif__mqtt)
    endif ()
    if (CONFIG_OSAL_MQTT_IMPL_CORE)
        idf_component_optional_requires(PRIVATE backoffAlgorithm coreMQTT-Agent)
    endif ()
    if (CONFIG_OSAL_DISCOVERY_DISCOVERY_TYPE_MDNS)
        idf_component_optional_requires(PRIVATE espressif__mdns)
    endif ()

    # esp_wifi / openthread are pulled only for the enabled network type (netstatus).
    if (CONFIG_ESP_WIFI_ENABLED
        OR CONFIG_ESP32_WIFI_ENABLED
        OR CONFIG_ESP_WIFI_REMOTE_ENABLED
    )
        idf_component_optional_requires(PRIVATE esp_wifi)
    endif ()
    if (CONFIG_ESP_WIFI_REMOTE_ENABLED)
        idf_component_optional_requires(PRIVATE esp_wifi_remote)
    endif ()
    if (CONFIG_OPENTHREAD_ENABLED)
        idf_component_optional_requires(PRIVATE openthread)
    endif ()

    # Normal CMake Platform ###
else ()
    list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/platform/posix/include" "${OSAL_DIR}/errors/include/posix"
         "${OSAL_DIR}/platform/src/posix"
    )
    if (APPLE)
        list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/platform/posix/bsd_include")
    endif ()

    list(
        APPEND
        OSAL_SRCS
        # platform (POSIX)
        "${OSAL_DIR}/platform/src/posix/ticks_posix.c"
        "${OSAL_DIR}/platform/src/posix/semaphore_posix.c"
        "${OSAL_DIR}/platform/src/posix/queue_posix.c"
        "${OSAL_DIR}/platform/src/posix/event_group_posix.c"
        "${OSAL_DIR}/platform/src/posix/task_posix.c"
        "${OSAL_DIR}/platform/src/posix/event_loop_posix.c"
        "${OSAL_DIR}/platform/src/posix/sysctrl_posix.c"
        "${OSAL_DIR}/platform/src/posix/random_posix.c"
        "${OSAL_DIR}/platform/src/posix/log_posix.c"
        # storage
        "${OSAL_DIR}/storage/src/nvs_posix_impl.c"
        # timesync
        "${OSAL_DIR}/timesync/src/time_posix.c"
        # netstatus
        "${OSAL_DIR}/netstatus/src/netstatus_posix.c"
        # console (esp_console API shim; native on ESP-IDF)
        "${OSAL_DIR}/console/src/esp_console_shim.c"
        # ca-bundle
        "${OSAL_DIR}/ca-bundle/src/ca_bundle_posix.c"
    )
    list(APPEND OSAL_INCLUDE_DIRS "${OSAL_DIR}/console/include" "${OSAL_DIR}/ca-bundle/include")
    if (OSAL_INCLUDE_HEAP_MONITOR)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/heap-monitor/src/heap_monitor_posix.c")
    endif ()

    # CA bundle: download the default bundle, optionally append a custom bundle from Kconfig, and embed it as binary
    # data (below, after the target exists).
    set(OSAL_CA_BUNDLE_CACRT_FILE ${CMAKE_CURRENT_BINARY_DIR}/cacrt_all.pem)
    include(${OSAL_DIR}/../../cmake/file_sync.cmake)
    download_github_file(
        "espressif" "esp-idf" "release/v5.5" "components/mbedtls/esp_crt_bundle/cacrt_all.pem"
        "${OSAL_CA_BUNDLE_CACRT_FILE}"
    )
    if (DEFINED CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE AND CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE)
        if (NOT DEFINED CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE_PATH)
            message(
                FATAL_ERROR
                    "[osal/ca-bundle] CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE_PATH must be set when CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE is enabled"
            )
        endif ()
        get_filename_component(
            OSAL_CUSTOM_CERTIFICATE_BUNDLE_PATH "${CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE_PATH}" ABSOLUTE
            BASE_DIR ${CMAKE_SOURCE_DIR}
        )
        if (NOT EXISTS "${OSAL_CUSTOM_CERTIFICATE_BUNDLE_PATH}")
            message(
                FATAL_ERROR
                    "[osal/ca-bundle] Custom certificate bundle file '${OSAL_CUSTOM_CERTIFICATE_BUNDLE_PATH}' not found"
            )
        endif ()
        file(READ "${OSAL_CUSTOM_CERTIFICATE_BUNDLE_PATH}" OSAL_CUSTOM_CERTIFICATE_BUNDLE_CONTENT)
        file(APPEND "${OSAL_CA_BUNDLE_CACRT_FILE}" "${OSAL_CUSTOM_CERTIFICATE_BUNDLE_CONTENT}")
    endif ()
    if (NOT OSAL_USING_VIRTUAL_SCHEDULER)
        list(APPEND OSAL_SRCS "${OSAL_DIR}/platform/src/posix/scheduler_posix.c")
    endif ()

    # Firmware version + embedded app descriptor from PROJECT_VER / PROJECT_NAME. The descriptor (with build-time CRC32)
    # lives in the same translation unit as the sysinfo getters so the linker keeps it in the final binary.
    if (NOT DEFINED PROJECT_VER)
        message(WARNING "PROJECT_VER is not defined, using 'unknown'")
        set(PROJECT_VER "unknown")
    endif ()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${OSAL_DIR}/platform/src/posix/app_desc_crc.py "${PROJECT_VER}" "${PROJECT_NAME}"
        OUTPUT_VARIABLE PROJECT_APP_DESC_CRC
        RESULT_VARIABLE _app_desc_crc_rc
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT _app_desc_crc_rc EQUAL 0)
        message(FATAL_ERROR "Failed to compute app descriptor CRC32 (rc=${_app_desc_crc_rc})")
    endif ()
    set(OSAL_SYSINFO_SRC ${CMAKE_CURRENT_BINARY_DIR}/platform/src/posix/sysinfo_posix.c)
    configure_file(${OSAL_DIR}/platform/src/posix/sysinfo_posix.c.in ${OSAL_SYSINFO_SRC} @ONLY)
    list(APPEND OSAL_SRCS ${OSAL_SYSINFO_SRC} "${OSAL_DIR}/platform/src/posix/sysinfo_mac_posix.c")

    add_library(${OSAL_NAME} STATIC ${OSAL_SRCS})
    target_include_directories(${OSAL_NAME} PUBLIC ${OSAL_INCLUDE_DIRS})
    target_include_directories(${OSAL_NAME} PRIVATE ${OSAL_PRIV_INCLUDE_DIRS})

    find_package(Threads REQUIRED)
    target_link_libraries(${OSAL_NAME} PRIVATE Threads::Threads)

    # Embed the CA bundle
    include(${OSAL_DIR}/../../cmake/target_add_binary_data.cmake)
    target_add_binary_data(${OSAL_NAME} "${OSAL_CA_BUNDLE_CACRT_FILE}" TEXT)

    # Discovery: pick the mDNS backend (Avahi > mDNSResponder > stub)
    set(OSAL_MDNS_SRC "${OSAL_DIR}/discovery/src/mdns_stub.c")
    find_package(PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(AVAHI avahi-client)
        if (AVAHI_FOUND)
            set(OSAL_MDNS_SRC "${OSAL_DIR}/discovery/src/mdns_avahi.c")
            target_compile_definitions(${OSAL_NAME} PRIVATE HAVE_AVAHI)
            target_include_directories(${OSAL_NAME} PRIVATE ${AVAHI_INCLUDE_DIRS})
            target_link_libraries(${OSAL_NAME} PRIVATE ${AVAHI_LIBRARIES})
            message(STATUS "osal/discovery: Using Avahi for mDNS")
        else ()
            set(AVAHI_FOUND FALSE)
        endif ()
    else ()
        set(AVAHI_FOUND FALSE)
    endif ()
    if (NOT AVAHI_FOUND)
        find_path(
            MDNSRESPONDER_INCLUDE_DIR dns_sd.h
            PATHS
                /usr/include
                /usr/local/include
                /opt/homebrew/include
                /System/Library/Frameworks/System.framework/Headers
                /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
                /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include
        )
        if (MDNSRESPONDER_INCLUDE_DIR)
            set(OSAL_MDNS_SRC "${OSAL_DIR}/discovery/src/mdns_sd.c")
            target_compile_definitions(${OSAL_NAME} PRIVATE HAVE_MDNSRESPONDER)
            target_include_directories(${OSAL_NAME} PRIVATE ${MDNSRESPONDER_INCLUDE_DIR})
            message(STATUS "osal/discovery: Using mDNSResponder for mDNS")
        else ()
            message(WARNING "osal/discovery: No Avahi or mDNSResponder; using mDNS stub")
        endif ()
    endif ()
    target_sources(${OSAL_NAME} PRIVATE ${OSAL_MDNS_SRC})

    # HTTP client (libcurl; optional). Internal sub-build — consumers only link osal.
    if (OSAL_INCLUDE_HTTP)
        add_subdirectory(http)
        target_link_libraries(${OSAL_NAME} PUBLIC osal_http)
    endif ()

    # OTA partition emulation + mock bootloader. Internal sub-build — consumers only link osal.
    add_subdirectory(ota)
    target_link_libraries(${OSAL_NAME} PUBLIC osal_ota)

    # External serial I/O (optional). Internal sub-build — consumers only link osal.
    if (OSAL_INCLUDE_EXT_IO)
        add_subdirectory(ext-io)
        target_link_libraries(${OSAL_NAME} PUBLIC osal_ext_io)
    endif ()

    # MQTT client. Internal sub-build — consumers only link osal.
    add_subdirectory(mqtt)
    target_link_libraries(${OSAL_NAME} PUBLIC osal_mqtt)

    # POSIX-compatible protocomm (vendored from ESP-IDF; CivetWeb HTTP transport). Separate static lib: consumers of the
    # control plane link protocomm-posix explicitly (see esp_rmaker_neo's local endpoints service); plain osal consumers
    # don't pay for it.
    add_subdirectory(protocomm)

    # Force include directory for non-Apple platforms
    if (NOT APPLE)
        target_compile_options(
            ${OSAL_NAME} PUBLIC -include "${OSAL_DIR}/platform/posix/non_bsd_include/queue_extended.h"
        )
    endif ()

    # Unit tests
    if (CMAKE_TESTING_ENABLED)
        include(${OSAL_DIR}/../../cmake/testing.cmake)
        testing_enable_coverage(${OSAL_NAME})

        add_subdirectory(platform/test-platform-common)
        add_subdirectory(storage/test-nvs-common)
        add_subdirectory(json/test-json-common)
        add_subdirectory(timesync/test-timesync-common)
        add_subdirectory(console/test-esp_console-posix)
    endif ()
endif ()

set(RMNG_COMMON_NAME esp_rmaker_neo_common)
set(RMNG_COMMON_SRCS
    # MQTT implementation
    "${CMAKE_CURRENT_LIST_DIR}/src/mqtt_impl.c"
    # Work queue
    "${CMAKE_CURRENT_LIST_DIR}/src/work_queue.c"
    # Runtime gate
    "${CMAKE_CURRENT_LIST_DIR}/src/runtime_gate.c"
    # Credentials
    "${CMAKE_CURRENT_LIST_DIR}/src/credentials/common.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/credentials/factory_part.c"
    # Utilities
    "${CMAKE_CURRENT_LIST_DIR}/src/util/nvs.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/util/hex_str.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/util/base64.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/util/crypto.c"
    # Retry
    "${CMAKE_CURRENT_LIST_DIR}/src/retry/backoff.c"
    # Common events + credentials provider (absorbed from rmng-common-ext)
    "${CMAKE_CURRENT_LIST_DIR}/src/common_events.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/credentials_provider.c"
)
set(RMNG_COMMON_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
set(RMNG_COMMON_PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/priv_include")
set(RMNG_COMMON_PRIV_REQUIRES mbedtls osal)
set(RMNG_COMMON_REQUIRES osal)

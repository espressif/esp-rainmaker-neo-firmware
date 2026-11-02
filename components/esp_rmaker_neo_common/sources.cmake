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

# rmaker_aws_sign osal port: the SigV4 signer and the IoT Credential Provider client that signed REST calls ride on.
# Defining it is unconditional; only consumption is gated on RMNG_AWS_SIGN_PORT, which a signing feature selects and
# CMakeLists.txt resolves. The port needs osal's HTTP client, which that same Kconfig symbol selects.
set(RMNG_AWS_SIGN_PORT_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/overrides/rmaker_aws_sign/port/esp_rmaker_aws_sign_port_osal.c"
    "${CMAKE_CURRENT_LIST_DIR}/overrides/rmaker_aws_sign/port/esp_rmaker_aws_sign_port_osal_http.c"
    "${CMAKE_CURRENT_LIST_DIR}/overrides/rmaker_aws_sign/port/esp_rmaker_aws_sign_port_osal_json.c"
)
set(RMNG_AWS_SIGN_PORT_PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/overrides/rmaker_aws_sign/port")

# The registry component keeps its namespace prefix in the target name; on POSIX the static lib built by
# overrides/rmaker_aws_sign/CMakeLists.txt is plain rmaker_aws_sign.
if (ESP_PLATFORM)
    set(RMNG_AWS_SIGN_PORT_PRIV_REQUIRES espressif__rmaker_aws_sign)
else ()
    set(RMNG_AWS_SIGN_PORT_PRIV_REQUIRES rmaker_aws_sign)
endif ()

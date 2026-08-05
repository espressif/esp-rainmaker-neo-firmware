include("${CMAKE_CURRENT_LIST_DIR}/coreMQTTAgentDeps.cmake")

# ESP-IDF Platform ### Transport: esp-aws-iot's esp-tls network_transport. esp_rmaker_neo no longer maintains an ESP
# transport port.
if (ESP_PLATFORM)
    list(APPEND COREMQTT_AGENT_REQUIRES esp-tls)
    list(APPEND COREMQTT_AGENT_PRIV_REQUIRES mbedtls)
    idf_component_register(
        SRCS "${CMAKE_CURRENT_LIST_DIR}/placeholder.c"
        REQUIRES ${COREMQTT_AGENT_REQUIRES}
        PRIV_REQUIRES ${COREMQTT_AGENT_PRIV_REQUIRES}
    )

    include("${CMAKE_CURRENT_LIST_DIR}/coreMQTTAgentExt.cmake")
    # esp-aws-iot esp-tls transport + the ESP-only part of the transport port shim.
    list(APPEND COREMQTT_AGENT_SRCS "${RMNG_EAI_COREMQTT_PORT_DIR}/network_transport.c"
         "${CMAKE_CURRENT_LIST_DIR}/port/esp/network_transport_port.c"
    )
    target_sources(${COMPONENT_LIB} PRIVATE ${COREMQTT_AGENT_SRCS})
    target_include_directories(
        ${COMPONENT_LIB} PUBLIC ${COREMQTT_AGENT_LIB_INCLUDE_DIRS} "${RMNG_EAI_COREMQTT_PORT_DIR}"
    )

    # Normal CMake Platform (POSIX) ###
else ()
    # Transport: mbedtls + BSD-socket posix port (no esp-aws-iot host support exists).
    include("${CMAKE_CURRENT_LIST_DIR}/coreMQTTAgentExt.cmake")
    list(APPEND COREMQTT_AGENT_SRCS "${CMAKE_CURRENT_LIST_DIR}/port/posix/network_transport.c"
         "${CMAKE_CURRENT_LIST_DIR}/port/posix/network_transport_port.c"
    )

    # Add mbedtls library
    include(${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/install_scripts/mbedtls.cmake)
    mbedtls_make_available()
    list(APPEND COREMQTT_AGENT_PRIV_REQUIRES mbedtls osal)

    add_library(${COREMQTT_AGENT_NAME} STATIC ${COREMQTT_AGENT_SRCS})
    target_include_directories(
        ${COREMQTT_AGENT_NAME} PUBLIC ${COREMQTT_AGENT_LIB_INCLUDE_DIRS} "${CMAKE_CURRENT_LIST_DIR}/port/posix"
    )
    target_link_libraries(${COREMQTT_AGENT_NAME} PRIVATE ${COREMQTT_AGENT_PRIV_REQUIRES})
endif ()

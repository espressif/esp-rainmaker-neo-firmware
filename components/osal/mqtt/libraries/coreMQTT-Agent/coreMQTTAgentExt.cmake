# Locate coreMQTT-Agent + coreMQTT inside esp-aws-iot.
include("${CMAKE_CURRENT_LIST_DIR}/../makeAvailable.cmake")

# This gives MQTT_AGENT_INCLUDE_PUBLIC_DIRS and MQTT_AGENT_SOURCES
include("${RMNG_EAI_AGENT_SRC}/mqttAgentFilePaths.cmake")

# This gives MQTT_SOURCES, MQTT_SERIALIZER_SOURCES and MQTT_INCLUDE_PUBLIC_DIRS
include("${RMNG_EAI_COREMQTT_SRC}/mqttFilePaths.cmake")

set(COREMQTT_AGENT_NAME "coreMQTT-Agent")

# coreMQTT / config headers.
set(COREMQTT_AGENT_CONFIG_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/config)

# Agent message + command-pool glue. esp_rmaker_neo keeps its own osal-backed glue on both targets (osal provides the
# FreeRTOS backend on ESP), so this is not target split.
set(COREMQTT_AGENT_GLUE_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/port/common)
set(COREMQTT_AGENT_GLUE_SRCS ${CMAKE_CURRENT_LIST_DIR}/port/common/core_agent_message.c
                             ${CMAKE_CURRENT_LIST_DIR}/port/common/core_agent_command_pool.c
)

# Public include dirs for the AWS library sources (shared across targets). The transport header include dir is target
# specific and appended in CMakeLists.txt.
set(COREMQTT_AGENT_LIB_INCLUDE_DIRS ${MQTT_INCLUDE_PUBLIC_DIRS} ${MQTT_AGENT_INCLUDE_PUBLIC_DIRS}
                                    ${COREMQTT_AGENT_CONFIG_INCLUDE_DIRS} ${COREMQTT_AGENT_GLUE_INCLUDE_DIRS}
)

set(COREMQTT_AGENT_SRCS ${MQTT_SOURCES} ${MQTT_SERIALIZER_SOURCES} ${MQTT_AGENT_SOURCES} ${COREMQTT_AGENT_GLUE_SRCS})

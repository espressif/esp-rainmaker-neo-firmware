# Internal osal sub-build: the POSIX MQTT client (coreMQTT-Agent based). Added via add_subdirectory() from the osal
# CMakeLists on POSIX only; on ESP-IDF the sources are compiled directly into the osal component (see
# components/osal/CMakeLists.txt).
if (ESP_PLATFORM)
    message(FATAL_ERROR "osal/mqtt is added as a subdirectory on POSIX only")
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/osal_mqtt.cmake")

# Only the core-MQTT implementation is supported for the normal CMake platform.
list(APPEND OSAL_MQTT_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/osal_mqtt_core_impl.c")
set(OSAL_MQTT_PRIV_REQUIRES backoffAlgorithm coreMQTT-Agent)

add_subdirectory(libraries/backoffAlgorithm)
add_subdirectory(libraries/coreMQTT-Agent)

add_library(${OSAL_MQTT_NAME} STATIC ${OSAL_MQTT_SRCS})
target_include_directories(${OSAL_MQTT_NAME} PUBLIC ${OSAL_MQTT_INCLUDE_DIRS})
target_link_libraries(${OSAL_MQTT_NAME} PUBLIC ${OSAL_MQTT_REQUIRES})
target_link_libraries(${OSAL_MQTT_NAME} PRIVATE ${OSAL_MQTT_PRIV_REQUIRES})

# DO NOT add test-mqtt-common here. It is added by the test app that needs it.

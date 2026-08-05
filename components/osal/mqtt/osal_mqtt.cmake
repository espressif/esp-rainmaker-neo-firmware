set(OSAL_MQTT_NAME osal_mqtt)
set(OSAL_MQTT_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/src/osal_mqtt_events.c" "${CMAKE_CURRENT_LIST_DIR}/src/osal_mqtt_util.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/osal_mqtt_subscription_manager.c" "${CMAKE_CURRENT_LIST_DIR}/src/osal_mqtt_impl.c"
)
set(OSAL_MQTT_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
set(OSAL_MQTT_REQUIRES osal)

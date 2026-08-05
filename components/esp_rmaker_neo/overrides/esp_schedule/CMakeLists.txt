# esp_schedule is used through its porting interface: the SDK supplies an osal-backed port and calls
# esp_schedule_init_with_config() instead of esp_schedule_init(). That keeps schedule timers on osal_scheduler — so
# firmware tests can drive them from the virtual scheduler — and keeps the component's built-in ESP-IDF port, with its
# FreeRTOS timer / nvs_flash / esp_log dependencies, out of the link.
#
# The port itself lives in esp_rmaker_neo (see its sources.cmake) because it is SDK code, not part of the component.
#
# POSIX only. On ESP-IDF the managed component registers itself and the port is selected at runtime rather than at link
# time, so there is nothing for this file to do and esp_rmaker_neo/CMakeLists.txt does not include it.
include(${CMAKE_CURRENT_LIST_DIR}/component_dir_setup.cmake)

# Platform-independent half of the component. Its own CMakeLists.txt is ESP-IDF-only, so the source list comes from the
# variables file the component publishes for exactly this purpose. src/esp_schedule_default.c is deliberately excluded
# there: it is the only referrer of the built-in ESP-IDF port.
include(${ESP_SCHEDULE_COMPONENT_DIR}/esp_schedule_variables.cmake)

# esp_daylight is compiled in directly rather than linked as a separate component.
list(APPEND ESP_SCHEDULE_SRCS "${ESP_DAYLIGHT_COMPONENT_DIR}/src/esp_daylight.c")
list(APPEND ESP_SCHEDULE_INCLUDE_DIRS "${ESP_DAYLIGHT_COMPONENT_DIR}/include")
list(REMOVE_ITEM ESP_SCHEDULE_PRIV_REQUIRES "esp_daylight")

# The component's public header includes esp_err.h, which off-target is osal's shim
# (components/osal/errors/include/posix). Linked PUBLIC so users of the header get it too. The component has no other
# osal dependency — the port does, and that lives in esp_rmaker_neo.
list(APPEND ESP_SCHEDULE_REQUIRES osal)

set(ESP_SCHEDULE_NAME esp_schedule)
add_library(${ESP_SCHEDULE_NAME} STATIC ${ESP_SCHEDULE_SRCS})
target_include_directories(${ESP_SCHEDULE_NAME} PUBLIC ${ESP_SCHEDULE_INCLUDE_DIRS})
target_include_directories(${ESP_SCHEDULE_NAME} PRIVATE ${ESP_SCHEDULE_PRIV_INCLUDE_DIRS})
target_link_libraries(${ESP_SCHEDULE_NAME} PUBLIC ${ESP_SCHEDULE_REQUIRES})
target_link_libraries(${ESP_SCHEDULE_NAME} PRIVATE ${ESP_SCHEDULE_PRIV_REQUIRES} m)
if (NOT APPLE)
    target_link_libraries(${ESP_SCHEDULE_NAME} PRIVATE bsd)
endif ()

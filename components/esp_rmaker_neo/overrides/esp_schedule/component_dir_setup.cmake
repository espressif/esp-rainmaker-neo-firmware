# Get required registry components
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/esp_component_manager.cmake)
rmng_idf_components_import(ESP_COMMON_COMPONENTS_DIR DEPENDENCIES "espressif/esp_schedule:1.5.0")

# Get the component directories. esp_daylight is not requested above -- it arrives as a transitive dependency of
# esp_schedule, which the recursive sync resolves.
set(ESP_SCHEDULE_COMPONENT_DIR ${ESP_COMMON_COMPONENTS_DIR}/espressif__esp_schedule)
set(ESP_DAYLIGHT_COMPONENT_DIR ${ESP_COMMON_COMPONENTS_DIR}/espressif__esp_daylight)

# Copy Kconfig file from esp_schedule component to the current directory
set(ESP_SCHEDULE_KCONFIG ${CMAKE_CURRENT_LIST_DIR}/Kconfig)
file(COPY ${ESP_SCHEDULE_COMPONENT_DIR}/Kconfig DESTINATION ${CMAKE_CURRENT_LIST_DIR})

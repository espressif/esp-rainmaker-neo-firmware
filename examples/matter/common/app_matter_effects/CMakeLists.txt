if (NOT ESP_PLATFORM)
    message(FATAL_ERROR "app_matter_effects is ESP-IDF only (it needs the Matter mirror)")
endif ()

set(APP_MATTER_EFFECTS_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/app_matter_effects.c")
set(APP_MATTER_EFFECTS_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

# app_led, the mirror and osal_err.h are all part of the public app_matter_effects.h surface
set(APP_MATTER_EFFECTS_REQUIRES "app_led" "esp_rmaker_neo_matter_mirror" "osal")

idf_component_register(
    SRCS ${APP_MATTER_EFFECTS_SRCS}
    INCLUDE_DIRS ${APP_MATTER_EFFECTS_INCLUDE_DIRS}
    REQUIRES ${APP_MATTER_EFFECTS_REQUIRES}
)

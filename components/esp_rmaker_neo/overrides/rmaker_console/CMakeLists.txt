# Vendored espressif/rmaker_console.
#
# On ESP-IDF this is consumed as a managed component (declared in components/esp_rmaker_neo/idf_component.yml and pulled
# in via PRIV_REQUIRES) so nothing is built here.
#
# On POSIX we build the upstream REPL source (rmaker_console.c, whose CONFIG_IDF_TARGET_LINUX path uses only the
# esp_console API) against the osal console shim, plus a portable glue implementation of the common commands. This
# yields the same esp_rmaker_common_console_init() entry point on both platforms so the SDK console code stays unified.

if (ESP_PLATFORM)
    # Managed component handles its own build/registration on ESP-IDF.
    return()
endif ()

# Normal CMake Platform ###

# Download the espressif/rmaker_console managed component (and its transitive deps) without IDF.
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/esp_component_manager.cmake)
rmng_idf_components_import(ESP_COMMON_COMPONENTS_DIR DEPENDENCIES "espressif/rmaker_console:1.0.0")

set(RMAKER_CONSOLE_COMPONENT_DIR ${ESP_COMMON_COMPONENTS_DIR}/espressif__rmaker_console)

set(RMAKER_CONSOLE_NAME rmaker_console)
set(RMAKER_CONSOLE_SRCS ${RMAKER_CONSOLE_COMPONENT_DIR}/src/rmaker_console.c
                        ${CMAKE_CURRENT_LIST_DIR}/port/rmaker_common_cmds_posix.c
)

add_library(${RMAKER_CONSOLE_NAME} STATIC ${RMAKER_CONSOLE_SRCS})

# Empty freertos/* compatibility headers (for the upstream Linux path's unconditional FreeRTOS includes) come
# transitively from osal's posix/idf_compat_include.
target_include_directories(${RMAKER_CONSOLE_NAME} PUBLIC ${RMAKER_CONSOLE_COMPONENT_DIR}/include)

# Select the upstream CONFIG_IDF_TARGET_LINUX path (esp_console REPL, no UART/FreeRTOS driver), enable the console, and
# pick the UART REPL device (the osal console shim maps it to stdin).
target_compile_definitions(
    ${RMAKER_CONSOLE_NAME} PRIVATE CONFIG_IDF_TARGET_LINUX=1 CONFIG_ESP_RMAKER_CONSOLE_ENABLED=1
                                   CONFIG_ESP_CONSOLE_UART_DEFAULT=1
)

target_link_libraries(${RMAKER_CONSOLE_NAME} PUBLIC osal)

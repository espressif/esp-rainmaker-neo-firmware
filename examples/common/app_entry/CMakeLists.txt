set(APP_ENTRY_NAME "app_entry")

set(APP_ENTRY_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    # ESP entry is a thin app_main() shim; it only needs the shared error/log headers.
    idf_component_register(
        SRCS "${CMAKE_CURRENT_LIST_DIR}/src/app_entry_esp.c"
        INCLUDE_DIRS ${APP_ENTRY_INCLUDE_DIRS}
        REQUIRES "osal"
    )
    set(APP_ENTRY_NAME ${COMPONENT_TARGET})

    # Normal CMake Platform ###
else ()
    # POSIX entry owns main() + the generic SDK teardown, so it links esp_rmaker_neo.
    add_library(${APP_ENTRY_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/src/app_entry_posix.c")
    target_include_directories(${APP_ENTRY_NAME} PUBLIC ${APP_ENTRY_INCLUDE_DIRS})
    foreach (LIBRARY osal esp_rmaker_neo)
        if (NOT TARGET ${LIBRARY})
            message(FATAL_ERROR "Target '${LIBRARY}' not found. Ensure it is added before this component (app_entry).")
        endif ()
    endforeach ()
    target_link_libraries(
        ${APP_ENTRY_NAME}
        PUBLIC osal
        PRIVATE esp_rmaker_neo
    )
endif ()

set(APP_EVENT_LOOP_NAME "app_event_loop")

set(APP_EVENT_LOOP_SRC "${CMAKE_CURRENT_LIST_DIR}/src/app_event_loop.c")
set(APP_EVENT_LOOP_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
set(APP_EVENT_LOOP_PRIV_REQUIRES "esp_rmaker_neo" "esp_rmaker_neo_ota")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${APP_EVENT_LOOP_SRC}
        INCLUDE_DIRS ${APP_EVENT_LOOP_INCLUDE_DIRS}
        PRIV_REQUIRES ${APP_EVENT_LOOP_PRIV_REQUIRES}
    )
    set(APP_EVENT_LOOP_NAME ${COMPONENT_TARGET})

    # Normal CMake Platform ###
else ()
    add_library(${APP_EVENT_LOOP_NAME} STATIC ${APP_EVENT_LOOP_SRC})
    target_include_directories(${APP_EVENT_LOOP_NAME} PUBLIC ${APP_EVENT_LOOP_INCLUDE_DIRS})
    foreach (LIBRARY ${APP_EVENT_LOOP_PRIV_REQUIRES})
        if (NOT TARGET ${LIBRARY})
            message(
                FATAL_ERROR "Target '${LIBRARY}' not found. Ensure it is added before this component (app_event_loop)."
            )
        endif ()
    endforeach ()
    target_link_libraries(${APP_EVENT_LOOP_NAME} PRIVATE ${APP_EVENT_LOOP_PRIV_REQUIRES})
endif ()

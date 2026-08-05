# OTA Jobs FSM tests: include ota_jobs.c in test to access static handlers
include(${CMAKE_CURRENT_LIST_DIR}/../requires.cmake)

# ../sources.cmake fetches the AWS IoT embedded SDK deps via FetchContent, which cannot run during ESP-IDF's early
# expansion pass (component CMakeLists are executed with `cmake -P`, where define_property is not scriptable and the
# deps/ sources do not exist yet). Register requirements only in that pass; sources are added on the real configure.
if (ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(PRIV_REQUIRES ${RMNG_OTA_REQUIRES} ${RMNG_OTA_PRIV_REQUIRES} unity)
    return()
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

get_filename_component(RMNG_OTA_JOBS_C "${CMAKE_CURRENT_LIST_DIR}/../src/ota_jobs.c" ABSOLUTE)
list(REMOVE_ITEM RMNG_OTA_SRCS ${RMNG_OTA_JOBS_C})

# ota_mqtt_downloader_bitmap.c is part of mqtt_downloader.cmake (MQTT-transport-only). Add it unconditionally for the
# test binary so bitmap tests run regardless of transport config.
get_filename_component(
    RMNG_OTA_MQTT_BITMAP_C "${CMAKE_CURRENT_LIST_DIR}/../src/image/ota_mqtt_downloader_bitmap.c" ABSOLUTE
)
list(REMOVE_ITEM RMNG_OTA_SRCS ${RMNG_OTA_MQTT_BITMAP_C})

set(RMNG_OTA_TEST_OWN_SRCS
    "tests_src/test_ota_jobs.c"
    "tests_src/test_ota_filetype_version.c"
    "tests_src/test_ota_status.c"
    "tests_src/test_ota_image_progress.c"
    "tests_src/test_ota_resume_nvs.c"
    "tests_src/test_ota_mqtt_bitmap.c"
    "tests_src/test_ota_image_handler.c"
    "test_all.c"
)
# Sources owned by esp_rmaker_neo_ota but compiled into this test component
set(RMNG_OTA_TEST_FOREIGN_SRCS "${RMNG_OTA_MQTT_BITMAP_C}")
set(RMNG_OTA_TEST_SRCS ${RMNG_OTA_TEST_OWN_SRCS} ${RMNG_OTA_TEST_FOREIGN_SRCS})

set(RMNG_OTA_TEST_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
set(RMNG_OTA_TEST_PRIV_INCLUDE_DIRS ${RMNG_OTA_INCLUDE_DIRS} ${RMNG_OTA_PRIV_INCLUDE_DIRS}
                                    "${CMAKE_CURRENT_LIST_DIR}/../src"
)

if (ESP_PLATFORM)
    # esp_rmaker_neo_ota's own sources and dirs are attached after register: IDF's component validation inspects
    # register-time properties only, and would otherwise warn about this component building/borrowing them.
    idf_component_register(
        SRCS ${RMNG_OTA_TEST_OWN_SRCS}
        INCLUDE_DIRS ${RMNG_OTA_TEST_INCLUDE_DIRS}
        PRIV_REQUIRES ${RMNG_OTA_REQUIRES} ${RMNG_OTA_PRIV_REQUIRES} unity
        WHOLE_ARCHIVE
    )
    target_sources(${COMPONENT_LIB} PRIVATE ${RMNG_OTA_TEST_FOREIGN_SRCS} ${RMNG_OTA_SRCS})
    target_include_directories(${COMPONENT_LIB} PRIVATE ${RMNG_OTA_TEST_PRIV_INCLUDE_DIRS})
    foreach (OPT_PRIV_REQUIRE ${RMNG_OTA_OPT_PRIV_REQUIRES})
        idf_component_optional_requires(PRIVATE ${OPT_PRIV_REQUIRE})
    endforeach ()

    # Wrap the functions in the library
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RMAKER_OTA_JOBS_TEST_WRAP_LINKER=1)
    target_link_options(
        ${COMPONENT_LIB}
        PUBLIC
        "-Wl,--wrap=esp_rmaker_work_queue_add_task"
        "-Wl,--wrap=osal_event_post"
        "-Wl,--wrap=esp_rmaker_backoff_retry"
        "-Wl,--wrap=osal_scheduler_schedule_task"
        "-Wl,--wrap=osal_scheduler_reset_timer"
    )
else ()
    if (NOT TARGET unity::framework)
        include(${CMAKE_CURRENT_LIST_DIR}/../../../third_party/install_scripts/unity_make_available.cmake)
        unity_make_available()
    endif ()

    set(RMNG_OTA_TEST_NAME rmng-ota-tests)
    add_executable(${RMNG_OTA_TEST_NAME} ${RMNG_OTA_TEST_SRCS} test_runner_posix.c ${RMNG_OTA_SRCS})

    target_include_directories(${RMNG_OTA_TEST_NAME} PUBLIC ${RMNG_OTA_TEST_INCLUDE_DIRS})
    target_include_directories(${RMNG_OTA_TEST_NAME} PRIVATE ${RMNG_OTA_TEST_PRIV_INCLUDE_DIRS})

    target_link_libraries(
        ${RMNG_OTA_TEST_NAME} PRIVATE ${RMNG_OTA_REQUIRES} ${RMNG_OTA_PRIV_REQUIRES} ${RMNG_OTA_OPT_PRIV_REQUIRES}
                                      unity::framework
    )

    # Force-enable custom-job branch in the test TU only so we can exercise the alternative memcpy/null-term path in
    # handle_state_job_doc_received. The test TU is the sole compiler of ota_jobs.c (REMOVE_ITEM above); other lib TUs
    # are unaffected.
    target_compile_definitions(${RMNG_OTA_TEST_NAME} PRIVATE CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT=1)

    add_test(NAME ${RMNG_OTA_TEST_NAME} COMMAND ${RMNG_OTA_TEST_NAME})

    # Strategy A (Linux/ESP): linker --wrap. Strategy B (Apple): stub object with real symbol names linked first.
    if (APPLE)
        message(STATUS "OTA Jobs tests: Platform is macOS: Using stub object (real symbol names) strategy.")
        add_library(ota_job_test_stubs_apple SHARED "${CMAKE_CURRENT_LIST_DIR}/tests_src/ota_job_test_stubs_apple.c")
        include(${CMAKE_CURRENT_LIST_DIR}/ota_jobs_stub_headers.cmake)
        target_include_directories(ota_job_test_stubs_apple PRIVATE ${OTA_JOBS_STUB_HEADERS})
        target_compile_definitions(${RMNG_OTA_TEST_NAME} PRIVATE RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB=1)
        if (CMAKE_DL_LIBS)
            target_link_libraries(ota_job_test_stubs_apple PRIVATE ${CMAKE_DL_LIBS})
        endif ()
        target_link_libraries(
            ota_job_test_stubs_apple PRIVATE ${RMNG_OTA_REQUIRES} ${RMNG_OTA_PRIV_REQUIRES}
                                             ${RMNG_OTA_OPT_PRIV_REQUIRES}
        )
        # TEST_OTA_JOBS_DO_NOT_WRAP is defined in test_ota_jobs.c (main test exe). Resolve at load time.
        target_link_options(ota_job_test_stubs_apple PRIVATE "LINKER:-undefined,dynamic_lookup")
        target_link_options(${RMNG_OTA_TEST_NAME} PRIVATE "LINKER:-flat_namespace,-interposable")
        set_tests_properties(
            ${RMNG_OTA_TEST_NAME}
            PROPERTIES ENVIRONMENT
                       "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ota_job_test_stubs_apple>;DYLD_FORCE_FLAT_NAMESPACE=1"
        )
    else ()
        message(STATUS "OTA Jobs tests: Using linker --wrap strategy.")
        target_compile_definitions(${RMNG_OTA_TEST_NAME} PRIVATE RMAKER_OTA_JOBS_TEST_WRAP_LINKER=1)

        target_link_options(
            ${RMNG_OTA_TEST_NAME}
            PRIVATE
            "LINKER:--wrap=esp_rmaker_work_queue_add_task"
            "LINKER:--wrap=osal_event_post"
            "LINKER:--wrap=esp_rmaker_backoff_retry"
            "LINKER:--wrap=osal_scheduler_schedule_task"
            "LINKER:--wrap=osal_scheduler_reset_timer"
        )
    endif ()
endif ()

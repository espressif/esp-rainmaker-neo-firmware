# makeAvailable.cmake fetches esp-aws-iot; backoffAlgorithmFilePaths.cmake then gives
# BACKOFF_ALGORITHM_INCLUDE_PUBLIC_DIRS and BACKOFF_ALGORITHM_SOURCES. Both must run after idf_component_register on
# ESP, since the requirement-expansion pass can't fetch.

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/placeholder.c")

    include("${CMAKE_CURRENT_LIST_DIR}/../makeAvailable.cmake")
    include("${RMNG_EAI_BACKOFF_SRC}/backoffAlgorithmFilePaths.cmake")

    target_sources(${COMPONENT_LIB} PRIVATE ${BACKOFF_ALGORITHM_SOURCES})
    target_include_directories(${COMPONENT_LIB} PUBLIC ${BACKOFF_ALGORITHM_INCLUDE_PUBLIC_DIRS})

    # Normal CMake platform ###
else ()
    include("${CMAKE_CURRENT_LIST_DIR}/../makeAvailable.cmake")
    include("${RMNG_EAI_BACKOFF_SRC}/backoffAlgorithmFilePaths.cmake")

    set(BACKOFF_ALGORITHM_NAME backoffAlgorithm)
    add_library(${BACKOFF_ALGORITHM_NAME} STATIC ${BACKOFF_ALGORITHM_SOURCES})
    target_include_directories(${BACKOFF_ALGORITHM_NAME} PUBLIC ${BACKOFF_ALGORITHM_INCLUDE_PUBLIC_DIRS})
endif ()

set(RMNG_SOCKET_MOCKS_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    set(RMNG_SOCKET_MOCKS_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/lwip.c")
    idf_component_register(
        SRCS ${RMNG_SOCKET_MOCKS_SRCS}
        INCLUDE_DIRS ${RMNG_SOCKET_MOCKS_INCLUDE_DIRS}
        WHOLE_ARCHIVE
    )

    # Normal CMake Platform ###
else ()
    set(SOCKET_MOCKS_NAME socket_mocks)
    set(RMNG_SOCKET_MOCKS_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/standard.c")
    add_library(${SOCKET_MOCKS_NAME} OBJECT ${RMNG_SOCKET_MOCKS_SRCS})
    target_include_directories(${SOCKET_MOCKS_NAME} PUBLIC ${RMNG_SOCKET_MOCKS_INCLUDE_DIRS})

    if (APPLE)
        message(STATUS "Socket mocks: Platform is macOS: Using dlsym() runtime mocking strategy.")

        # DO NOT define USE_LINKER_WRAP
        if (CMAKE_DL_LIBS)
            target_link_libraries(${SOCKET_MOCKS_NAME} PRIVATE ${CMAKE_DL_LIBS})
        endif ()

    elseif (MSVC)
        message(FATAL_ERROR "Socket mocks: This mocking method works on POSIX, not Windows MSVC yet.")

    else ()
        # --- Linux (GCC/Clang) ---
        message(STATUS "Socket mocks: Platform is Linux/ESP: Using linker --wrap strategy.")
        target_compile_definitions(${SOCKET_MOCKS_NAME} PRIVATE USE_LINKER_WRAP)
    endif ()
endif ()

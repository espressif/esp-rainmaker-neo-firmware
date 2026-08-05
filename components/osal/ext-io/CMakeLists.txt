# Internal osal sub-build: the POSIX (PTY-based) external serial I/O implementation. Added via add_subdirectory() from
# the osal CMakeLists on POSIX only, when OSAL_INCLUDE_EXT_IO is enabled; on ESP-IDF the UART implementation is compiled
# directly into the osal component (see components/osal/CMakeLists.txt).
if (ESP_PLATFORM)
    message(FATAL_ERROR "osal/ext-io is added as a subdirectory on POSIX only")
endif ()

set(OSAL_EXT_IO_NAME osal_ext_io)

add_library(${OSAL_EXT_IO_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/src/osal_ext_io_posix.c")
target_include_directories(${OSAL_EXT_IO_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(${OSAL_EXT_IO_NAME} PRIVATE osal)

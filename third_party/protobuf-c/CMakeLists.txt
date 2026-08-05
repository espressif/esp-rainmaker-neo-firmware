# Wrapper for CMake to use the protobuf-c library. The sources are fetched from https://github.com/protobuf-c/protobuf-c
# into protobuf-c/ and remain under their upstream BSD-2-Clause license (see the fetched tree's LICENSE).

set(PROTOBUF_C_NAME protobuf-c)
set(PROTOBUF_C_SRCS ${CMAKE_CURRENT_LIST_DIR}/protobuf-c/protobuf-c/protobuf-c.c)
set(PROTOBUF_C_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/protobuf-c)

add_library(${PROTOBUF_C_NAME} STATIC ${PROTOBUF_C_SRCS})
target_include_directories(${PROTOBUF_C_NAME} PUBLIC ${PROTOBUF_C_INCLUDE_DIRS})

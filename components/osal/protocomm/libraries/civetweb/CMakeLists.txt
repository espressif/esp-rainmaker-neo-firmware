# This is intended to be copied into the civetweb root folder and built there, replacing CivetWeb's own root
# CMakeLists.txt with a minimal static-lib build. CivetWeb itself is fetched from https://github.com/civetweb/civetweb
# and remains under its upstream MIT license (see the fetched tree's LICENSE.md).

cmake_minimum_required(VERSION 3.16)

# Create the CivetWeb library
add_library(civetweb STATIC ${CMAKE_CURRENT_LIST_DIR}/src/civetweb.c)

# Include directories
target_include_directories(civetweb PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Compile definitions for minimal configuration
target_compile_definitions(civetweb PRIVATE NO_SSL NO_CGI NO_FILES NO_CACHING)

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(civetweb PRIVATE Threads::Threads)

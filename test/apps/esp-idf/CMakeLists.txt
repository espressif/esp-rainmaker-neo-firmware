# The following five lines of boilerplate have to be in your project's CMakeLists in this exact order for cmake to work
# correctly
cmake_minimum_required(VERSION 3.16)

# Component dependencies are pulled in via main/idf_component.yml

# The linux target lacks most chip drivers, so build only what main needs there.
if (IDF_TARGET STREQUAL "linux")
    set(COMPONENTS main)
endif ()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test-app-esp-idf)

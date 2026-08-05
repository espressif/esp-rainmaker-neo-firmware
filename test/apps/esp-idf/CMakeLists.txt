# The following five lines of boilerplate have to be in your project's CMakeLists in this exact order for cmake to work
# correctly
cmake_minimum_required(VERSION 3.16)

# Component dependencies are pulled in via main/idf_component.yml
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rmng-sdk-testapp)

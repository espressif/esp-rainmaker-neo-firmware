cmake_minimum_required(VERSION 3.16)

# The KVS WebRTC engine and media pipeline come from the esp-port-for-amazon-kvs-sdk repo.
if (NOT DEFINED ENV{KVS_SDK_PATH})
    message(
        FATAL_ERROR
            "KVS_SDK_PATH not set in environment. Please set KVS_SDK_PATH to your local clone of esp-port-for-amazon-kvs-sdk"
    )
endif ()

# Define the firmware version (done before project())
set(PROJECT_VER
    "1.0.0"
    CACHE STRING "Firmware version"
)

# Force minimal build
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(camera_standalone)

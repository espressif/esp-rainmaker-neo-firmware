cmake_minimum_required(VERSION 3.16)

# The KVS signaling client and the SDIO bridge to the streaming chip come from the esp-port-for-amazon-kvs-sdk repo.
if (NOT DEFINED ENV{KVS_SDK_PATH})
    message(
        FATAL_ERROR
            "KVS_SDK_PATH not set in environment. Please set KVS_SDK_PATH to your local clone of esp-port-for-amazon-kvs-sdk"
    )
endif ()

# Signaling half of the split: tells webrtc_bridge / esp_webrtc_utils which side of the bridge this image is.
option(ENABLE_SIGNALLING_ONLY "enable only the signalling component" ON)
option(ENABLE_STREAMING_ONLY "enable only the streaming component" OFF)

# Define the firmware version (done before project())
set(PROJECT_VER
    "1.0.0"
    CACHE STRING "Firmware version"
)

# Force minimal build
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rmaker_split_camera)

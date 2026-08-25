cmake_minimum_required(VERSION 3.23)

# aCore only exports architecture-level CMSIS Core support.  MCU Device,
# startup and system sources belong to the selected aDrv port.
add_library(aCore INTERFACE)

target_include_directories(aCore INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Include"
)

cmake_minimum_required(VERSION 3.23)

# aCore only exports architecture-level CMSIS Core support.  MCU Device,
# startup and system sources belong to the selected aDrv port.
if(TOOLCHAIN_NAME STREQUAL "GCC")
    add_library(
        aCore
        OBJECT
            runtime/GCC/syscalls.c
            runtime/GCC/sysmem.c
    )

    target_include_directories(
        aCore
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/Core/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/GCC/include"
    )

    target_link_libraries(
        aCore
        PUBLIC
            aLib
    )
else()
    add_library(aCore INTERFACE)

    target_include_directories(
        aCore
        INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/Core/Include"
    )
endif()

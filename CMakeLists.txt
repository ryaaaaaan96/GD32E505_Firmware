cmake_minimum_required(VERSION 3.23)

include(cmake/Aclass.cmake)

aclass_select(
    NAME gd32e505vet7_debug
    VERSION 0.1.0
    MCU gd32e505
    LINKER_SCRIPT GD32E505_flash.ld
    TOOLCHAIN GCC
)

# Optional configuration profile for reproducible product/matrix builds.
if(NOT DEFINED ACLASS_CONFIG_FILE)
    set(ACLASS_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/aclass_config.cmake")
endif()
include("${ACLASS_CONFIG_FILE}")
include(cmake/aclass_resolve.cmake)

project(
    ${FIRMWARE_NAME}
    VERSION ${FIRMWARE_VERSION}
    LANGUAGES C ASM
)

aclass_initialize()

add_subdirectory(platform)
add_subdirectory(device)
add_subdirectory(func)
add_subdirectory(app)

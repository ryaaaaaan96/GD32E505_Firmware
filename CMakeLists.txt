cmake_minimum_required(VERSION 3.23)

include(cmake/Aclass.cmake)

aclass_select(
    NAME io_gd32e505
    VERSION 0.1.0
    MCU gd32e505
    LINKER_SCRIPT GD32E505_flash.ld
    TOOLCHAIN GCC
)

project(${FIRMWARE_NAME} VERSION ${FIRMWARE_VERSION} LANGUAGES C ASM)
aclass_initialize()

add_subdirectory(platform)
add_subdirectory(device)
add_subdirectory(func)
add_subdirectory(app)

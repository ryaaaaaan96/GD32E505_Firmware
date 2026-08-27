cmake_minimum_required(VERSION 3.23)

include(cmake/Aclass.cmake)

aclass_select(
    NAME gd32e505vet7_debug
    VERSION 0.1.0
    MCU gd32e505
    LINKER_SCRIPT GD32E505_flash.ld
    TOOLCHAIN GCC
)

# Board clock: a 20 MHz external active oscillator drives HXTAL_IN.
set(BOARD_HXTAL_HZ 20000000)
set(BOARD_HXTAL_BYPASS ON)

# Application features. Disabled features keep their public API through
# lightweight stub implementations.
set(APP_ENABLE_SHELL ON CACHE BOOL "Enable the application Shell console")

project(
    ${FIRMWARE_NAME}
    VERSION ${FIRMWARE_VERSION}
    LANGUAGES C ASM
)

aclass_initialize()

message(STATUS "Application features: shell=${APP_ENABLE_SHELL}")

add_subdirectory(platform)
add_subdirectory(device)
add_subdirectory(func)
add_subdirectory(app)

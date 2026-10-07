cmake_minimum_required(VERSION 3.23)

include(cmake/Aclass.cmake)

aclass_select(
    NAME gd32e505vet7_debug
    VERSION 0.1.0
    PLATFORM Embedded
    OS FreeRTOS
    MCU gd32e505
    LINKER_SCRIPT GD32E505_flash.ld
    TOOLCHAIN GCC
)

# Optional configuration profile for reproducible product/matrix builds.
if(NOT DEFINED ACLASS_CONFIG_FILE)
    set(ACLASS_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/aclass_config.cmake")
endif()

project(
    ${FIRMWARE_NAME}
    VERSION ${FIRMWARE_VERSION}
    LANGUAGES C ASM
)

aclass_initialize()

aclass_add_libraries(CONFIG_FILE "${ACLASS_CONFIG_FILE}")
add_subdirectory(app)

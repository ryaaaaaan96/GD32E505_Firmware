cmake_minimum_required(VERSION 3.23)

include(cmake/Aclass.cmake)

aclass_select(
    NAME gd32e505vet7_debug
    VERSION 0.1.0
    MCU gd32e505
    LINKER_SCRIPT GD32E505_flash.ld
    TOOLCHAIN GCC
)

include(config/aclass_config.cmake)
include(config/aDrv_config.cmake)

if(ASHELL_ENABLED AND
   (NOT ADRV_MODULE_USART OR NOT ADRV_USART_INTERRUPT OR NOT ADRV_USART_ASYNC))
    message(FATAL_ERROR
        "ASHELL_ENABLED requires USART, USART interrupts, and asynchronous DMA RX"
    )
endif()

if(ADATABASE_ENABLED AND
   (NOT ADRV_MODULE_QSPI OR NOT ADRV_MODULE_GPIO))
    message(FATAL_ERROR
        "ADATABASE_ENABLED requires ADRV_MODULE_QSPI and ADRV_MODULE_GPIO"
    )
endif()

# Board clock: a 20 MHz external active oscillator drives HXTAL_IN.
set(BOARD_HXTAL_HZ 20000000)
set(BOARD_HXTAL_BYPASS ON)

project(
    ${FIRMWARE_NAME}
    VERSION ${FIRMWARE_VERSION}
    LANGUAGES C ASM
)

aclass_initialize()

message(STATUS "aClass modules: aShell=${ASHELL_ENABLED}")

add_subdirectory(platform)
add_subdirectory(device)
add_subdirectory(func)
add_subdirectory(app)

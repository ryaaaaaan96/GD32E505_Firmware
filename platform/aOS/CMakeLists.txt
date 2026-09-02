# aOS owns the FreeRTOS configuration schema and reusable defaults.  A firmware
# project may override those defaults without modifying aOS itself.
include(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/freeRTOS_defaults.cmake"
)

include(
    "${CMAKE_SOURCE_DIR}/config/freeRTOS_config.cmake"
    OPTIONAL
)

if(NOT FREERTOS_USE_TASK_NOTIFICATIONS)
    message(FATAL_ERROR
        "aOS wait objects require FreeRTOS task notifications"
    )
endif()

if(FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES LESS 2)
    message(FATAL_ERROR
        "aOS reserves task notification index 1; "
        "FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES must be at least 2"
    )
endif()

set(FREERTOS_PORT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/portable/${FREERTOS_PORT}"
)

set(FREERTOS_CONFIG_DIR
    "${CMAKE_BINARY_DIR}/aOS_config"
)

file(MAKE_DIRECTORY
    "${FREERTOS_CONFIG_DIR}"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/FreeRTOSConfig.h.in"
    "${FREERTOS_CONFIG_DIR}/FreeRTOSConfig.h"
    @ONLY
)

set(FREERTOS_SOURCES
    tasks.c
    queue.c
    list.c
    timers.c
    event_groups.c
    stream_buffer.c
    croutine.c
)

set(FREERTOS_PORT_SOURCES
    "portable/MemMang/heap_${FREERTOS_HEAP_IMPLEMENTATION}.c"
    "${FREERTOS_PORT_DIR}/port.c"
    "${FREERTOS_PORT_DIR}/portasm.c"
)

add_library(
    aOS
    STATIC
        aOS.c
        ${FREERTOS_SOURCES}
        ${FREERTOS_PORT_SOURCES}
)

target_include_directories(
    aOS
    PUBLIC
        "${FREERTOS_CONFIG_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${FREERTOS_PORT_DIR}"
)

target_link_libraries(
    aOS
    PUBLIC
        aLib
        aCore
)

# FreeRTOS 是上游代码，不使用本工程对自有代码开启的严格告警升级。
set_source_files_properties(
    ${FREERTOS_SOURCES}
    ${FREERTOS_PORT_SOURCES}
    PROPERTIES
        COMPILE_OPTIONS "-Wno-error;-Wno-pedantic"
)

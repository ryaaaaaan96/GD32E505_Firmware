# aOS owns the FreeRTOS configuration schema and reusable defaults.  A firmware
# project may override those defaults without modifying aOS itself.
if(NOT ACLASS_OS STREQUAL "FreeRTOS")
    message(FATAL_ERROR "aOS backend not implemented: ${ACLASS_OS}")
endif()
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

if(FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES LESS 3)
    message(FATAL_ERROR
        "aOS reserves notification indexes 1 (wait) and 2 (work queue); "
        "FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES must be at least 3"
    )
endif()

if(NOT FREERTOS_USE_MUTEXES)
    message(FATAL_ERROR
        "aOS mutex objects require FreeRTOS mutex support"
    )
endif()

if(NOT FREERTOS_USE_RECURSIVE_MUTEXES)
    message(FATAL_ERROR
        "aOS recursive mutex services require FreeRTOS recursive mutex support"
    )
endif()

set(FREERTOS_PORT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/portable/${FREERTOS_PORT}"
)

if(NOT EXISTS "${FREERTOS_PORT_DIR}/port.c")
    message(FATAL_ERROR "Select an implemented FREERTOS_PORT in config/freeRTOS_config.cmake")
endif()
foreach(option AOS_WORKER_STACK_WORDS AOS_WORKER_PRIORITY)
    if(NOT "${${option}}" MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${option} must be a positive integer")
    endif()
endforeach()
if(AOS_WORKER_STACK_WORDS GREATER 65535 OR
   AOS_WORKER_PRIORITY GREATER_EQUAL FREERTOS_MAX_PRIORITIES)
    message(FATAL_ERROR "Invalid aOS worker stack size or priority")
endif()

set(FREERTOS_CONFIG_DIR
    "${CMAKE_BINARY_DIR}/aOS_config"
)

file(MAKE_DIRECTORY
    "${FREERTOS_CONFIG_DIR}"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/aOS_config.h.in"
    "${FREERTOS_CONFIG_DIR}/aOS_config.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/FreeRTOSConfig.h.in"
    "${FREERTOS_CONFIG_DIR}/FreeRTOSConfig.h"
    @ONLY
)

# 上游 FreeRTOS Kernel source set；与本项目的 aOS.c backend 明确分组。
set(FREERTOS_SOURCES
    tasks.c
    queue.c
    list.c
    timers.c
    event_groups.c
    stream_buffer.c
    croutine.c
)

# 上游 FreeRTOS heap 与 Cortex-M33 port source set。
set(FREERTOS_PORT_SOURCES
    "portable/MemMang/heap_${FREERTOS_HEAP_IMPLEMENTATION}.c"
    "${FREERTOS_PORT_DIR}/port.c"
    "${FREERTOS_PORT_DIR}/portasm.c"
)

add_library(
    aOS_freertos_kernel
    OBJECT
        ${FREERTOS_SOURCES}
        ${FREERTOS_PORT_SOURCES}
)
target_include_directories(aOS_freertos_kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${FREERTOS_CONFIG_DIR}"
    "${FREERTOS_PORT_DIR}"
)
target_link_libraries(aOS_freertos_kernel PRIVATE aCore aclass_build_options)

add_library(aOS STATIC
    backend/freertos/aOS_freertos.c
    $<TARGET_OBJECTS:aOS_freertos_kernel>
)

target_include_directories(
    aOS
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${FREERTOS_CONFIG_DIR}"
        "${FREERTOS_PORT_DIR}"
)

target_link_libraries(
    aOS
    PUBLIC
        aLib
    PRIVATE
        aCore
)

# Upstream kernel consumes build options only, never project warning policy.
target_link_libraries(aOS PRIVATE aclass_project_options)

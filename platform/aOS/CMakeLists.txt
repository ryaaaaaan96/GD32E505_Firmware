# aOS owns the FreeRTOS configuration schema and reusable defaults.  A firmware
# project may override those defaults without modifying aOS itself.
if(NOT ACLASS_OS STREQUAL "FreeRTOS")
    message(FATAL_ERROR "aOS backend not implemented: ${ACLASS_OS}")
endif()
include(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/freeRTOS_defaults.cmake"
)

if(ACLASS_FREERTOS_CONFIG_FILE)
    include("${ACLASS_FREERTOS_CONFIG_FILE}")
endif()

# The public millisecond clock currently requires this exact tick model.
if(FREERTOS_USE_16_BIT_TICKS OR NOT FREERTOS_TICK_RATE_HZ EQUAL 1000)
    message(FATAL_ERROR "aOS currently requires 32-bit ticks at 1000 Hz")
endif()

if(NOT FREERTOS_USE_TASK_NOTIFICATIONS)
    message(FATAL_ERROR
        "aOS wait objects require FreeRTOS task notifications"
    )
endif()

# 后端使用的基本内核服务，关闭时应在配置阶段报错，而不是链接失败。
foreach(required_option
    FREERTOS_SUPPORT_DYNAMIC_ALLOCATION
    FREERTOS_USE_TIMERS
    FREERTOS_INCLUDE_xTimerPendFunctionCall
    FREERTOS_INCLUDE_xTaskGetSchedulerState
    FREERTOS_INCLUDE_xTaskGetCurrentTaskHandle
    FREERTOS_INCLUDE_vTaskDelay
    FREERTOS_INCLUDE_vTaskDelete
    FREERTOS_INCLUDE_vTaskSuspend
)
    if(NOT ${required_option})
        message(FATAL_ERROR "aOS requires ${required_option}=1")
    endif()
endforeach()

if(FREERTOS_NUM_THREAD_LOCAL_STORAGE_POINTERS LESS 2)
    message(FATAL_ERROR "aOS errno and task startup require two FreeRTOS TLS slots")
endif()

if(NOT "${FREERTOS_TICK_RATE_HZ}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "FREERTOS_TICK_RATE_HZ must be a positive integer")
endif()

if(FREERTOS_MINIMAL_STACK_SIZE LESS 1 OR FREERTOS_MINIMAL_STACK_SIZE GREATER 65535)
    message(FATAL_ERROR "FreeRTOS default stack depth must be 1..65535 words")
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

set(FREERTOS_KERNEL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/freertos/kernel"
)

set(FREERTOS_PORT_DIR
    "${FREERTOS_KERNEL_DIR}/portable/${FREERTOS_PORT}"
)

if(NOT EXISTS "${FREERTOS_PORT_DIR}/port.c")
    message(FATAL_ERROR "Select an implemented FREERTOS_PORT in config/freeRTOS_config.cmake")
endif()
foreach(option AOS_WORKER_STACK_BYTES AOS_WORKER_PRIORITY)
    if(NOT "${${option}}" MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${option} must be a positive integer")
    endif()
endforeach()
if(AOS_WORKER_STACK_BYTES GREATER 262140 OR
   AOS_WORKER_PRIORITY GREATER_EQUAL FREERTOS_MAX_PRIORITIES OR
   AOS_WORKER_PRIORITY GREATER 7)
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

# 上游 FreeRTOS Kernel source set；与本项目的适配代码明确分组。
set(FREERTOS_SOURCES
    "${FREERTOS_KERNEL_DIR}/tasks.c"
    "${FREERTOS_KERNEL_DIR}/queue.c"
    "${FREERTOS_KERNEL_DIR}/list.c"
    "${FREERTOS_KERNEL_DIR}/timers.c"
    "${FREERTOS_KERNEL_DIR}/event_groups.c"
    "${FREERTOS_KERNEL_DIR}/stream_buffer.c"
    "${FREERTOS_KERNEL_DIR}/croutine.c"
)

# 上游 FreeRTOS heap 与 Cortex-M33 port source set。
set(FREERTOS_PORT_SOURCES
    "${FREERTOS_KERNEL_DIR}/portable/MemMang/heap_${FREERTOS_HEAP_IMPLEMENTATION}.c"
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
    "${FREERTOS_KERNEL_DIR}/include"
    "${FREERTOS_CONFIG_DIR}"
    "${FREERTOS_PORT_DIR}"
)
target_link_libraries(aOS_freertos_kernel PRIVATE aCore aclass_build_options)

add_library(aOS STATIC
    backend/freertos/aOS_freertos.c
    $<TARGET_OBJECTS:aOS_freertos_kernel>
)

target_compile_definitions(aOS PUBLIC
    AOS_WORKQUEUE_ENABLE=$<BOOL:${AOS_WORKQUEUE_ENABLE}>
)

target_include_directories(
    aOS
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE
        "${FREERTOS_KERNEL_DIR}/include"
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

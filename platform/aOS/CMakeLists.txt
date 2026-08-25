set(FREERTOS_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/portable/${FREERTOS_PORT}")
include("${CMAKE_SOURCE_DIR}/config/freeRTOS_config.cmake")
set(FREERTOS_CONFIG_DIR "${CMAKE_BINARY_DIR}/aOS_config")
file(MAKE_DIRECTORY "${FREERTOS_CONFIG_DIR}")
configure_file(
    "${CMAKE_SOURCE_DIR}/config/FreeRTOSConfig.h.in"
    "${FREERTOS_CONFIG_DIR}/FreeRTOSConfig.h"
    @ONLY
)
add_library(aOS STATIC
    aOS.c tasks.c queue.c list.c timers.c event_groups.c stream_buffer.c croutine.c
    "portable/MemMang/heap_${FREERTOS_HEAP_IMPLEMENTATION}.c"
    ${FREERTOS_PORT_DIR}/port.c ${FREERTOS_PORT_DIR}/portasm.c)
target_include_directories(aOS PUBLIC "${FREERTOS_CONFIG_DIR}" include ${FREERTOS_PORT_DIR})
target_link_libraries(aOS PUBLIC aLib aCore)
set_source_files_properties(
    tasks.c queue.c list.c timers.c event_groups.c stream_buffer.c croutine.c
    "portable/MemMang/heap_${FREERTOS_HEAP_IMPLEMENTATION}.c"
    ${FREERTOS_PORT_DIR}/port.c ${FREERTOS_PORT_DIR}/portasm.c
    PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-pedantic")

# AClass MCU 工程公共 CMake 规范与工程选择入口。

include_guard(GLOBAL)
set(ACLASS_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# 使用 macro 是为了让 MCU profile 中的变量保留在顶层目录作用域，并确保
# toolchain、CPU/FPU 参数在 project() 启用编译器之前生效。
macro(aclass_select)
    cmake_parse_arguments(ACLASS ""
        "NAME;VERSION;PLATFORM;OS;MCU;LINKER_SCRIPT;TOOLCHAIN;PRODUCT_DIR" "" ${ARGN})

    if(ACLASS_UNPARSED_ARGUMENTS OR ACLASS_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid aclass_select arguments: ${ACLASS_UNPARSED_ARGUMENTS}; ${ACLASS_KEYWORDS_MISSING_VALUES}")
    endif()

    if(NOT ACLASS_PRODUCT_DIR)
        set(ACLASS_PRODUCT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    get_filename_component(ACLASS_PRODUCT_DIR "${ACLASS_PRODUCT_DIR}" ABSOLUTE)

    foreach(required NAME MCU LINKER_SCRIPT)
        if(NOT ACLASS_${required})
            message(FATAL_ERROR "aclass_select requires ${required}")
        endif()
    endforeach()
    if(NOT ACLASS_VERSION)
        set(ACLASS_VERSION 0.1.0)
    endif()
    if(NOT ACLASS_TOOLCHAIN)
        set(ACLASS_TOOLCHAIN GCC)
    endif()
    if(NOT ACLASS_PLATFORM)
        set(ACLASS_PLATFORM Embedded)
    endif()
    if(NOT ACLASS_OS)
        set(ACLASS_OS FreeRTOS)
    endif()
    # Explicit backend selection; do not advertise unimplemented ports.
    if(NOT ACLASS_PLATFORM STREQUAL "Embedded" OR NOT ACLASS_OS STREQUAL "FreeRTOS")
        message(FATAL_ERROR "Implemented backend: PLATFORM Embedded / OS FreeRTOS; requested ${ACLASS_PLATFORM}/${ACLASS_OS}")
    endif()

    set(FIRMWARE_NAME "${ACLASS_NAME}")
    set(FIRMWARE_VERSION "${ACLASS_VERSION}")
    set(MCU_NAME "${ACLASS_MCU}")
    set(MCU_CONFIG
        "${ACLASS_PRODUCT_DIR}/config/mcu_${MCU_NAME}.cmake")

    if(IS_ABSOLUTE "${ACLASS_LINKER_SCRIPT}")
        set(_ACLASS_LINKER_SCRIPT "${ACLASS_LINKER_SCRIPT}")
    else()
        set(_ACLASS_LINKER_SCRIPT
            "${ACLASS_PRODUCT_DIR}/${ACLASS_LINKER_SCRIPT}")
    endif()
    set(LINKER_SCRIPT "${_ACLASS_LINKER_SCRIPT}")

    string(TOUPPER "${ACLASS_TOOLCHAIN}" ACLASS_TOOLCHAIN_NAME)
    set(TOOLCHAIN_NAME "${ACLASS_TOOLCHAIN_NAME}")
    set(_ACLASS_SELECTION "${MCU_NAME}|${TOOLCHAIN_NAME}")
    if(DEFINED ACLASS_BUILD_SELECTION AND
       NOT ACLASS_BUILD_SELECTION STREQUAL _ACLASS_SELECTION)
        message(FATAL_ERROR
            "MCU/toolchain changed in an existing build directory. "
            "Use a new build directory to avoid stale compiler flags.")
    endif()
    set(ACLASS_BUILD_SELECTION "${_ACLASS_SELECTION}"
        CACHE INTERNAL "Configured MCU/toolchain identity")

    set(CMAKE_TOOLCHAIN_FILE
        "${ACLASS_ROOT}/cmake/toolchains/${TOOLCHAIN_NAME}.cmake")

    if(NOT EXISTS "${MCU_CONFIG}")
        message(FATAL_ERROR "MCU configuration does not exist: ${MCU_CONFIG}")
    endif()
    if(NOT EXISTS "${LINKER_SCRIPT}")
        message(FATAL_ERROR "Linker script does not exist: ${LINKER_SCRIPT}")
    endif()
    if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        message(FATAL_ERROR "Toolchain file does not exist: ${CMAKE_TOOLCHAIN_FILE}")
    endif()

    if(NOT DEFINED ACLASS_FREERTOS_CONFIG_FILE)
        set(ACLASS_FREERTOS_CONFIG_FILE "${ACLASS_PRODUCT_DIR}/config/freeRTOS_config.cmake")
    endif()
    include("${MCU_CONFIG}")

    foreach(required
            MCU_DEVICE
            MCU_CPU MCU_CORE_CLOCK_HZ)
        if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
            message(FATAL_ERROR
                "MCU profile ${MCU_CONFIG} must define ${required}")
        endif()
    endforeach()

    message(STATUS "Firmware: ${FIRMWARE_NAME} ${FIRMWARE_VERSION}")
    message(STATUS "MCU profile: ${MCU_NAME} (${MCU_DEVICE})")
    message(STATUS "Toolchain: ${TOOLCHAIN_NAME}")
    message(STATUS "Linker script: ${LINKER_SCRIPT}")
endmacro()

macro(aclass_initialize)
    # 编译数据库与语言标准。
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    # 统一输出目录。
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

    # 工程通用编译选项。
    add_library(aclass_build_options INTERFACE)
    target_compile_options(aclass_build_options INTERFACE
        -ffunction-sections -fdata-sections
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Release>:-Os>
    )
    add_library(aclass_project_options INTERFACE)
    target_link_libraries(aclass_project_options INTERFACE aclass_build_options)
    target_compile_options(aclass_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Werror
    )
endmacro()

# 为最终固件目标设置链接脚本，并生成 ELF、HEX、BIN 与 MAP。
function(generate_firmware_images target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Unknown firmware target: ${target}")
    endif()
    if(NOT LINKER_SCRIPT OR NOT EXISTS "${LINKER_SCRIPT}")
        message(FATAL_ERROR "Invalid LINKER_SCRIPT: ${LINKER_SCRIPT}")
    endif()

    target_link_options(${target} PRIVATE
        "-T${LINKER_SCRIPT}"
        "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.map"
        "-Wl,--print-memory-usage"
    )
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${LINKER_SCRIPT}"
    )

    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/firmware-$<CONFIG>.json"
        CONTENT "{\n  \"elf\": \"$<TARGET_FILE:${target}>\",\n  \"device\": \"${MCU_DEBUG_DEVICE}\",\n  \"platform\": \"${ACLASS_PLATFORM}\",\n  \"os\": \"${ACLASS_OS}\"\n}\n"
    )

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        COMMENT "Generating HEX/BIN and size report for ${target}"
        VERBATIM
    )
endfunction()

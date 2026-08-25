# AClass MCU 工程公共 CMake 规范与工程选择入口。

include_guard(GLOBAL)

# Override a cache-backed default from a project config file.  Defaults must
# be declared before para_set() is used.
macro(para_set variable value)
    if(NOT DEFINED CACHE{${variable}})
        message(FATAL_ERROR
            "para_set cannot override undefined parameter: ${variable}")
    endif()
    set_property(CACHE ${variable} PROPERTY VALUE ${value})
endmacro()

# 使用 macro 是为了让 MCU profile 中的变量保留在顶层目录作用域，并确保
# toolchain、CPU/FPU 参数在 project() 启用编译器之前生效。
macro(aclass_select)
    cmake_parse_arguments(ACLASS ""
        "NAME;VERSION;MCU;LINKER_SCRIPT;TOOLCHAIN" "" ${ARGN})

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

    set(FIRMWARE_NAME "${ACLASS_NAME}" CACHE STRING "Firmware target name" FORCE)
    set(FIRMWARE_VERSION "${ACLASS_VERSION}")
    set(MCU_NAME "${ACLASS_MCU}" CACHE STRING "MCU configuration name" FORCE)
    set(MCU_CONFIG
        "${CMAKE_SOURCE_DIR}/config/mcu_${MCU_NAME}.cmake")

    if(IS_ABSOLUTE "${ACLASS_LINKER_SCRIPT}")
        set(_ACLASS_LINKER_SCRIPT "${ACLASS_LINKER_SCRIPT}")
    else()
        set(_ACLASS_LINKER_SCRIPT
            "${CMAKE_SOURCE_DIR}/${ACLASS_LINKER_SCRIPT}")
    endif()
    set(LINKER_SCRIPT "${_ACLASS_LINKER_SCRIPT}"
        CACHE FILEPATH "Firmware linker script" FORCE)

    string(TOUPPER "${ACLASS_TOOLCHAIN}" ACLASS_TOOLCHAIN_NAME)
    set(TOOLCHAIN_NAME "${ACLASS_TOOLCHAIN_NAME}"
        CACHE STRING "AClass toolchain name" FORCE)
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_SOURCE_DIR}/cmake/toolchains/${TOOLCHAIN_NAME}.cmake"
        CACHE FILEPATH "Toolchain file" FORCE)

    if(NOT EXISTS "${MCU_CONFIG}")
        message(FATAL_ERROR "MCU configuration does not exist: ${MCU_CONFIG}")
    endif()
    if(NOT EXISTS "${LINKER_SCRIPT}")
        message(FATAL_ERROR "Linker script does not exist: ${LINKER_SCRIPT}")
    endif()
    if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        message(FATAL_ERROR "Toolchain file does not exist: ${CMAKE_TOOLCHAIN_FILE}")
    endif()

    include("${MCU_CONFIG}")

    foreach(required
            MCU_DEVICE MCU_PORT
            MCU_CPU MCU_CORE_CLOCK_HZ FREERTOS_PORT)
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
    add_compile_options(
        -Wall -Wextra -Wpedantic -Werror
        -ffunction-sections -fdata-sections
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Release>:-Os>
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

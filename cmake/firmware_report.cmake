# 读取真实构建配置与产物；仅生成构建报告，不改写固件。
cmake_minimum_required(VERSION 3.23)
include("${REPORT_INPUT}")

execute_process(
    COMMAND "${CMAKE_SIZE}" --format=berkeley --radix=10 "${REPORT_ELF}"
    RESULT_VARIABLE result OUTPUT_VARIABLE sizes ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect firmware size: ${error}")
endif()
if(NOT sizes MATCHES
   "\n[ \t]*([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+")
    message(FATAL_ERROR "Unexpected size output: ${sizes}")
endif()
set(text_bytes "${CMAKE_MATCH_1}")
set(data_bytes "${CMAKE_MATCH_2}")
set(bss_bytes "${CMAKE_MATCH_3}")

# GNU size 的 data 分类可包含 Flash 初始化数组，不能等同于 RAM .data。
# 从 MAP 读取 MEMORY 区域，再按 ELF 的 VMA/LMA 计算占用末端，包含地址空隙。
# VMA 计算运行期预留；只有 LOAD 节才同时计算 LMA 中的烧录镜像。
file(STRINGS "${REPORT_BASE}.map" memory_lines
    REGEX "^[A-Za-z_][A-Za-z0-9_]*[ \t]+0x[0-9a-fA-F]+[ \t]+0x")
set(regions "")
foreach(line IN LISTS memory_lines)
    if(line MATCHES "^([^ \t]+)[ \t]+(0x[0-9a-fA-F]+)[ \t]+(0x[0-9a-fA-F]+)")
        set(region "${CMAKE_MATCH_1}")
        list(APPEND regions "${region}")
        math(EXPR origin_${region} "${CMAKE_MATCH_2}")
        math(EXPR capacity_${region} "${CMAKE_MATCH_3}")
        math(EXPR end_${region} "${origin_${region}} + ${capacity_${region}}")
        set(used_${region} 0)
    endif()
endforeach()
if(NOT regions)
    message(FATAL_ERROR "No MEMORY regions found in ${REPORT_BASE}.map")
endif()
execute_process(COMMAND "${CMAKE_OBJDUMP}" -h "${REPORT_ELF}"
    RESULT_VARIABLE result OUTPUT_VARIABLE sections ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect firmware sections: ${error}")
endif()
string(REPLACE "\n" ";" section_lines "${sections}")
string(CONCAT section_pattern
    "^[ \t]*[0-9]+[ \t]+[^ \t]+[ \t]+([0-9a-fA-F]+)[ \t]+"
    "([0-9a-fA-F]+)[ \t]+([0-9a-fA-F]+)[ \t]+")
foreach(line IN LISTS section_lines)
    if(line MATCHES "${section_pattern}")
        math(EXPR section_size "0x${CMAKE_MATCH_1}")
        math(EXPR vma "0x${CMAKE_MATCH_2}")
        math(EXPR lma "0x${CMAKE_MATCH_3}")
    elseif(line MATCHES "ALLOC" AND section_size GREATER 0)
        set(addresses "${vma}")
        if(line MATCHES "LOAD")
            list(APPEND addresses "${lma}")
        endif()
        foreach(region IN LISTS regions)
            foreach(address IN LISTS addresses)
                if(address GREATER_EQUAL origin_${region} AND
                   address LESS end_${region})
                    math(EXPR used
                        "${address} + ${section_size} - ${origin_${region}}")
                    if(used GREATER used_${region})
                        set(used_${region} "${used}")
                    endif()
                endif()
            endforeach()
        endforeach()
    endif()
endforeach()
set(memory_report "")
foreach(region IN LISTS regions)
    math(EXPR free "${capacity_${region}} - ${used_${region}}")
    if(capacity_${region} GREATER 0)
        math(EXPR ratio
            "(${used_${region}} * 10000 + ${capacity_${region}} / 2) / ${capacity_${region}}")
        math(EXPR whole "${ratio} / 100")
        math(EXPR fraction "${ratio} % 100")
        if(fraction LESS 10)
            set(fraction "0${fraction}")
        endif()
        set(percent "${whole}.${fraction}%")
    else()
        set(percent "n/a")
    endif()
    string(APPEND memory_report
        "${region}: ${used_${region}} / ${capacity_${region}} bytes "
        "(${percent}), free=${free} bytes\n")
endforeach()

# 源码包不要求安装 Git；有仓库时记录提交和工作区状态，便于追溯。
set(revision "unavailable (source archive or Git not installed)")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}"
            rev-parse --short=12 HEAD
        RESULT_VARIABLE result OUTPUT_VARIABLE commit
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    if(result EQUAL 0)
        set(revision "${commit}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}"
                status --porcelain
            RESULT_VARIABLE result OUTPUT_VARIABLE changes ERROR_QUIET
        )
        if(NOT result EQUAL 0)
            string(APPEND revision " (worktree status unavailable)")
        elseif(NOT changes STREQUAL "")
            string(APPEND revision " (dirty)")
        endif()
    endif()
endif()

foreach(list_name REPORT_ENABLED REPORT_DISABLED)
    if(${list_name})
        list(JOIN ${list_name} ", " ${list_name})
    else()
        set(${list_name} "none")
    endif()
endforeach()

set(report "\n================ Firmware build summary ================\n")
string(APPEND report
    "Firmware       : ${FIRMWARE_NAME} v${FIRMWARE_VERSION}\n"
    "Source state   : ${revision}\n"
    "Configuration  : ${REPORT_CONFIG}\n"
    "Demo tasks     : ${REPORT_DEMO}\n"
    "Platform / OS  : ${ACLASS_PLATFORM} / ${ACLASS_OS}\n"
    "MCU / clock    : ${MCU_DEVICE} / ${MCU_CORE_CLOCK_HZ} Hz\n"
    "CPU / FPU / ABI: ${MCU_CPU} / ${MCU_FPU} / ${MCU_FLOAT_ABI}\n"
    "Compiler       : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\n"
    "Compiler path  : ${CMAKE_C_COMPILER}\n"
    "Linker script  : ${LINKER_SCRIPT}\n"
    "Libraries ON   : ${REPORT_ENABLED}\n"
    "Libraries OFF  : ${REPORT_DISABLED}\n"
    "OS tick        : ${REPORT_TICK} Hz\n"
    "OS heap reserve: ${REPORT_HEAP} bytes (included in RAM below)\n"
    "\n"
    "Sections       : text=${text_bytes}, data=${data_bytes}, "
        "bss=${bss_bytes} bytes\n"
    "${memory_report}"
    "RAM includes reserved pools; runtime heap/stack peaks need board tests.\n"
    "\n")

foreach(kind ELF HEX BIN MAP)
    if(kind STREQUAL "ELF")
        set(path "${REPORT_ELF}")
    else()
        string(TOLOWER "${kind}" extension)
        set(path "${REPORT_BASE}.${extension}")
    endif()
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Firmware artifact missing: ${path}")
    endif()
    file(SIZE "${path}" bytes)
    string(APPEND report "${kind}: ${path} (${bytes} bytes)\n")
endforeach()
file(SHA256 "${REPORT_BASE}.bin" digest)
string(APPEND report
    "BIN SHA256: ${digest}\n"
    "Report: ${REPORT_BASE}.build.txt\n"
    "========================================================\n")
file(WRITE "${REPORT_BASE}.build.txt" "${report}")
message("${report}")

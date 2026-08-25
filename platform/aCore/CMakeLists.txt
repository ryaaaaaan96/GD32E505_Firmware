cmake_minimum_required(VERSION 3.23)

string(TOLOWER ${MCU_DEVICE}   MCU_DEVICE_LOWER)
string(TOLOWER ${MCU_FAMILY}   MCU_FAMILY_LOWER)
string(TOLOWER ${TARGET_CORE}  TARGET_CORE_LOWER)

list(LENGTH CMSIS_CORES CMSIS_CORE_COUNT)

if(CMSIS_CORE_COUNT GREATER 1)
    set(CMSIS_STARTUP startup_${MCU_DEVICE_LOWER}_${TARGET_CORE}.s)
else()
    set(CMSIS_STARTUP startup_${MCU_DEVICE_LOWER}.s)
endif()

if(CMSIS_CORE_COUNT GREATER 1)
    set(CMSIS_SYSTEM system_${MCU_FAMILY_LOWER}xx_dualcore_boot_cm4_cm7.c)
else()
    set(CMSIS_SYSTEM system_${MCU_FAMILY_LOWER}xx.c)
endif()

add_library(aCore STATIC
    Device/${MCU_DEVICE_DIR}/Source/gcc/${CMSIS_STARTUP}
    Device/${MCU_DEVICE_DIR}/Source/${CMSIS_SYSTEM}
)

target_sources(aCore PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/Device/${MCU_DEVICE_DIR}/Include
    FILES
        Core/Include/core_${TARGET_CORE_LOWER}.h
        Device/${MCU_DEVICE_DIR}/Include/${MCU_DEVICE_LOWER}.h
        Device/${MCU_DEVICE_DIR}/Include/${MCU_FAMILY_LOWER}xx.h
        Device/${MCU_DEVICE_DIR}/Include/system_${MCU_FAMILY_LOWER}xx.h
)


target_compile_definitions(aCore PUBLIC
    CORE_${TARGET_CORE}
    USER_VECT_TAB_ADDRESS
)

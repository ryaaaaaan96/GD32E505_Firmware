# 只读点表契约始终可用，运行库由 ABUS_ENABLE 独立裁剪。
add_subdirectory(aBus)

if(AMEMORY_ENABLE)
    add_subdirectory(aMemory)
endif()

if(ADATABASE_ENABLE)
    add_subdirectory(aDataBase)
endif()

if(AMODBUS_ENABLE)
    add_subdirectory(aModbus)
endif()

add_subdirectory(aShell)
add_subdirectory(aLog)

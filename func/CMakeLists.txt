if(ABUS_ENABLE)
    add_subdirectory(aBus)
endif()

if(ADATABASE_ENABLE)
    add_subdirectory(aDataBase)
endif()

if(AMODBUS_ENABLE)
    add_subdirectory(aModbus)
endif()

add_subdirectory(aShell)
add_subdirectory(aLog)

if(ADATABASE_ENABLE)
    add_subdirectory(aDataBase)
endif()

if(AMODBUS_ENABLE)
    add_subdirectory(aModbus)
endif()

add_subdirectory(aShell)

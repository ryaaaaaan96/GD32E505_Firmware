if(ADATABASE_ENABLED)
    add_subdirectory(aDataBase)
endif()

if(AMODBUS_ENABLED)
    add_subdirectory(aModbus)
endif()

add_subdirectory(aShell)

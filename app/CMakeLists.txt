set(APP_TARGET ${PROJECT_NAME})

add_executable(${APP_TARGET}
    main.c
)
target_link_libraries(${APP_TARGET} PRIVATE
    aCore
    aDrv
    aOS
    aDevUsart
    aDataBase
    aModbus
    aShell
)

set_target_properties(${APP_TARGET} PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    SUFFIX ".elf"
)

generate_firmware_images(${APP_TARGET})

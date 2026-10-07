set(APP_TARGET ${PROJECT_NAME})

add_executable(
    ${APP_TARGET}
        main.c
)

target_include_directories(${APP_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(ADEV_USART_ENABLE)
    target_link_libraries(${APP_TARGET} PRIVATE aDevUsart)
endif()

add_subdirectory(sig)
add_subdirectory(devices)
add_subdirectory(flash_test)
add_subdirectory(database)
add_subdirectory(log)
add_subdirectory(task)

target_link_libraries(
    ${APP_TARGET}
    PRIVATE
        aCore
        aDrv
        aOS
)

set_target_properties(
    ${APP_TARGET}
    PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
        SUFFIX ".elf"
)

generate_firmware_images(${APP_TARGET})
target_link_libraries(${APP_TARGET} PRIVATE aclass_project_options)

target_compile_definitions(${APP_TARGET} PRIVATE
    ABUS_ENABLE=$<BOOL:${ABUS_ENABLE}>
)

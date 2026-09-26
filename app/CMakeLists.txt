set(APP_TARGET ${PROJECT_NAME})

add_executable(
    ${APP_TARGET}
        main.c
)

target_include_directories(${APP_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(ADEV_USART_ENABLED)
    target_link_libraries(${APP_TARGET} PRIVATE aDevUsart)
    target_compile_definitions(${APP_TARGET} PRIVATE APP_HAS_USART=1)
else()
    target_compile_definitions(${APP_TARGET} PRIVATE APP_HAS_USART=0)
endif()

add_subdirectory(devices)
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

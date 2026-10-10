add_library(
    aDevLed
    STATIC
        aDev_led.c
)

target_include_directories(
    aDevLed
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    aDevLed
    PUBLIC
        aDrv
        aLib
)
target_link_libraries(aDevLed PRIVATE aclass_project_options)

foreach(feature STATIC DYNAMIC)
    target_compile_definitions(aDevLed PUBLIC
        ADEV_LED_${feature}_ENABLE=$<BOOL:${ADEV_LED_${feature}_ENABLE}>
    )
endforeach()

# 纯静态 LED 只依赖 GPIO；动态创建才使用 aOS 分配器。
if(ADEV_LED_DYNAMIC_ENABLE)
    target_link_libraries(aDevLed PRIVATE aOS)
endif()

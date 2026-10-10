add_library(
    aDevUsart
    STATIC
        src/aDev_usart.c
        src/aDev_usart_tx.c
        src/aDev_usart_rx.c
)

target_include_directories(
    aDevUsart
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    aDevUsart
    PUBLIC
        aDrv
        aLib
    PRIVATE
        aOS
)

foreach(feature INTERRUPT DIRECT ASYNC RS485 STATIC DYNAMIC)
    target_compile_definitions(
        aDevUsart
        PUBLIC
            ADEV_USART_${feature}_ENABLE=$<BOOL:${ADEV_USART_${feature}_ENABLE}>
    )
endforeach()

# 发送与接收实现按各自的能力宏进行编译裁剪。
if(ADEV_USART_RS485_ENABLE)
    target_sources(aDevUsart PRIVATE src/aDev_usart_rs485.c)
endif()
target_link_libraries(aDevUsart PRIVATE aclass_project_options)

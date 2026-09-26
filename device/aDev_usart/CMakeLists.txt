add_library(
    aDevUsart
    STATIC
        aDev_usart.c
        aDev_usart_rx.c
)

target_include_directories(
    aDevUsart
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    aDevUsart
    PUBLIC
        aDrv
        aLib
    PRIVATE
        aOS
)

foreach(feature INTERRUPT DMA ASYNC RS485)
    target_compile_definitions(
        aDevUsart
        PUBLIC
            ADEV_USART_HAS_${feature}=$<BOOL:${ADEV_USART_${feature}_ENABLED}>
    )
endforeach()

if(ADEV_USART_DMA_ENABLED)
    target_sources(aDevUsart PRIVATE aDev_usart_direct.c aDev_usart_dma_rx.c)
endif()
if(ADEV_USART_ASYNC_ENABLED)
    target_sources(aDevUsart PRIVATE aDev_usart_async_rx.c aDev_usart_tx_queue.c)
endif()
if(ADEV_USART_RS485_ENABLED)
    target_sources(aDevUsart PRIVATE aDev_usart_rs485.c)
endif()

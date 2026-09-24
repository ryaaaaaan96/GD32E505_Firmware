add_library(
    aDevUsart
    STATIC
        aDev_usart.c
        aDev_usart_rx.c
        aDev_usart_direct.c
        aDev_usart_rs485.c
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

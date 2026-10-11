add_subdirectory(aLib)
add_subdirectory(aCore)
add_subdirectory(aDrv)
add_subdirectory(aOS)

# 在平台边界连接 OS 优先级约束与 USART 驱动，设备层不参与校验。
target_compile_definitions(aDrv PRIVATE
    ADRV_USART_INTERRUPT_PRIORITY_MIN=$<TARGET_PROPERTY:aOS,AOS_ISR_PRIORITY_MIN>
    ADRV_USART_INTERRUPT_PRIORITY_MAX=$<TARGET_PROPERTY:aOS,AOS_ISR_PRIORITY_MAX>
)

# 产品配置：普通变量；依赖必须显式开启，解析器不覆盖用户选择。
# 缺少依赖时 cmake/aclass_resolve.cmake 给出配置错误。

# func
set(ASHELL_REQUESTED ON)
set(ADATABASE_REQUESTED OFF)
set(AMODBUS_REQUESTED OFF)

# Board
set(BOARD_HXTAL_HZ 20000000)
set(BOARD_HXTAL_BYPASS ON)

# device / LED
set(ADEV_LED_REQUESTED ON)

# device / USART：按功能组配置；TX/RX 模式与 IDLE 由 app 初始化选择。
set(ADEV_USART_REQUESTED ON)
set(ADEV_USART_INTERRUPT_REQUESTED ON)
# 同步用户缓冲区直传 API；轮询/DMA 后端在设备初始化时选择。
set(ADEV_USART_DIRECT_REQUESTED OFF)
# 业务异步请求、超时/取消和任务回调；当前依赖底层 DMA/IRQ，不依赖 Direct API。
set(ADEV_USART_ASYNC_REQUESTED OFF)
set(ADEV_USART_RS485_REQUESTED OFF)

# device / Flash25Q
set(ADEV_FLASH25Q_REQUESTED OFF)

# driver：显式启用依赖的底层能力。
set(ADRV_MODULE_GPIO_REQUESTED ON)
set(ADRV_MODULE_USART_REQUESTED ON)
set(ADRV_USART_INTERRUPT_REQUESTED ON)
# USART 专用 DMA 搬运能力，依赖通用 DMA 驱动；不是业务 Async 开关。
set(ADRV_USART_DMA_REQUESTED OFF)
set(ADRV_MODULE_DMA_REQUESTED OFF)
set(ADRV_MODULE_SPI_REQUESTED OFF)
set(ADRV_MODULE_QSPI_REQUESTED OFF)
# aOS shared deferred-work service (not an application business task).
set(AOS_WORKER_STACK_BYTES 2048)
set(AOS_WORKQUEUE_REQUESTED OFF)
set(AOS_WORKER_PRIORITY 4)
# FLASH25Q builds the optional adapter; CUSTOM uses application-supplied ops.
set(ADATABASE_BACKEND FLASH25Q)

# Product-owned FlashDB geometry; custom profiles may override this path.
set(ADATABASE_LAYOUT_FILE "${CMAKE_CURRENT_LIST_DIR}/aDatabase_flash_layout.h")

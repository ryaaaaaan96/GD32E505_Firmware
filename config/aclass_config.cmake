# 产品配置：普通变量；依赖必须显式开启，解析器不覆盖用户选择。
# 缺少依赖时 cmake/aclass_resolve.cmake 给出配置错误。

# func
set(ABUS_ENABLE ON)
set(ABUS_STATIC_ENABLE ON)
set(ABUS_DYNAMIC_ENABLE ON)
# Static definition validation; runtime value ranges are always checked.
set(ABUS_DEF_CHECK_ENABLE ON)
# NONE: no locks; BUS: one mutex; SIG: one mutex for every signal.
set(ABUS_LOCK_GRANULARITY BUS)
set(ASHELL_ENABLE ON)
# Shell command history entries (1..255).
set(ASHELL_HISTORY_COUNT 10)
# 日志核心不依赖 Shell；当前应用将日志接入 Shell 队列。
set(ALOG_ENABLE ON)
# 静态最高等级：1 ERROR、2 WARN、3 INFO、4 DEBUG、5 VERBOSE。
set(ALOG_OUTPUT_LEVEL 5)
# 单条完整日志容量，含头部、颜色和 CRLF；不创建独立异步队列。
set(ALOG_LINE_BUFFER_SIZE 256)
set(AMEMORY_ENABLE ON)
set(AMEMORY_MAX_PARTITIONS 16)
set(ADATABASE_ENABLE ON)
set(ADATABASE_STATIC_ENABLE ON)
set(ADATABASE_DYNAMIC_ENABLE ON)
set(AMODBUS_ENABLE ON)
# 协议核心依赖 aBus；USART 适配为可选目标，设备参数、映射和任务归应用。
set(AMODBUS_STATIC_ENABLE ON)
set(AMODBUS_DYNAMIC_ENABLE ON)
set(AMODBUS_CLIENT_ENABLE ON)
set(AMODBUS_SERVER_ENABLE ON)
# 应用 RTU 演示：USART2 PC10/PC11，PA15 高电平发送。
# 主从角色和站号在 app/app_config.h 配置。
set(APP_MODBUS_DEMO_ENABLE ON)

# Board
set(BOARD_HXTAL_HZ 20000000)
set(BOARD_HXTAL_BYPASS ON)

# device / LED
set(ADEV_LED_ENABLE ON)

# device / USART：按功能组配置；TX/RX 模式与 IDLE 由 app 初始化选择。
set(ADEV_USART_ENABLE ON)
# Device object allocation APIs; both may be enabled.
set(ADEV_USART_STATIC_ENABLE OFF)
set(ADEV_USART_DYNAMIC_ENABLE ON)
set(ADEV_USART_INTERRUPT_ENABLE ON)
# 同步用户缓冲区直传 API；轮询/DMA 后端在设备初始化时选择。
set(ADEV_USART_DIRECT_ENABLE OFF)
# 业务异步请求、超时/取消和任务回调；当前依赖底层 DMA/IRQ，不依赖 Direct API。
set(ADEV_USART_ASYNC_ENABLE OFF)
set(ADEV_USART_RS485_ENABLE ON)

# device / Flash25Q
set(ADEV_FLASH25Q_ENABLE ON)
set(ADEV_FLASH25Q_STATIC_ENABLE ON)
set(ADEV_FLASH25Q_DYNAMIC_ENABLE ON)

# driver：显式启用依赖的底层能力。
set(ADRV_MODULE_GPIO_ENABLE ON)
set(ADRV_MODULE_USART_ENABLE ON)
set(ADRV_USART_INTERRUPT_ENABLE ON)
# USART 专用 DMA 搬运能力，依赖通用 DMA 驱动；不是业务 Async 开关。
set(ADRV_USART_DMA_ENABLE ON)
set(ADRV_MODULE_DMA_ENABLE ON)
set(ADRV_MODULE_SPI_ENABLE ON)
set(ADRV_MODULE_QSPI_ENABLE OFF)
# aOS shared deferred-work service (not an application business task).
set(AOS_WORKER_STACK_BYTES 2048)
set(AOS_WORKQUEUE_ENABLE OFF)
set(AOS_WORKER_PRIORITY 4)

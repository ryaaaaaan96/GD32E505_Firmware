/**
 * @file app_config.h
 * @brief 应用层配置宏的统一入口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 集中定义应用任务、时序以及设备实例使用的配置宏。
 * 具名常量由使用处包含的 aOS/aDev/aDrv 头文件提供；本文件不引入平台私有头。
 * 模块编译裁剪仍由 config 目录的 CMake 配置负责，不在此重复定义。
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 一次性应用初始化任务，栈容量单位为字节。 */
#define APP_INIT_TASK_STACK_BYTES 4096U
#define APP_INIT_TASK_PRIORITY AOS_TASK_PRIO_HIGH

/* 系统状态指示灯及其任务。 */
#define APP_STATUS_LED_PIN ADRV_PIN(ADRV_GPIO_PORT_A, 8)
#define APP_STATUS_LED_ACTIVE_LEVEL ADEV_LED_ACTIVE_LOW
#define ASYSTEM_STATUS_BLINK_PERIOD_MS 500U
#define ASYSTEM_STATUS_TASK_STACK_BYTES 1024U
#define ASYSTEM_STATUS_TASK_PRIORITY AOS_TASK_PRIO_NORMAL

/* 系统控制台的收发缓冲区与数据路径。 */
#define APP_CONSOLE_RX_BUFFER_SIZE 256U
#define APP_CONSOLE_TX_BUFFER_SIZE 256U
#define APP_CONSOLE_USART_MODE                  \
    (ADEV_USART_TX_INTERRUPT_BUFFERED |         \
     ADEV_USART_RX_INTERRUPT_BUFFERED |         \
     ADEV_USART_OPTION_RX_IDLE)

/* Shell 任务及读写时序。 */
#define ASYSTEM_SHELL_IO_TIMEOUT A_TIMEOUT_MS(20U)
#define ASYSTEM_SHELL_TASK_STACK_BYTES 2048U
#define ASYSTEM_SHELL_TASK_PRIORITY AOS_TASK_PRIO_LOW
#define ASYSTEM_SHELL_RETRY_DELAY_MS 1U

#endif

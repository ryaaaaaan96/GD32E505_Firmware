/**
 * @file app_system_device.h
 * @brief 应用 system 设备实例的初始化及句柄获取。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 应用枚举是业务身份，不是 MCU 硬件编号；引脚/波特率/缓冲区配置在对应 .c 中。
 * 仅启动阶段单线程使用，在 aDrv/aOS 前置初始化之后调用，不是 ISR 安全 API。
 * 返回句柄为共享借用对象；业务可保存并跨文件传递，不能自行销毁或重新初始化。
 */

#ifndef APP_SYSTEM_DEVICE_H
#define APP_SYSTEM_DEVICE_H
#include "aDev_led.h"
#if ASHELL_ENABLED
#include "aDev_usart.h"
typedef enum {
    APP_USART_CONSOLE = 0,
} appUsartId_t;
#endif

/** @brief 应用 LED 实例标识，不是硬件引脚编号；枚举值保持稳定。 */
typedef enum {
    APP_LED_STATUS = 0,
} appLedId_t;

/**
 * @brief 按业务实例 ID 初始化状态灯并取得共享句柄。
 * @param[in] id 应用 LED 实例，当前为 APP_LED_STATUS。
 * @param[out] handle_out 必填，成功返回模块持有的静态句柄，失败置 NULL。
 * @retval A_STATUS_OK 实例可用；重复调用返回相同句柄。
 * @retval A_STATUS_INVALID_PARAM 输出指针为空。
 * @retval A_STATUS_NOT_FOUND ID 无效。
 * @retval A_STATUS_BUSY 检测到重入初始化，不代表支持并发初始化。
 * @return 首次设备初始化错误会被保存，后续调用返回相同错误，不自动重试。
 */
aStatus_t appSystemStatusLedInit(appLedId_t id, aDevLedHandle_t **handle_out);
#if ASHELL_ENABLED
/**
 * @brief 按业务实例 ID 初始化控制台 USART 并取得共享句柄。
 * @param[in] id 应用 USART 实例，当前为 APP_USART_CONSOLE。
 * @param[out] handle_out 必填输出，失败置 NULL；句柄由模块持有。
 * @return 状态和重复调用规则同 appSystemStatusLedInit()，也可能返回 USART 初始化错误。
 * @note 仅 ASHELL_ENABLED 时声明；可能创建内部 OS 对象，不是无副作用查找。
 */
aStatus_t appSystemConsoleInit(appUsartId_t id, aDevUsartHandle_t **handle_out);
#endif
#endif

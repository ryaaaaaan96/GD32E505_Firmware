/**
 * @file app_system_device.h
 * @brief 应用 system 设备实例的初始化及句柄获取。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 函数名标识业务设备；引脚/波特率/缓冲区配置在对应 .c 中。
 * 仅启动阶段单线程使用，在 aDrv/aOS 前置初始化之后调用，不是 ISR 安全 API。
 * 由应用保证每个实例只初始化一次；不缓存结果、不提供重复或重入初始化保护。
 * 返回句柄为共享借用对象；业务可保存并跨文件传递，不能自行销毁或重新初始化。
 */

#ifndef APP_SYSTEM_DEVICE_H
#define APP_SYSTEM_DEVICE_H
#include "aDev_led.h"

/**
 * @brief 初始化状态灯并取得共享句柄。
 * @param[out] handle_out 必填，成功返回模块持有的静态句柄，失败置 NULL。
 * @retval A_STATUS_OK 初始化成功，输出实例句柄。
 * @retval A_STATUS_INVALID_PARAM 输出指针为空。
 * @return 底层初始化错误直接返回，由上层处理，不在本层重试。
 */
aStatus_t appSystemStatusLedInit(aDevLedHandle_t **handle_out);
#if ASHELL_ENABLE
/**
 * @brief 初始化控制台 USART 并绑定 Stream，初始化 aShell 单例。
 * @return 返回 USART 或 Shell 初始化错误；Shell 失败时销毁动态 USART 对象。
 * @note 仅 ASHELL_ENABLE 时声明；可能创建内部 OS 对象，不是无副作用查找。
 */
aStatus_t appSystemConsoleInit(void);
#endif
#endif

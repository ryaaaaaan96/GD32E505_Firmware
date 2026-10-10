/**
 * @file system_device.h
 * @brief 应用 system 设备实例的初始化及句柄获取。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 函数名标识业务设备；引脚/波特率/缓冲区配置在对应 .c 中。
 * 仅启动阶段单线程使用，在 aDrv/aOS 前置初始化之后调用，不是 ISR 安全 API。
 * 由应用保证每个实例只初始化一次；不缓存结果，不支持并发初始化或运行期重启。
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
 * @return 已持有串口返回 BUSY；否则返回初始化错误或失败清理错误。
 * @note Shell 初始化失败时回收串口；回收失败保留句柄，须调用 DeInit 清理。
 * @note 仅 ASHELL_ENABLE 时声明；可能创建内部 OS 对象，不是无副作用查找。
 * 串口句柄不对外暴露，收发由唯一的 Shell 处理任务执行，不创建串口互斥锁。
 * 其他任务仅通过 ASHELL_PRINT/aShellWrite 入队，不能使用命令回复接口代替。
 */
aStatus_t appSystemConsoleInit(void);

/**
 * @brief 释放控制台 USART 与 Shell 队列，主要用于启动失败清理。
 * @return OK、NOT_READY（无串口实例）或底层清理错误。
 * @warning 任务上下文调用，必须先停止 Shell 处理及所有输出生产者。
 * 启动等待中的 Shell 任务尚未访问控制台，可在保持未就绪时清理。
 * 运行期不能仅调用此函数停止系统；应用仍须先协调任务及日志使用者。
 * 串口释放失败保留句柄与 Shell 状态，允许重试清理；成功后丢弃排队输出。
 */
aStatus_t appSystemConsoleDeInit(void);
#endif
#endif

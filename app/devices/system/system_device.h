/**
 * @file system_device.h
 * @brief 应用 system 设备实例的初始化及句柄获取。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 函数名标识业务设备；引脚/波特率/缓冲区配置在对应 .c 中。
 * 初始化仅在启动阶段单线程调用，要求 aDrv/aOS 就绪；所有接口均非 ISR 安全。
 * 由应用保证每个实例只初始化一次；不缓存结果，不支持并发初始化或运行期重启。
 * 返回句柄为共享借用对象；业务可保存并跨文件传递，不能自行销毁或重新初始化。
 */

#ifndef APP_SYSTEM_DEVICE_H
#define APP_SYSTEM_DEVICE_H
#include "aDev_led.h"
#if ASHELL_ENABLE
#include "aStream.h"
#endif

/**
 * @brief 初始化状态灯并取得共享句柄。
 * @param[out] handle_out 必填，成功返回模块持有的静态句柄，失败置 NULL。
 * @retval A_STATUS_OK 初始化成功，输出实例句柄。
 * @retval A_STATUS_INVALID_PARAM 输出指针为空。
 * @return 底层初始化错误直接返回，由上层处理，不在本层重试。
 */
aStatus_t appSystemStatusLedInit(aDevLedHandle_t **handle_out);
#if ASHELL_ENABLE
/** 控制台只读流描述，Init 成功后至 DeInit 前可用；串口句柄私有。
 * read 仅供 Shell 任务使用，不加接收锁；不得增加第二个读取者。
 * write 可由多个任务调用，在内部共用一把 TX 锁；禁止绕过它访问串口。
 * timeout 包含锁等待和串口提交；返回实际字节数，允许部分提交。
 * -1 时通过 aOS errno 取原因；未初始化为 A_ENODEV，锁忙为 A_EAGAIN。
 * 锁只覆盖一次 write，不覆盖多次写入组成的整行、命令或交互显示。
 * flush 为 NULL：已提交至串口发送缓冲区，不表示线路发送完成。
 * 此锁不保护 Init/DeInit，也不保护调用者的格式化缓冲区或消息队列。
 */
extern const aStream_t app_system_console_stream;

/**
 * @brief 初始化控制台 USART 并绑定 Stream，初始化 aShell 单例。
 * @return 已持有串口返回 BUSY；否则返回初始化错误或失败清理错误。
 * @note Shell 初始化失败时回收串口；回收失败保留句柄，须调用 DeInit 清理。
 * @note 仅 ASHELL_ENABLE 时声明；可能创建内部 OS 对象，不是无副作用查找。
 * 创建控制台 TX 锁，Shell 与日志通过同一个 stream.write 共享串口发送。
 * ASHELL_PRINT/aShellWrite 仍可供其他任务入队；命令回复仅限 Shell 任务。
 */
aStatus_t appSystemConsoleInit(void);

/**
 * @brief 释放控制台 USART、TX 锁与 Shell 队列，主要用于启动失败清理。
 * @return OK、NOT_READY（无串口实例）或底层清理错误。
 * @warning 任务上下文调用，必须先停止 Shell、日志及所有 stream 使用者。
 * 销毁 TX 锁前必须没有持锁者、等待者及新调用；本函数不负责停止任务。
 * 启动等待中的 Shell 任务尚未访问控制台，可在保持未就绪时清理。
 * 运行期不能仅调用此函数停止系统；应用仍须先协调任务及日志使用者。
 * 串口释放失败保留句柄、TX 锁与 Shell，允许重试；成功后丢弃排队输出。
 */
aStatus_t appSystemConsoleDeInit(void);
#endif
#endif

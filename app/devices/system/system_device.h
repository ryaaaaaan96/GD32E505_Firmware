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
#if APP_DATABASE_ENABLE
#include "aDataBase.h"
#endif
#if ASHELL_ENABLE || APP_LOG_ENABLE
#include "aStream.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif
#if APP_LOG_ENABLE
#include "aLog.h"
#endif
#if APP_MEMORY_ENABLE
#include "aMemory.h"
#endif

/**
 * @brief 初始化状态灯并取得共享句柄。
 * @param[out] handle_out 必填，成功返回模块持有的静态句柄，失败置 NULL。
 * @retval A_STATUS_OK 初始化成功，输出实例句柄。
 * @retval A_STATUS_INVALID_PARAM 输出指针为空。
 * @return 底层初始化错误直接返回，由上层处理，不在本层重试。
 */
aStatus_t appSystemStatusLedInit(aDevLedHandle_t **handle_out);
#if ASHELL_ENABLE || APP_LOG_ENABLE
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

/** 初始化控制台 USART 与 TX 锁；Shell/日志由 system 初始化层装配。
 * 已持有串口返回 BUSY；创建失败回收 TX 锁。
 * Shell 或应用日志启用时提供，不要求启用命令行。
 */
aStatus_t appSystemConsoleInit(void);

/** 释放串口及 TX 锁；调用者须先停止 Shell/日志等全部读写者。
 * 不销毁 Shell/日志自身状态。串口忙时保留资源，允许重试。
 * 返回 OK、NOT_READY（无串口）或串口释放错误。
 */
aStatus_t appSystemConsoleDeInit(void);
#endif
#if ASHELL_ENABLE
/** 仅填写 Shell 的流和超时配置；使用前需先初始化共享控制台。 */
void appSystemShellConfigInit(aShellConfig_t *config);
#endif

#if APP_LOG_ENABLE
/* 填充日志配置，不创建资源；使用输出回调前须完成 appSystemConsoleInit。
 * 回调直接共享控制台，使用 20 ms 总发送预算，部分提交返回 ERROR，不重试。
 * 日志锁由 aLog 管理，串口 TX 锁由控制台管理；此处不增加互斥对象。 */
void appSystemLogConfigInit(aLogConfig_t *config);
#endif

#if APP_MEMORY_ENABLE
/* 只填充注册配置，不访问硬件；描述及数组具有静态生命周期。
 * system_init.c 调用 aMemoryInit，后者负责设备探测、失败清理及区域发布。
 * 业务通过 aMemoryFind / Read / Write / Erase 访问区域。 */
void appSystemMemoryConfigInit(aMemoryConfig_t *config);
#endif

#if APP_DATABASE_ENABLE
/* 填充数据库配置并关联已注册的 aMemory 区域；不打开或格式化数据库。 */
void appSystemDatabaseKvConfigInit(aDataBaseKvConfig_t *config);
void appSystemDatabaseTsConfigInit(aDataBaseTsConfig_t *config);
#endif
#endif

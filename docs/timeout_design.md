# 统一超时方案

## 设计目标

本工程使用统一的超时语义，同时支持裸机、FreeRTOS 和 Linux。超时方案必须满足：

- aDrv 不依赖 aOS，不包含软件阻塞、延时或超时轮询；
- aOS 提供与运行环境相关的单调时基和等待能力；
- device 组合 aDrv 非阻塞操作，实现完整设备操作、阻塞和超时；
- func 管理协议或功能流程的总时间预算；
- app 选择具体超时值，并决定超时后的恢复策略；
- 更换 MCU 主要修改 aCore 和 aDrv，更换运行环境主要修改 aOS；
- 上层公共接口不暴露 FreeRTOS、Linux 或芯片厂商类型。

## 总体依赖

```text
app
├── func
│   └── device
│       ├── aDrv
│       ├── aOS
│       └── aLib
├── aOS
└── aLib

aDrv -> aLib + aCore/vendor
aOS  -> aLib + 对应运行环境适配
```

必须遵守以下依赖规则：

```text
aDrv 不得依赖 aOS
aLib 不得依赖 aCore、aDrv 或 aOS
aCore 不得依赖 aDrv
```

## 各层职责

### aLib

aLib 定义与硬件和操作系统无关的 `aBool_t` 和公共时间类型，并提供纯计算
函数。项目自研接口统一使用 `aBool_t`、`A_TRUE`、`A_FALSE`；固定二进制格式
与第三方源码的边界规则见 `platform/aLib/README.md`：

- `aTimeout_t`：调用者给出的等待要求；
- `aTimepoint_t`：一次操作的截止点；
- `A_TIMEOUT_NO_WAIT`：只尝试一次，不能等待；
- `A_TIMEOUT_MS(value)`：有限相对超时；
- `A_TIMEOUT_FOREVER`：永久等待；
- timepoint 创建、到期判断和剩余时间计算。

aLib 同时定义跨平台的 `aSSize_t` 和 `aErrno_t`。`aSSize_t` 是 POSIX
`ssize_t` 的平台无关替代类型，用于让流式 read/write 同时返回非负长度和 `-1`
错误。`aErrno_t` 使用 Linux/POSIX errno 的常见语义，但不要求数值与宿主 Linux
完全相同。

aLib 不读取系统时钟，不执行延时，也不依赖 aOS。

建议接口：

```c
typedef enum {
    A_TIMEOUT_TYPE_RELATIVE,
    A_TIMEOUT_TYPE_FOREVER
} aTimeoutType_t;

typedef struct {
    uint32_t milliseconds;
    aTimeoutType_t type;
} aTimeout_t;

typedef struct {
    uint32_t start_ms;
    uint32_t duration_ms;
    aBool_t forever;
} aTimepoint_t;

#define A_TIMEOUT_NO_WAIT \
    ((aTimeout_t){ .milliseconds = 0U, .type = A_TIMEOUT_TYPE_RELATIVE })

#define A_TIMEOUT_MS(value_) \
    ((aTimeout_t){ \
        .milliseconds = (uint32_t)(value_), \
        .type = A_TIMEOUT_TYPE_RELATIVE \
    })

#define A_TIMEOUT_FOREVER \
    ((aTimeout_t){ .milliseconds = 0U, .type = A_TIMEOUT_TYPE_FOREVER })

aTimepoint_t aTimepointCalc(aTimeout_t timeout, uint32_t now_ms);

aBool_t aTimepointExpired(const aTimepoint_t *timepoint,
                          uint32_t now_ms);

aTimeout_t aTimepointRemaining(const aTimepoint_t *timepoint,
                               uint32_t now_ms);
```

I/O 返回类型和 errno 建议定义为：

```c
#include <stddef.h>

typedef ptrdiff_t aSSize_t;

typedef enum {
    A_ERRNO_NONE = 0,
    A_EINVAL,
    A_EAGAIN,
    A_ETIMEDOUT,
    A_EIO,
    A_ENODEV,
    A_ENOTSUP,
    A_EINTR,
    A_ENOMEM
} aErrno_t;
```

`aSSize_t` 的最大正值就是一次 read/write 可报告的最大长度，超过该范围的请求必须
以 `A_EINVAL` 拒绝。

有限超时使用无符号时间差处理 32 位计数器回绕：

```c
uint32_t elapsed_ms = now_ms - timepoint->start_ms;
```

不允许直接比较 `now_ms >= deadline_ms`。

### aCore

aCore 只提供 CPU 架构和 CMSIS Core 支持，不提供系统 uptime，也不维护与 aOS
重复的 SysTick 计数。

芯片启动过程中等待晶振稳定等保护循环属于 startup/system，不属于公共软件超时
接口。

### aDrv

aDrv 只提供立即返回的硬件操作：

```text
Try     尝试一次读写
Start   启动硬件传输
GetState/GetStatus
Abort   终止硬件传输
```

例如：

```c
aStatus_t aDrvUsartTryReadByte(aDrvUsartHandle_t *handle,
                               uint8_t *data);

aStatus_t aDrvUsartTryWriteByte(aDrvUsartHandle_t *handle,
                                uint8_t data);

aStatus_t aDrvUsartIsTransmitComplete(
    const aDrvUsartHandle_t *handle,
    aBool_t *complete);
```

aDrv 公共操作中禁止出现：

```c
while (hardware_not_ready) {
}

aOSDelayMs(...);
aOSGetUptimeMs();
timeout_ms;
A_TIMEOUT_FOREVER;
```

aDrv 可以配置或报告外设自身的硬件 timeout，例如 I2C bus timeout、SDIO 命令
timeout。这类状态由外设硬件产生，不等同于 device 使用系统 uptime 计算的软件
操作超时。

### aOS

aOS 提供统一的单调系统时基、主动延时和操作系统同步能力：

```c
aStatus_t aOSInit(void);
uint32_t aOSGetUptimeMs(void);
void aOSDelayMs(uint32_t milliseconds);
void aOSYield(void);
aBool_t aOSPollWaitExpired(const aTimepoint_t *timepoint);
aSSize_t aOSFailWithTimeout(aTimeout_t timeout);
```

`aOSGetUptimeMs()` 返回启动后的单调毫秒数。它不是日历时间，也不是原始 RTOS
tick。

不同运行环境的适配关系：

| 环境 | uptime 来源 | delay 实现 |
|---|---|---|
| 裸机 | SysTick 或硬件定时器 | 忙等待、WFI 或协作式调度 |
| FreeRTOS | `xTaskGetTickCount()` | `vTaskDelay()` |
| Linux | `clock_gettime(CLOCK_MONOTONIC)` | `nanosleep()` |

FreeRTOS tick 转毫秒时使用 64 位中间值：

```c
milliseconds =
    (uint32_t)(((uint64_t)ticks * 1000ULL) / configTICK_RATE_HZ);
```

不能使用可能因整数计算变为零的 `ticks * portTICK_PERIOD_MS` 作为通用实现。

aOS 的信号量、消息队列、互斥锁和事件等待接口统一接收 `aTimeout_t`，并在 port
内部转换为 FreeRTOS `TickType_t`、Linux 时间结构或裸机等待方式。有限毫秒转换为
RTOS tick 时应向上取整，保证实际等待不短于调用者要求。

aOS port 还负责保存当前任务或线程的 `aErrno_t`：

```c
aErrno_t aOSGetErrno(void);
void aOSSetErrno(aErrno_t error);
```

- 裸机 port 可以使用一个静态错误槽；
- FreeRTOS port 必须使用任务局部存储，不能使用所有任务共享的全局变量；
- Linux port 映射到线程局部的系统 `errno`。

errno 只在线程或任务上下文中使用。ISR 和 aDrv 不读写 errno，而是直接返回
`aStatus_t`。

`aOSPollWaitExpired()` 组合 aLib 的纯截止时间计算与当前 aOS 的 uptime/yield：
截止时间已经到期时返回 `A_TRUE`，否则让出一次执行权并返回 `A_FALSE`。
`aOSFailWithTimeout()` 在 NO_WAIT 场景设置 `A_EAGAIN`，在有限等待到期时设置
`A_ETIMEDOUT`，然后返回 `-1`。两者只能在任务或线程上下文调用。

### device

device 使用 aDrv 非阻塞能力和 aOS 单调时基，实现一次完整设备操作：

- 阻塞轮询；
- 软件超时；
- POSIX 风格的部分传输返回值；
- 多步骤操作共享截止点；
- 必要时使用 aOS delay、yield 或同步对象释放 CPU。

流式 read/write 直接返回实际传输长度，不再增加 `bytes_read` 或
`bytes_written` 输出参数：

```c
aSSize_t aDevUsartRead(aDevUsartHandle_t *handle,
                       void *buffer,
                       size_t buffer_size,
                       aTimeout_t timeout);

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle,
                        const void *data,
                        size_t data_size,
                        aTimeout_t timeout);
```

返回语义与 POSIX read/write 保持一致：

| 返回值 | 含义 |
|---:|---|
| `> 0` | 实际传输的字节数，允许小于请求长度 |
| `0` | 请求长度为零；支持 EOF 的设备也可表示 EOF |
| `-1` | 没有完成可报告的数据，调用 `aOSGetErrno()` 查询原因 |

只有返回 `-1` 时 errno 才有效。已经传输部分数据后发生 timeout 或其他错误时，
read/write 返回部分长度，不修改 errno；调用者可以决定是否使用剩余 timepoint
继续操作。这与 Linux read/write 优先报告已完成字节数的行为一致。

建议的错误映射：

| `aErrno_t` | 场景 |
|---|---|
| `A_EINVAL` | handle、buffer、长度或 timeout 参数无效 |
| `A_EAGAIN` | NO_WAIT 下硬件暂时不可读或不可写 |
| `A_ETIMEDOUT` | 有限等待到期且没有传输任何字节 |
| `A_EIO` | 外设报告传输、奇偶、帧或总线错误 |
| `A_ENODEV` | 设备未初始化或已经离线 |
| `A_ENOTSUP` | 当前 port 不支持该操作 |
| `A_EINTR` | 等待被任务取消、信号或平台事件中断 |
| `A_ENOMEM` | 内存或其他动态资源分配失败 |

`A_TIMEOUT_NO_WAIT` 下必须至少尝试一次硬件操作。如果没有传输任何字节，返回
`-1` 并设置 `A_EAGAIN`。有限等待到期且没有传输任何字节时，返回 `-1` 并设置
`A_ETIMEDOUT`。

device 的阻塞接口只能在 aOS 时基已经初始化后使用，不得在 ISR 中调用。ISR 只
允许调用 aDrv 非阻塞接口。

### func

func 管理完整协议或功能流程的总时间预算，例如：

```text
Modbus 请求
├── RS485 发送
├── 等待发送完成
├── 等待响应
├── 接收完整帧
└── 校验和解析
```

func 在流程入口创建一次 timepoint，调用每个子操作前只传递剩余时间。禁止每个
子操作重新获得完整 timeout。

### app

app 决定产品的具体超时值：

```c
#define APP_CONSOLE_TIMEOUT A_TIMEOUT_MS(100U)
#define APP_MODBUS_TIMEOUT  A_TIMEOUT_MS(500U)
#define APP_FLASH_TIMEOUT   A_TIMEOUT_MS(5000U)
```

app 还负责决定超时后的行为，例如重试、记录日志、复位设备、切换备用链路或进入
降级状态。app 不实现底层寄存器轮询。

## 总时间预算

所有多阶段操作必须共享一个截止点。例如一次 RS485 发送包含：

```text
拉高 DE
-> 写入 USART 数据
-> 等待最后一个字节发送完成
-> 拉低 DE
```

正确处理方式：

```c
aTimepoint_t end =
    aTimepointCalc(timeout, aOSGetUptimeMs());

aSSize_t written = aDevUsartWrite(
    usart,
    data,
    size,
    aTimepointRemaining(&end, aOSGetUptimeMs()));

status = aDevUsartWaitTransmitComplete(
    usart,
    aTimepointRemaining(&end, aOSGetUptimeMs()));
```

禁止以下行为：

```text
写数据使用完整 500 ms
+ 等待发送完成重新使用完整 500 ms
+ 接收响应重新使用完整 500 ms
```

调用者给出的 500 ms 必须是整个操作的上限，而不是每个步骤各自的上限。

## 阻塞与上下文规则

| 上下文 | 允许的接口 |
|---|---|
| startup/aOS 初始化前 | aDrv 非阻塞接口、必要的 system 硬件保护等待 |
| 普通任务 | device/func 阻塞接口和 aOS 同步接口 |
| ISR | aDrv 非阻塞接口、专用 FromISR 接口 |
| Linux 线程 | device/func 阻塞接口和 POSIX aOS port |

USART 高速轮询路径不应固定执行 `aOSDelayMs(1U)`，否则可能造成接收寄存器溢出。
轮询实现可以使用紧密状态检查或 `aOSYield()`；高吞吐量场景应增加中断或 DMA
device 实现。

## 返回值和错误语义

配置、控制和底层非阻塞接口继续直接返回 `aStatus_t`：

| 状态 | 含义 |
|---|---|
| `A_STATUS_OK` | 操作完成 |
| `A_STATUS_BUSY` | NO_WAIT 操作暂时无法执行，或者硬件仍在运行 |
| `A_STATUS_TIMEOUT` | 有限软件时间预算已经耗尽 |
| `A_STATUS_NOT_READY` | handle 或设备尚未初始化 |
| `A_STATUS_UNSUPPORTED` | 当前 port 不支持该能力 |
| `A_STATUS_ERROR` | 无法归类的硬件或设备错误 |

硬件 timeout 可以映射为 `A_STATUS_TIMEOUT`，但驱动状态结构应保留其硬件来源，
便于诊断时区分软件预算耗尽和外设主动报告 timeout。

流式 device/func read/write 使用 `aSSize_t`：

```text
非负值：实际传输长度
-1：失败，具体原因由 aOSGetErrno() 提供
```

不得让 aDrv 设置 errno。aDrv 返回的 `aStatus_t` 通过 aLib 的
`aStatusToErrno()` 统一映射为 `aErrno_t`；需要返回 `-1` 的 OS 感知接口使用
`aOSFailWithStatus()` 设置任务 errno。errno 是任务或线程局部的附加错误信息，
不能保存在共享 device handle 中。

## 当前实现状态

当前 GD32E505 + FreeRTOS 组合已经完成以下迁移：

- aLib 已提供 `aTimeout_t`、`aTimepoint_t`、`aSSize_t`、`aErrno_t` 和统一状态映射；
- 已验证 NO_WAIT、FOREVER、普通到期和 32 位计数回绕；
- aOS 已提供单调毫秒 uptime、delay、yield 和任务局部 errno；
- aDrv 已删除系统时基、延时以及 USART/SPI 软件超时轮询；
- USART/SPI aDrv 接口只尝试一次或查询一次硬件状态，SQPI 特殊命令也已改为
  启动与完成查询分离；
- USART 和 RS485 流式接口返回长度或 `-1`，具体错误通过 aOS errno 查询；
- RS485 写入和发送完成使用同一个 timepoint；
- Flash25Q 的页写、扇区擦除和整片擦除使用调用者给出的总时间预算；
- app 已使用 `A_TIMEOUT_*` 明确选择控制台超时；
- 旧 timeout 接口和兼容别名已经删除。

裸机与 Linux 的 aOS port 尚未加入。当前 aModbus 只有 CRC 和帧校验，没有通信
事务接口，因此暂时不存在可迁移的 Modbus 超时调用链。以后增加 Modbus 请求接口
时，必须在请求入口创建一次 timepoint，并向 RS485 子操作传递剩余预算。

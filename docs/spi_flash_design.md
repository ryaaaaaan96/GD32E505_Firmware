# Flash25Q / SFUD / SPI / QSPI 整体设计

## 状态与范围

沿用 `device/aDev_Flash25q` 和 `aDevFlash25q` 命名，不新增 a25q。
当前已完成 SFUD 封装、同步 SPI 后端和 SPI1 板级初始化。
QSPI 后端仍是后续设计，尚未接入；不能宣称全链路已上板验证。
SFUD 已下载到 `device/aDev_Flash25q/SFUD/`，上游提交为
`6b4bef82e6c603b783a17968f1b75da89cc5e2f8`。

本阶段实现以 SPI 板卡为准：封装 SFUD，并完善 SPI
事务能力。不新增 aDevSpi、aDevQspi、aSerialMem 或 aMemory 模块。
未来 func/aMemory 可以组合此设备接口，但分区、缓存、文件系统、磨损均衡
和持久化策略不进入当前模块。

## 整体链路与职责

```text
应用业务（未来可接 aMemory / FlashDB）
                    |
          Read / Write / Erase / GetInfo
                    |
device/aDev_Flash25q：生命周期、请求检查、同步、超时、错误转换
                    |
               SFUD 官方核心
          识别 / SFDP / 分页编程 / 擦除
                    |
             本项目 SFUD port
           +--------+---------+
           |                  |
       SPI 事务适配        QSPI 命令适配
           |                  |
        aDrvSpi            aDrvQspi
           |                  |
        GD32 SPI           GD32 SQPI
```

应用设备初始化层知道控制器、引脚和接线，业务读写层只知道 Flash 句柄
和字节地址。SFUD 类型不进入公共业务 API。

aDrv 不依赖 SFUD、aOS、系统时间或堆分配；提供硬件配置、传输推进、
完成查询和中止恢复。device 的 port 负责等待、总线互斥、deadline 和
SFUD 回调转换。SPI/QSPI 保持不同硬件接口，不强行统一成字节流。

## 目录和官方源码边界

```text
device/aDev_Flash25q/
├── aDev_flash25q.h                 公共接口
├── aDev_flash25q.c                 生命周期、统一操作入口
├── aDev_flash25q_instance.h        静态实例布局
├── aDev_flash25q_internal.h        内部状态和端口声明
├── config/                        项目 SFUD 配置
├── port/                          项目移植实现
│   ├── aDev_flash25q_sfud_port.c   SFUD 回调适配、芯片忙状态轮询
│   ├── aDev_flash25q_spi_bus.c     SPI 总线事务、片选与字节收发
│   └── aDev_flash25q_qspi.c        尚未实现
├── SFUD/                          官方仓库，保持原样
│   └── sfud/{inc,src,port}/
└── CMakeLists.txt
```

SFUD 的 `wr` 回调由内部函数 `sfud_write_read` 实现，负责错误转换
和芯片忙状态轮询；它调用 `aDevFlash25qSpiTransaction` 执行一次完整
SPI 事务。后者管理总线锁、片选和字节收发，不负责 Flash 命令生成。
`sfud_spi_port_init` 是官方约定的移植入口，保留原名。

优先通过官方配置、`sfud_spi_port_init`、`wr`、`qspi_read` 和 user_data
扩展点接入。保留上游许可，不修改官方移植模板，不编译官方 demo。
项目配置目录优先于上游 include；需实际核对预处理输出，避免误用示例配置。
使用 `sfud_device_init` 初始化每个私有对象，不用官方全局设备表管理业务实例。
构建时仍提供 SFUD 要求的合法设备表配置，不向业务暴露该表。

## 对象关系与所有权

```text
一个物理控制器
    └── 一个 Flash25Q 总线上下文
         ├── aDrv 控制器句柄
         ├── 唯一总线 mutex
         ├── 控制器类型、配置、故障状态
         └── 多个 Flash 设备句柄（每个对应一个物理器件）
              ├── 借用总线上下文
              ├── SPI 从设备配置：CS（速率、模式由 bus 共享）
              ├── 私有 sfud_flash
              ├── 当前操作 deadline 和首个底层错误
              └── 生命周期及动态对象所有权
```

总线上下文仍属于 Flash25Q 模块的内部适配，不新增通用 SPI device 层。
其静态存储布局放 instance 头文件。应用初始化层持有总线，负责先初始化总线、
后初始化 Flash；先停止业务、销毁 Flash，再销毁总线。

每个物理控制器只允许一个上下文。SPI 可以挂多个 CS；GD32 SQPI 首版
仅允许一个受支持的器件连接，不假定其具有多个独立硬件片选。
其他模块共享 SPI 时必须使用同一控制器和同一总线锁，不得另建独立锁。
总线对象记录借用计数，有 Flash 实例存活时拒绝销毁。

控制器资源和 Flash 对象分开创建，避免初始化第二个器件时重置正在使用的
控制器。跨实例生命周期操作由应用在初始化/关闭阶段串行编排。

## 公共接口方案

以下公共接口已实现，完整声明和约定以 aDev_flash25q.h 为准。

```c
typedef struct aDevFlash25qHandle aDevFlash25qHandle_t;
typedef struct aDevFlash25qBus aDevFlash25qBus_t;

typedef struct {
    uint32_t address;
    void *data;
    size_t size;
    aTimeout_t timeout;
} aDevFlash25qReadRequest_t;

typedef struct {
    uint32_t address;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aDevFlash25qWriteRequest_t;

typedef struct {
    uint32_t address;
    size_t size;
    aTimeout_t timeout;
} aDevFlash25qEraseRequest_t;

aStatus_t aDevFlash25qInitStatic(
    const aDevFlash25qConfig_t *config,
    aDevFlash25qHandle_t *handle);
aStatus_t aDevFlash25qCreate(
    const aDevFlash25qConfig_t *config,
    aDevFlash25qHandle_t **handle_out);
aStatus_t aDevFlash25qRead(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qReadRequest_t *request);
aStatus_t aDevFlash25qWrite(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qWriteRequest_t *request);
aStatus_t aDevFlash25qErase(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qEraseRequest_t *request);
aStatus_t aDevFlash25qGetInfo(
    const aDevFlash25qHandle_t *handle,
    aDevFlash25qInfo_t *info);
```

同时提供对应 StructInit、DeInitStatic、Destroy，以及总线配置和静态
总线 Init/DeInit 接口。首版总线上下文由应用静态提供；Flash 对象支持
静态和动态两种创建。静态对象仍可能由 aOS 分配内部 mutex。

配置包含：借用的 bus、CS、期望容量（0 表示采用探测结果）和初始化超时。
当前仅普通单线读，不提供读模式切换。配置值复制，bus 必须持续有效。
GetInfo 返回探测到的 JEDEC ID、容量和擦除粒度；不暴露
SFUD 内部指针，不把应用配置容量伪装成探测结果。

首版同步任务接口，不提供 ISR、异步提交或 flush。请求及数据仅在调用
期间借用，返回后后端不得继续访问。状态使用 aStatus_t，不混用流式 errno。

- Read：成功表示完整读取。
- Write：只编程、不自动擦除；成功表示全部编程请求完成且芯片就绪，
  不等于额外读回校验通过。保护位可能影响编程，需测试及明确错误能力。
- Erase：地址和长度必须严格按探测的最小擦除粒度对齐；不允许 SFUD
  自动扩大区域影响请求范围以外的数据。整片擦除可用显式全容量请求。
- size 必须非零，范围使用 `size <= capacity - address` 检查溢出。
- 失败可能已有部分数据改变，不回滚；超时不等于 Flash 内部操作被取消。
- Destroy 拒绝静态对象，DeInitStatic 拒绝动态对象；活动对象禁止复制。
- 初始化失败释放本层资源，清空输出句柄；不能承诺撤销已经发出的芯片命令。

## SFUD 适配与内存

`spi.user_data` 指向所属 Flash 实例。port 从实例找到控制器及本次 deadline，
不使用“当前实例”的可变全局指针，不用线程局部变量模拟上下文。

SFUD `wr` 是一次“先发送、再接收”的事务：从第一字节命令到最后一个
接收字节保持 CS，不能将发送和接收拆成两个独立事务。
SFUD `qspi_read` 带结构化读命令格式，用于快速读取；普通写命令仍走 wr。

当前上游 `page256_or_1_byte_write` 内使用静态 cmd_data，且把用户数据
复制到命令缓冲区。保留官方代码时必须接受这一次页数据复制，不能声称
Flash Write 为零拷贝。port 不再增加整页复制或逐次堆分配。

Read 尽量直接写入用户缓冲区；若后续 DMA 有对齐或可访问内存限制，必须
在后端说明，不以未经检查的强制转换处理。

## 同步策略

当前 SFUD 的静态页缓冲区跨实例共享。首版采用一个模块级 SFUD mutex，
覆盖每次完整 SFUD 调用，包括初始化、读、写、擦除。它同时承担设备操作
串行保护，不再给每个 Flash 创建功能重复的操作锁。

该 mutex 随首个 bus 初始化创建，最后一个 bus 销毁后释放，引用计数的更新
要求跨实例生命周期串行。任何初始化失败必须撤销相应资源和引用。
不同 Flash 的业务操作暂时不能并行，这是保持上游原样的明确代价。

另有每个控制器的总线 mutex，只保护一次 CS 事务。固定顺序：

```text
SFUD 模块锁 → 控制器总线锁 → 硬件事务 → 释放总线锁
            → 状态轮询（每次重新获取总线锁）→ 释放模块锁
```

等待 WIP 时不持有总线锁，因此其他非 SFUD 设备仍能访问共享控制器。
SFUD 的 void lock/unlock 回调不能报告超时，首版不依赖这些回调获取锁；
由封装在调用 SFUD 前检查锁结果，避免失败后 SFUD 继续访问硬件。
禁止绕过封装直接调用 SFUD，否则无法保证共享缓冲区和对象安全。

## 超时和错误传播

一次请求在加模块锁之前计算 deadline；锁等待、传输和所有页/扇区的
状态等待使用同一预算。不能为每次 SFUD 回调重新开启完整 timeout。

aDrv 只推进或查询硬件。port 根据剩余预算调用 aOS 等待，预算耗尽时
中止总线事务并保存首个错误。SFUD 后续清理命令不能覆盖原始错误。
返回错误优先级：底层首错 → SFUD 错误映射 → 解锁错误。

当前 SFUD wait_busy 在底层读状态失败时仍按 retry.times 重试。
首版方案是把 RDSR 的 WIP 等待放入 port：

1. 对 SFUD 的标准 RDSR 请求，在单次总线事务后检查 WIP。
2. 忙时释放总线锁，按剩余预算让出 CPU，再发下一次 RDSR。
3. 就绪时返回真实状态字节（保留 WEL 等位），失败时记录错误并返回失败。
4. SFUD retry.times 设为 0，避免内部再次进行无预算的错误重试。
   该配置必须通过真实 SFUD 源码测试，不能仅靠 mock 的 SFUD 函数验证。

初始化和普通操作都走同一预算路径；SFDP 读取出现总线错误后不能被
上游参数表回退掩盖。SPI/QSPI port 发现首错后停止提交后续硬件命令。
当前明确拒绝 NO_WAIT，返回 UNSUPPORTED；支持正数预算和 FOREVER。
超时是软件协作式截止，不能中断一次不可抢占的硬件寄存器访问。

## SPI aDrv 链路

沿用 USART 的静态驱动句柄、厂商类型隔离与按配置裁剪。
将控制器状态与每个从设备的 CS/配置区分开，避免共享控制器重复初始化。

首版为 8 位主机、MSB、软件 CS、轮询传输；设备模式和分频在持有总线锁、
且总线空闲时配置。需要的底层能力为：

- 配置初始化与参数能力检查。
- 单帧 TryWrite/TryRead（可以沿用现有接口）。
- 线路完成和硬件错误查询。
- 片选控制，以及超时后的停止、排空和恢复。

port 在同一次 CS 下发送 SFUD 的整个 write_buf，再产生 read_size 个
时钟读取数据。TX-only 也必须读出接收数据；RX-only 发送 dummy 字节。
释放 CS 前确认移位完成，不能只判断发送寄存器空。

后续 DMA 可替换事务推进方式，公共 Flash API 不变；DMA 完成不代表
最后一个 SPI 位已经发送。超时后先停止 DMA/控制器对缓冲区的访问再返回。
恢复失败的总线进入 FAULT，后续操作返回 NOT_READY，显式重新初始化恢复。

## QSPI aDrv 链路

QSPI 需要“结构化命令 + 数据 + 完成查询/中止”，与 SPI 字节串不同。
wr 到 QSPI 的适配必须按当前支持的 SFUD 命令集合解析 opcode、地址字节、
dummy 和 payload；不得把 write_size - 1 一律当作地址或数据。

需覆盖探测、SFDP、复位、状态、WREN/WRDI、普通读、页编程、擦除及实际
启用的地址模式。未知命令和不支持的线数组合在访问硬件前返回 UNSUPPORTED。
记录 3/4 字节地址模式必须与成功提交的模式切换一致，不根据长度猜测。

qspi_read 直接转换官方格式，但需核对 alternate bytes、dummy cycles、
地址位数和线数。四线模式还必须解决所支持器件的 QE 配置与复位影响；
不能仅调用 sfud_qspi_fast_read_enable 就认为已完成芯片四线使能。
默认单线；快速模式能力未完成时返回 UNSUPPORTED，不静默降级。

现有 GD32 实现有两个明确问题需在实现阶段解决：

- command_mode 当前把 1-1-4 与 1-4-4 都映射 SQQ；厂商头文件实际提供
  SSQ 和 SQQ。1-1-2 与 1-2-2 也需要区分 SSD/SDD。
- Transmit 逐字节写映射窗口，不能据此证明一页数据在一次 CS 下提交。
  必须核对硬件手册并用逻辑分析仪验证；若硬件无法满足完整编程事务，
  此后端不得宣称支持 W25Q 编程，也不能简单循环写窗口替代。

QSPI 路径的完整可用性属于实施验证项。不能因芯片提供 SQPI 外设就假定
它等价于其他 MCU 的通用 QSPI 间接传输控制器。

## 构建、应用和迁移

保留 aFlash25q target 和 ADEV_FLASH25Q_ENABLE，后者目前依赖
ADRV_MODULE_SPI_ENABLE。STATIC_ENABLE / DYNAMIC_ENABLE 已实现，
至少启用一种创建方式。尚无 QSPI port 开关；启用 aDrvQspi 并不会自动
使 Flash25Q 支持 QSPI。

SFUD 作为 OBJECT target 编译并合入 aFlash25q，官方与项目代码均继承
aclass_project_options。项目配置开启 SFUD 内部断言，关闭调试输出，
不引入上游静态日志缓冲区。配置宏涉及 sfud_flash 布局，所有使用方使用
同一配置。公开业务头文件没有 SFUD 类型，静态实例头文件包含其内部布局。

app/devices/system/app_system_flash.c 私有持有 bus 和 handle，使用
ADRV_SPI_1 / PB13(SCK) / PB15(MOSI) / PB14(MISO) / PB12(CS)，
Mode 0、64 分频。SPI 编号已改为 ADRV_SPI_0/1/2，与 GD32 完全一致。
aSystemInit 在 Shell 后探测 Flash，打印探测信息，不自动擦写。
FlashDB 后端已迁移到请求结构体，并从 GetInfo 获取真实擦除粒度。
旧 Init/DeInit/GetSize/HandleIsValid/ChipErase 和指针式 IoCtl 已移除；
整片擦除由显式全容量 Erase 请求表达，不保留兼容别名。

Linux 后端可在 port 层提供事务操作，公共设备读写语义保持一致；
不要求模拟 GD32 寄存器、GPIO 或中断模型。

## 验收与后续工作

1. 项目 SFUD 配置及独立构建，核对上游 git diff 为空。
2. 公共请求、静态/动态对象、生命周期与 SFUD port，使用真实 SFUD 核心
   加模拟 Flash 总线测试，覆盖 JEDEC/SFDP 和参数表回退。
3. SPI 事务实现，验证 CS 连续性、收发排空、错误恢复及共享控制器。
4. QSPI 命令解析、模式映射与硬件可行性验证，通过后接入。
5. FlashDB 调用方及文档迁移；构建 SPI-only、QSPI-only、双后端、关闭、
   静态和动态配置，检查链接无缺失及无新增项目告警。
6. 测试跨页写入、严格擦除范围、越界、锁失败、总超时、底层首错保留、
   多实例静态缓冲区保护、初始化回滚、销毁所有权和超时后再次访问。
7. 上板核对器件完整型号、控制器、引脚和安全测试区后，验证 ID、擦写回读、
   CS 边界及恢复。普通读和快速读分别验证，不以编译通过替代硬件验证。

此设计不会自动解决 SQPI 的硬件限制。后续实现应先建立可靠的 SPI 路径，
再按证据开放 QSPI 能力；官方 SFUD 源码默认保持不变。

### 本次验证范围

Debug 固件以及 tests/flash25q 下的 host 与构建矩阵用于验证本实现。
Host 使用真实 SFUD 核心、模拟 GPIO/SPI/aOS，执行静态、动态及两者同时
启用的配置。它验证无 SFDP 器件的参数表回退，不替代真实型号的 SFDP、
电气连接、CS 时序或上板擦写测试。

# aModbus

公共业务接口不包含 nanoMODBUS 类型。仅启用动态分配时，上游头目录和
`NMBS_*` 裁剪宏为库私有；启用静态分配时，实例布局头需要这些依赖，
由 CMake 向实例创建方传播，保证双方布局一致。

基于 nanoMODBUS 的同步主从站模块，默认依赖 aBus。
官方源码位于 `nanoMODBUS/`，当前版本为提交 `91d6782`，保持原样。
工程直接编译官方 `nanomodbus.c`，继承统一的 C11、优化和告警参数，
不执行上游工程的 CMake，也不编译其示例和测试。

## 分层和生命周期

```text
应用通信任务 → aModbus → nanoMODBUS
                 ├─ 传输回调 → aDevUsart / Linux socket
                 └─ 地址映射、编码 → SIG 回调 / 默认 aBus
```

模块不创建任务，不接管设备或 aBus 的生命周期，不设置全局单例。
每个实例在初始化时选择 CLIENT 或 SERVER，角色在运行期间固定。
多个实例可以借用同一 aBus；一个实例可以访问 handle 下的多个 deviceID。
先初始化 aOS、aBus 和传输设备，再初始化协议；关闭时先停止通信调用，
销毁协议，再释放传输和 aBus。配置结构体复制保存，配置引用的地址段、
映射、context、传输资源和 aBus 在整个实例生命周期内必须保持有效。

仅任务上下文调用，同一实例使用原子使用标志拒绝并发/重入，返回 BUSY。
应用应让一个通信任务管理一条 RTU 链路，按完整事务串行化所有主站请求。
不能让多个从站实例抢读同一串口；一个 RTU 从站地址可以映射多张 aBus 表。
Linux 的 socket 建立、accept、关闭、重连和任务调度由应用完成。

静态接口为 `aModbusInitStatic(config, handle)` 和 `aModbusDeInitStatic`；
存储定义在 `aModbus_instance.h`，字段私有，初始化后禁止复制或移动。
动态接口为 `aModbusCreate(config, handle_out)` 和 `aModbusDestroy`。
运行期无堆分配，不创建协议内部 mutex；静态协议实例不申请堆，
但 aBus 的锁是否申请堆由 aBus/aOS 自身决定。
配置初始化使用各个 `StructInit` 接口，静态只读表使用指定成员初始化。

## 主设备

主站主动发出请求，采样周期、优先级和重试由应用任务决定。

| 接口 | 数据流 |
| --- | --- |
| `aModbusClientRead` | 远端 → 调用者协议数值缓冲区 |
| `aModbusClientWrite` | 调用者协议数值缓冲区 → 远端 |
| `aModbusClientReadSig` | 远端 → 解码 → 本地 aBus SIG/Param |
| `aModbusClientWriteSig` | 本地 aBus SIG/Param → 编码 → 远端 |

普通读写请求内的 `access` 选择区域、零起始地址、数量、缓冲区和总超时。
寄存器数据为正确对齐的本地 `uint16_t[]`，位数据为低位在前的紧凑位数组。
例如 10 个线圈需要 2 字节；寄存器数组不是已经编码的大端字节数组。
失败时输出可能已部分改写，只有返回 OK 时才消费输出。

SIG 请求的本地目标通过 `deviceID + sigIndex + paramIndex` 定位。
数量根据 aBus 的类型/长度自动决定。主站的远端地址不使用从站地址段表，
可以直接对应不同设备的寄存器布局。
Modbus `unit_id` 与本地 aBus `deviceID` 独立；RTU 主站目标为 1..247，
站号 0 仅允许广播写入，不等待应答。TCP 支持 0..255 的 Unit Identifier。

```c
aModbusClientSigRequest_t request;
aModbusClientSigRequestStructInit(&request);
request.unit_id = 2U;
request.area = AMODBUS_AREA_INPUT_REGISTERS;
request.address = 100U;
request.target.deviceID = 1U;
request.target.sigIndex = IDU_SIG_COUNTER;
request.timeout = A_TIMEOUT_MS(500U);
status = aModbusClientReadSig(client, &request);
```

读取失败不会发布本地数据，也不会自动清零旧数据。
通信健康状态、采样有效性和最后采样时间由业务另建 SIG 表达。

## 从设备与地址段

应用循环调用 `aModbusServerProcess(handle, request)`。
没有输入时，本次等待结束返回 OK；这不表示一定处理了一条请求。
从站访问由地址段分发，地址段按 `area、address` 排序，同一区域禁止重叠。
区域分别是 COILS、DISCRETE_INPUTS、HOLDING_REGISTERS、INPUT_REGISTERS。
输入区域只允许读；保持寄存器/线圈可进一步配置为只读。

每段选择一种处理方式：

1. `maps + map_count`：通过 SIG 映射处理，不再设置地址段读写回调。
2. `read/write + context`：直接按协议地址、数量和数值处理，不再设置 maps。

一个请求允许跨相邻地址段，模块先检查全部覆盖和权限。
段内映射可以留空洞，但访问空洞返回非法地址；不会自动返回零值。
跨段位请求会重新排列紧凑位数组，回调的第一个地址始终对应 data 的 bit 0。
初始化始终检查地址边界、排序、重叠、权限、目标、类型、长度和处理方式。

## SIG 映射

`aModbusBusMap_t` 保存协议起始地址、权限、本地目标和字序。
类型、长度和上下限读取 aBus 元信息，不维护第二份数据定义。
定位使用 sigIndex，sigKey 继续作为业务稳定标识；两者都不是寄存器地址。
映射按协议地址排序、禁止覆盖；查找地址段和映射均使用二分查找。

| aBus 类型 | 占用和编码 |
| --- | --- |
| U8 | 一寄存器，低 8 位；写入超过 255 拒绝 |
| U16 | 一寄存器 |
| U32/S32 | 两寄存器，可选高字在前或低字在前 |
| RAW | 两字节一寄存器，前一字节在高 8 位 |
| STRUCT | 必须分别映射 Param，不直接导出结构体布局 |

位区映射仅接受 U8，当前值须为 0/1。
RAW 的奇数长度最后一个低字节补零，写入时非零补位拒绝。
单个映射最大 250 字节；可写映射最多 246 字节，受 FC10 的数量限制。
LOW_FIRST 只用于 U32/S32；RAW 和其他类型使用 HIGH_FIRST。
读取一个 32 位映射时先取得完整值，再拆字，允许请求只读取其中一个寄存器。
写入必须完整覆盖该映射，禁止先写高字再写低字，也不提供拼接缓存。

`paramIndex = AMODBUS_SIG_WHOLE` 表示整个标量/RAW SIG；0 表示第一个字段。
使用静态指定成员初始化时必须显式填写这个选择，不能省略后依赖清零。

```c
static const aModbusBusMap_t maps[] = {
    {
        .address = 0U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {
            .deviceID = 1U,
            .sigIndex = IDU_SIG_COUNTER,
            .paramIndex = AMODBUS_SIG_WHOLE
        },
        .word_order = AMODBUS_WORD_HIGH_FIRST
    },
    {
        .address = 2U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_MOTOR,
            .paramIndex = 0U
        }
    }
};
static const aModbusAddressRange_t ranges[] = {
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 0U,
        .quantity = 4U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .maps = maps,
        .map_count = sizeof(maps) / sizeof(maps[0])
    }
};

/* 示例放在持有私有 sig_handle 的应用模块内，不暴露 aBus handle。 */
aStatus_t sigModbusServerInit(const aModbusTransport_t *transport,
                               aModbusHandle_t *instance)
{
    aModbusConfig_t config;
    aModbusConfigStructInit(&config);
    config.bus = sig_handle;
    config.transport = *transport;
    config.ranges = ranges;
    config.range_count = sizeof(ranges) / sizeof(ranges[0]);
    return aModbusInitStatic(&config, instance);
}
```

此片段说明绑定方式。当前板级实例见
[产品协议](../../app/protocol/README.md)：USART2 PC10/PC11、PA15 DE，
使用 dataBusModbusCreate / dataBusModbusInitStatic 绑定私有 aBus 实例。

## 两级回调与并发

地址段回调收到的是协议数据：寄存器为本地 uint16_t 数值，线圈为紧凑位数组。
SIG 回调收到的是本地业务类型字节，长度与 aBus 元信息完全一致。
配置中的 `sig_read/sig_write` 是统一分发入口，携带目标标识供业务选择。
未配置回调时默认调用 aBus 整组或 Param 接口。
自定义回调必须整项完成，不得持有借用缓冲区或重入当前协议实例。
所有回调都在当前调用者任务中执行，不在 ISR 中执行。

Param 修改直接调用 aBusSetParam，保留其他字段，不通过整组读改写覆盖。
写入先检查所有地址、权限、完整覆盖、编码和各项整数范围，再逐项提交。
提交时仍由 aBus 执行其最终范围检查和锁等待。
跨 SIG、跨字段、跨段不提供事务性；提交期间锁/回调失败可能已有部分更新。
读多个字段也不承诺整批来自同一采样时刻。
需要这种一致性时，应增加 aBus 批量快照/提交能力，不能用 Modbus 的实例
使用标志替代 aBus 同步。应用直接访问静态绑定变量的同步仍由应用决定。
直接地址回调的业务校验由回调负责，模块无法替它预检查业务数据。

## 传输、超时和恢复

读写适配使用项目 errno，部分进度由封装继续补齐。
整次 API 调用共享一个超时预算，覆盖传输、辅助回调和 aBus 等待，支持回绕。
byte_timeout 限制每次字节等待，有进度时重置；总预算始终不重置。
所有回调都必须遵守收到的剩余预算，模块不能强制中断超时不返回的回调。
NO_WAIT 只进行立即尝试，可能完成已有数据，也可能返回 BUSY。

RTU 三个辅助回调必须提供：

- `discard_input`：恢复接收帧边界、清理残留；不是输出 flush。
- `prepare_frame`：确认可以开始新帧，处理帧间静默和前次发送恢复。
- `wait_transmit_complete`：确认最后一个停止位发送完毕及 DE 已释放。

模块提供通用 RTU 分帧实例，也允许应用提供自定义 transport。
字节收发统一使用 `aModbusTransport_t.stream`（`aStream_t`）。
仅 USART IDLE 或毫秒超时不足以保证严格 RTU 接收时序。
不要将 Shell 与 Modbus 绑定到同一条接收流。
TCP 可将三个辅助回调设为 NULL，不应在每次请求前清理 socket 中的合法数据。
aStream 不携带 context，每个端口提供自己的适配函数并绑定私有实例。
transport.context 只供 RTU 辅助操作使用，不能用它区分 Stream 读写实例。
当前接口适合固定端口；运行时任意数量的动态 socket 连接需要额外设计端口管理，
本次不宣称支持动态连接池。

可选 `finish` 回调在每笔操作结束、释放实例使用权之前调用，成功和失败
路径均执行。它只释放当前接收帧，禁止清空后续排队帧；TCP 一般设为 NULL。

RTU 发生错误后通过 discard_input 恢复；操作可能已有部分字节发送，
不自动重试，业务决定是否重新发送控制命令。
TCP 半帧、传输或协议错误后锁定为故障状态，后续调用返回 NOT_READY，
应用须关闭旧连接并重建连接/协议实例，避免把余下半帧当成新帧解析。
封装对官方固定帧缓冲区做接收前保护，并额外验证 TCP 功能码对应的 PDU 长度，
避免畸形短帧使用上一帧残留的数据。

## RTU 传输实例

每条链路独立保存帧队列、时间戳及读取位置，不使用全局单例。
协议实例和传输实例一一绑定；角色和从站地址须配置一致。
接收固定为 3 个 256 字节槽位及一个当前帧快照，运行时不分配内存。
队列满时丢弃新帧；硬件错误、帧内间隔超限或超长帧被整体丢弃。
逐字节路径只采样一次时钟并比较整数差，时间阈值在初始化时计算。

| 接口 | 用途 |
| --- | --- |
| `aModbusRtuConfigStructInit` | 填充通用 RTU 默认配置 |
| `aModbusRtuInitStatic` / `aModbusRtuCreate` | 初始化独立 RTU 状态 |
| `aModbusRtuReceive` | ISR 或受同步保护的单生产者输入字节与错误 |
| `aModbusRtuRead` / `aModbusRtuWrite` | 供应用薄适配函数转成 Stream 回调 |
| `aModbusRtuBindTransport` | 给应用 Stream 补充帧间隔、发送完成及收尾操作 |
| `aModbusRtuDeInitStatic` / `aModbusRtuDestroy` | 接收和协议调用停止后释放 |

通用接口由 `aModbus_rtu.h` 声明；静态布局见 `aModbus_rtu_instance.h`。
调用者通过 `aModbusRtuIo_t` 提供计数器、同步及线路完成操作，
通过 `aModbusRtuConfig_t.output` 提供物理输出 Stream；其 read 不使用。
消费任务通过 enter/exit 排除生产者；线程生产者必须使用同一同步机制。
计数器须以固定频率运行，支持 32 位回绕；aOS 毫秒时基用于判断长空闲。
Linux 适配可以使用通用 RTU 或自定义 transport，但普通串口批量读取的
返回时间不能还原逐字节到达时间，不能直接补送字节来声称严格的 RTU 定时。

## 应用端口与协议装配

`aModbus` 不包含 aDevUsart、GPIO 或 DWT，不创建/关闭具体设备或 TCP 连接。
CMake 只有 `aModbus` 目标，依赖 aBus/aOS/aLib，不再提供 `aModbusUsart`。
此前直接拥有 USART 的组合接口已移除，应用通过公开接口编排生命周期。
接口迁移：原 transport.read/write 改为 transport.stream.read/write，
移除读写回调的 context 参数；原 aModbusRtuGetTransport 改为
aModbusRtuBindTransport，并由应用先提供帧读写的 Stream 适配。

当前应用由 [protocol.c](../../app/protocol/protocol.c)
装配协议，物理端口在 [rs485_device.c](../../app/devices/rs485/rs485_device.c)：

1. rs485PortPrepare 提供串口参数、物理输出流和时基操作，不创建 RTU。
2. 协议服务创建 RTU，适配帧 Stream，再创建 aModbus 并校验 aBus 映射。
3. rs485PortOpen 将接收回调绑定到已就绪的 RTU，最后打开 USART。
4. 服务创建自己的通信任务；system 只调用 protocolInit。
5. 停止所有调用者后，先销毁协议，再关闭串口，最后释放 RTU。
   串口关闭 BUSY 时保留 RTU，后续允许重试关闭。

`aModbus.h` 提供 aModbusServiceConfig_t，统一组合 aModbusConfig_t、
主站采集列表、从站处理请求、aOSTaskConfig_t 及成功/失败后的等待时间。
该类型只描述配置，库不自动创建任务、不执行轮询调度，也不持有应用任务。
具体设备的配置实例由 app 定义，库中没有 IDU/FAN 等设备声明。
装配时复制协议模板，填入具体 transport 并绑定私有 aBus；模板及点表保持只读。
静态模板须明确字节等待策略，当前应用为 20 ms；局部配置可调用
aModbusServiceConfigStructInit 获取整组默认值：协议按 aModbusConfigStructInit，
从站处理超时为 100 ms，任务名为 modbus、普通优先级、栈使用 aOS 默认容量，
成功等待 0 ms、失败等待 5 ms。应用须补齐端口、总线、任务入口和采集或映射配置。
初始化函数仅填写结构体，不申请资源；只配置协议实例时仍用 aModbusConfig_t。

应用 Stream 的 read/write 分别转调 `aModbusRtuRead/Write`。RTU Read 消费
已经确定边界的帧，Write 经 output.write 提交物理字节并记录发送时序。
`aModbusRtuBindTransport` 保留应用填写的 Stream，只补充 RTU 辅助操作。
当前串口 write 直接提交，因此 output.flush 和协议 stream.flush 均为 NULL。
如果端口使用缓冲输出，这两个 flush 须使用同一回调。

协议和 RTU 分帧实例遵循 AMODBUS 分配开关；串口遵循 ADEV_USART 分配开关。
静态方式使用各自 `_instance.h` 中的显式存储。运行期不申请协议内存。
当前板端依赖固定内核频率，休眠、调试暂停和 ISR 延迟仍需实测。

TCP 使用已经建立连接的 Stream：只设置 transport.stream 的 read/write，
可选 flush，RTU 辅助操作全部留空。连接建立、断线后的重连及资源释放
由应用负责。库保留 RTU/TCP 主从能力，ASCII 暂未实现。

### Stream 提交与发送完成

`write` 可返回部分长度，库在剩余预算内补齐整帧；随后调用可选的 flush，
最后调用 RTU wait_transmit_complete。flush 只提交输出，不清空输入、不代表
最后一个停止位已经发完。nanoMODBUS 的输入 flush 继续适配到 discard_input，
与 aStream.flush 分开。

缓冲流的 write 或 flush 失败可能留下半帧，因此 RTU/TCP 实例都会进入故障状态，
后续操作返回 NOT_READY；应用清理端口输出并重建实例后才能继续。
flush 为 NULL 的 RTU 继续使用原有的帧间隔等待和输入恢复策略。

## 功能和验证范围

当前公开基础功能码 01/02/03/04/05/06/0F/10。
文件记录、设备标识、FC17 读写组合暂未提供接口；对应从站代码通过官方宏裁剪。
请求结果通过 aStatus_t 返回，协议异常保存在可选 aModbusResult_t 中。
从站已成功发送异常响应时 Process 返回 OK；业务拒绝可在 result 中观察。
不存在/禁止访问地址对应 02，数值不合法对应 03，内部/锁失败对应 04。
当前官方版本仅接受异常 01..04；远端其他异常作为无效响应处理。

配置位于 `config/aclass_config.cmake`：
AMODBUS_ENABLE、AMODBUS_STATIC_ENABLE、AMODBUS_DYNAMIC_ENABLE、
AMODBUS_CLIENT_ENABLE、AMODBUS_SERVER_ENABLE。
至少开启一种角色和一种分配接口；启用协议必须显式开启 ABUS_ENABLE。
当前产品通过 APP_MODBUS_DEMO_ENABLE 接入 USART2 / RS485 及通信任务；
主从角色和站号在 app/app_config.h 选择，应用分别提供主站和从站实现。
模块本身不创建任务，板级配置和限制见 [产品协议](../../app/protocol/README.md)。

```sh
SANITIZE=1 python3 tests/modbus/run.py
python3 tests/modbus/build_matrix.py
cmake -P tests/config/test_resolver.cmake
```

主机测试使用真实 nanoMODBUS 与 aBus，覆盖 RTU/TCP、主从站、两级回调、
多设备表、范围拒绝、完整值写入、RAW 补位、跨段位数组、广播、CRC、
最大帧、畸形帧、超时回绕、重入拒绝及角色/分配/锁粒度裁剪。
板级接收端口已接入；RTU 时序和物理 RS485 收发仍需上板验证。

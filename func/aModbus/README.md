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
| `aModbusClientReadBatch` | 连续采集项合并为一帧，同一 SIG 一次发布 |
| `aModbusClientReadSig` | 远端 → 解码 → 本地 aBus SIG/Param |
| `aModbusClientWriteSig` | 本地 aBus SIG/Param → 编码 → 远端 |

普通读写请求内的 `access` 选择区域、零起始地址、数量、缓冲区和总超时。
寄存器数据为正确对齐的本地 `uint16_t[]`，位数据为低位在前的紧凑位数组。
例如 10 个线圈需要 2 字节；寄存器数组不是已经编码的大端字节数组。
失败时输出可能已部分改写，只有返回 OK 时才消费输出。

SIG 请求的本地目标通过 `deviceID + sigIndex + paramIndex` 定位。
这两个单次 SIG 接口按 aBus 的类型/长度确定数量；批量采集项则显式填写
quantity，规则见下文。主站的远端地址不使用从站地址段表，
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
request.target.sigIndex = IDU_SIG_TEST_001;
request.timeout = A_TIMEOUT_MS(500U);
status = aModbusClientReadSig(client, &request);
```

读取失败不会发布本地数据，也不会自动清零旧数据。
通信健康状态、采样有效性和最后采样时间由业务另建 SIG 表达。

单次 SIG 请求用 aModbusClientSigCheck 做启动前检查；采集项使用
aModbusClientPollCheck，检查 flags、目标、类型、地址、长度、站号与工作区。
aModbusPoll_t 描述区域、地址、显式长度 quantity、目标、字序和断帧标志，
不保存逐项超时。quantity 是寄存器数或位数，必须非零，不自动推导或补全。
寄存器长度必须等于 aBus 目标字节数向上除以 2，位区域 U8 目标必须为 1；
不匹配返回 INVALID_PARAM，当前映射不提供隐式截断或类型扩展。
aModbusClientBatchRequest_t 为整帧提供 unit_id、timeout、result。

aModbusClientReadBatch 从首项合并同区域的连续地址，不排序、不跨空洞，
按已校验的 quantity 累计长度，与本地 sigIndex 是否连续无关。
不同类型、字序可以同帧读取，随后分别解码；组合 SIG 可按字段或整组配置。
整组作为一个采集项参与合并，库不拆分单项；需要分帧时改用字段采集项。
达到协议容量（125 个寄存器或 2000 位）或下一项要求断帧时停止合并。
每次调用只发送一帧，此即时批量入口不保存预分组表。固定轮询使用下述采集计划。

flags 默认零，AMODBUS_POLL_FLAG_BREAK_BEFORE 表示从该项另起一帧：
连续的 A、B、C 中给 B 设置标志，将读取 A 和 B+C 两帧。
标志在当前批次首项上不产生空帧，连续设置标志可实现逐项读取。
同一 SIG 的字段被拆入多帧时分别发布，不能再依赖整组采样的一致性。
未定义标志会在检查时被拒绝。

completed 返回本次尝试的项数；通信失败
也返回该数量，供调度器推进；实例重入 BUSY 时为零。每个 SIG 独立提交，
批次内解码/范围检查先于提交。自定义回调失败和跨 SIG 锁失败不回滚前项。
设备服务配置用 request_timeout 统一控制每帧超时，站号使用 modbus.unit_id。
采集周期和执行进度由 aPoll 或其他调用者维护，不属于协议配置。
单次主动读写接口仍在请求中指定 timeout，不与常驻采集项混用。

## 固定采集计划

`aModbusPlanCreate/InitStatic` 接收协议实例和 `aModbusPlanConfig_t`，检查清单并
预先合并连续地址，尊重长度上限及 BREAK_BEFORE。RTU 请求缓存完整 8 字节
报文（含 CRC）；TCP 复用分组，仍由 nanoMODBUS 在执行时更新事务编号及 MBAP。
`aModbusPlanFrameCount` 返回帧数，`aModbusPlanExecute` 按帧下标执行一次。

计划绑定原协议实例，共用实例占用保护、收发预算、端口恢复及 aBus 发布。
不保存周期或执行游标，不依赖 aPoll。上游 nanoMODBUS 未修改；预生成 RTU
读取入口在自有代码内校验站号、功能码、字节数、CRC 及异常，即时请求保留
上游路径。两条路径使用同一 transport，不能由不同任务并发访问物理串口。

PlanRequest 的 attempted 在进入前清零。取得实例并尝试事务后置位；通信
超时或发布 BUSY 都保留置位，供调度器推进。入口重入 BUSY 时保持未尝试。
每帧预算来自计划配置 timeout，仍涵盖通信及发布。结果 exception 语义与
普通读写一致，支持的远端异常码为 1～4。

动态计划按实际帧数一次分配。静态计划提供零初始化实例及 frames/capacity，
帧类型见 aModbus_instance.h；容量可以按采集项数提供上限。配置对象复制，
清单、目标定义及协议实例保持有效且不变。计划存在时协议销毁返回 BUSY，
须先停止执行和排队请求，再销毁计划及协议实例。运行中不申请内存。

## 从设备与地址段

应用循环调用 `aModbusServerProcess(handle, request)`。
没有输入时，本次等待结束返回 OK；这不表示一定处理了一条请求。
从站使用两张平级只读表，分别按 `area、address` 排序。
区域分别是 COILS、DISCRETE_INPUTS、HOLDING_REGISTERS、INPUT_REGISTERS。
输入区域只允许读；保持寄存器/线圈可进一步配置为只读。

在 aModbusConfig_t 中分别配置：

1. `ranges + range_count`：地址段回调，每段提供 `read/write + context`。
2. `maps + map_count`：离散 SIG 映射，每项直接携带 area，无需外围地址段。

先查回调段，未命中才查离散映射。命中回调段后，权限不足或回调报错直接返回，
包括 NOT_FOUND；不会继续尝试映射。纯回调、纯映射和两者并存均支持；从站
至少提供其中一种。未配置的表使用 NULL/0，指针和数量必须一致。

同一区域内，段与段、映射与映射、段与映射均不能重叠；不同区域可复用地址。
初始化检查的是完整寄存器/位区间，包括 U32 的两个寄存器，不能只比较起点。
两类配置的交叉重叠通过顺序遍历检查，不增加与地址空间大小相关的索引 RAM。

一个请求允许跨相邻回调段和离散映射，模块先检查全部覆盖和权限。
任意空洞都返回非法地址，不跳过或补零，预检失败不调用业务回调。
回调得到实际负责的部分：address 保留绝对地址，quantity/size/data 调整到该部分，
timeout 是事务剩余时间。跨段位请求会重新排列紧凑位数组，回调的第一个地址
对应 data 的 bit 0。回调自行校验业务值，必须完整处理后再返回 OK。
跨回调/SIG 不提供整体回滚；后面的执行失败可能保留前面的修改。

## SIG 映射

`aModbusBusMap_t` 保存协议区域、起始地址、显式 quantity、权限、目标和字序。
quantity 的单位是寄存器或位，必须非零，不能省略后自动推导。
本地类型、字节大小和上下限读取 aBus 元信息；初始化检查协议数量与
目标编码长度匹配，不匹配返回 A_STATUS_INVALID_PARAM。
因此修改本地类型后，须同时核对协议配置，不会静默改变对外地址范围。
定位使用 sigIndex，sigKey 继续作为业务稳定标识；两者都不是寄存器地址。
映射按区域、地址排序、禁止覆盖；查找回调段和离散映射均使用二分查找。
初始化同时检查显式区间的边界与重叠，运行时路由直接使用已校验的 quantity。
映射表和 aBus 目标定义在协议实例使用期间不得修改。

| aBus 类型 | 占用和编码 |
| --- | --- |
| U8 | 一寄存器，低 8 位；写入超过 255 拒绝 |
| U16 | 一寄存器 |
| U32/S32 | 两寄存器，可选高字在前或低字在前 |
| RAW | 两字节一寄存器，前一字节在高 8 位 |
| STRUCT | 按 Param 映射，或以 WHOLE 映射满足连续布局的整个 SIG |

位区映射仅接受 U8，当前值须为 0/1。
RAW 的奇数长度最后一个低字节补零，写入时非零补位拒绝。
单个映射最大 250 字节；可写映射最多 246 字节，受 FC10 的数量限制。
LOW_FIRST 只影响 U32/S32；整组内 RAW/U16 使用固定编码规则。
独立 RAW/U8/U16 映射仍要求 HIGH_FIRST。
读取一个 32 位映射时先取得完整值，再拆字，允许请求只读取其中一个寄存器。
标量/RAW 写入必须完整覆盖该映射；STRUCT 可写完整字段组成的子区间。
禁止先写高字再写低字，也不提供拼接缓存。
请求数量无需等于某一条映射的 quantity，可读取部分映射或跨多个映射；
访问空洞和半值写入返回非法地址，完整覆盖检查在执行任何写入前完成。

直接映射不提供长度不同的隐式转换。例如协议 U16、本地 U32，应改用
地址段回调：段 quantity 配置为 1，读取时检查 U32 能否放入 U16，写入时
将收到的 U16 转换为 U32 后交给 aBus。缩放、溢出等规则由回调明确处理，
不能通过放宽映射长度校验来实现；sig_read/sig_write 仍使用本地业务类型。

`paramIndex = AMODBUS_SIG_WHOLE` 表示整个 SIG；0 表示第一个字段。
使用静态指定成员初始化时必须显式填写这个选择，不能省略后依赖清零。

### STRUCT 整组映射

整组使用 `target.paramIndex = AMODBUS_SIG_WHOLE`，主站 poll 和从站 map
共用同一套布局与编解码规则。每个字段的寄存器地址为
`map.address + param.offset / 2`，显式 `quantity` 必须等于 `sig.size / 2`。

严格整组要求 params 非空，按内存偏移顺序从零开始连续覆盖整个 SIG；
每项长度非零且为偶数，支持 U16、U32、S32 和偶数长度 RAW。
拒绝中间空隙、尾部未描述空间、重复/重叠字段、奇数字段及嵌套 STRUCT。
隐式填充应由应用调整布局或显式保留字段解决；保留字段也登记为 Param。
U8 和奇数 RAW 仍可独立或通过字段映射访问，不满足整组条件时不会自动降级。
单个整组最大 250 字节，可写整组最大 246 字节；不跨帧拆分单项。

从站初始化及主站请求预检查验证定义。运行时借用只读 params，不建立额外
RAM 字段表，不申请内存；使用现有 SIG 快照工作区，只编码请求涉及的字段。
字序转换仍按类型执行，不能直接发送结构体原始内存。
在同一请求内，默认路径对同一 SIG 取得一次快照，或用 aBusUpdateSig 一次提交。
同步依赖 aBus 锁配置或应用外部同步，静态绑定的直接写者也须遵守同一约定。

读取允许任意寄存器子区间，包括半个 U32 或部分 RAW。写入起止必须落在
字段边界，可修改一个或多个完整字段，其他字段保持原值。半字段写返回
非法地址（异常 02），输入范围失败返回非法数据值（异常 03）。
提交前先检查本帧所有映射值；跨 SIG/地址回调或提交阶段的失败不整体回滚。

自定义 sig_read 对整组调用一次，接收完整本地结构体缓冲区。
自定义 sig_write 在完整组写入时调用一次；部分组写入按完整字段调用，
target.paramIndex 为相应字段下标。库不通过先读后写拼接旧值，回调负责同步，
多个字段回调之间的失败不回滚。主站整组采集成功后也按一个 SIG 发布。

aLib 的布局断言和 aBusSigParamSizeCheck 由应用主动选择调用；长度求和
不替代协议布局检查。修改结构体字段偏移会改变整组寄存器布局，即使总长度
没有变化，也应核对上位机协议。字段映射仍适合固定地址、不同权限或稀疏布局。

```c
static const aModbusBusMap_t maps[] = {
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 0U,
        .quantity = 2U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {
            .deviceID = 1U,
            .sigIndex = IDU_SIG_TEST_001,
            .paramIndex = AMODBUS_SIG_WHOLE
        },
        .word_order = AMODBUS_WORD_HIGH_FIRST
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 2U,
        .quantity = 2U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_001,
            .paramIndex = 0U
        }
    }
};

/* 应用装配时借用总线，协议实例不得销毁它。 */
aStatus_t sigModbusServerInit(aBusHandle_t *bus,
                               const aModbusTransport_t *transport,
                               aModbusHandle_t *instance)
{
    aModbusConfig_t config;
    aModbusConfigStructInit(&config);
    config.bus = bus;
    config.transport = *transport;
    config.maps = maps;
    config.map_count = sizeof(maps) / sizeof(maps[0]);
    return aModbusInitStatic(&config, instance);
}
```

此片段为纯离散映射。需要回调时，在 config.ranges/range_count 另行注册
互不重叠的 aModbusAddressRange_t 数组。当前板级实例见
[产品协议](../../app/protocol/README.md)：USART2 PC10/PC11、PA15 DE，
system 通过 protocolInit(bus_handle) 传入总线；protocol 填入 config.bus 后
直接调用 aModbusCreate / aModbusInitStatic。IDU_modbus_slave.c 提供可读写
测试寄存器、只读板卡标识、运行时间三个回调段示例及调试地址。

## 两级回调与并发

地址段回调收到的是协议数据：寄存器为本地 uint16_t 数值，线圈为紧凑位数组。
SIG 回调收到的是本地业务类型字节，长度与 aBus 元信息完全一致。
配置中的 `sig_read/sig_write` 是统一分发入口，携带目标标识供业务选择。
未配置回调时默认调用 aBus 整组或 Param 接口。
自定义回调必须整项完成，不得持有借用缓冲区或重入当前协议实例。
所有回调都在当前调用者任务中执行，不在 ISR 中执行。

默认映射路径按 SIG 分组：一次请求内同一 SIG 的字段从同一完整快照读取，
写入通过 aBusUpdateSig 在同一临界区内编辑、校验并提交，保留其他字段。
同一 SIG 在首次命中处合并处理，回调业务不应依赖逐寄存器的提交顺序。
即使同一 SIG 的多个映射之间夹着其他映射，也不会分别提交它的各个字段。
这一保证要求对应 SIG 启用 aBus 锁，或由应用保证串行访问；直接改绑定变量
不能受总线锁保护。跨 SIG 不提供事务，后续锁失败不回滚先前提交。

实例提供 250 字节工作区，无运行时堆分配。若某个映射字段所在的完整 SIG
更大，配置 snapshot/snapshot_size 提供独立工作区，生命周期覆盖实例，
不得与总线存储或其他实例正在使用的工作区重叠。初始化拒绝容量不足的映射。
自定义 SIG 回调保留逐项调用约定，回调负责自己的数据一致性。

写入先检查所有地址、权限、完整覆盖、编码和各项范围，再按 SIG 提交。
不同 SIG 不保证属于同一采样时刻，整批更新也不是数据库事务。
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
| `aModbusRtuReceiveAt` | 输入字节、错误及实际采集的计数器/毫秒时间 |
| `aModbusRtuRead` / `aModbusRtuWrite` | 供应用薄适配函数转成 Stream 回调 |
| `aModbusRtuBindTransport` | 给应用 Stream 补充帧间隔、发送完成及收尾操作 |
| `aModbusRtuDeInitStatic` / `aModbusRtuDestroy` | 接收和协议调用停止后释放 |

通用接口由 `aModbus_rtu.h` 声明；静态布局见 `aModbus_rtu_instance.h`。
调用者通过 `aModbusRtuIo_t` 提供计数器、同步及线路完成操作，
通过 `aModbusRtuConfig_t.output` 提供物理输出 Stream；其 read 不使用。
消费任务通过 enter/exit 排除生产者；线程生产者必须使用同一同步机制。
计数器须以固定频率运行，支持 32 位回绕；aOS 毫秒时基用于判断长空闲。

接收适配按底层能提供的信息选择：

| 底层输入 | 适配方式 |
| --- | --- |
| 每字节完成后立即回调 | 保持 `aModbusRtuReceive`，内部采样时钟 |
| 每字节带真实采集时间，可延迟交付 | 使用 `aModbusRtuReceiveAt`，复用原分帧逻辑 |
| 已有可靠的完整 RTU 帧边界 | 直接实现 `aModbusTransport_t`，不经过逐字节分帧 |

`ReceiveAt` 的 rx_ticks 和 rx_ms 是字符完成时采集的 io.ticks 与
aOSGetUptimeMs 值，必须与实例当前时钟同源；只提交初始化后采集的数据，
并保持采集顺序。不能使用补送时刻或按波特率推算时间，不能给整批数据
统一填写同一个时间戳。采集错误通过 status 传入，仍丢弃当前整帧。

延迟交付必须在同一次同步保护内补齐全部待交付字节；消费者及发送辅助
操作取得 io 保护时，不能还有尚未补送的采集记录。适配器可在 io.enter
取得保护后先排空已有采集记录，再返回给 RTU；排空不等待新数据。
这是因为静默检查仍使用当前时钟，未交付字节可能导致提前截帧或误判线路空闲。
仅给每个字节单独加锁不能满足这一要求；不能保证时应由底层分帧后适配。

完整帧适配由底层保证帧内时序、边界及完整 ADU 长度（含地址和 CRC）。
stream.read 可分次返回当前帧，但不得跨到下一帧；finish 只释放当前帧。
同时提供 discard_input、prepare_frame 和 wait_transmit_complete，分别
负责恢复边界、发送前静默和线路发送完成；CRC 校验仍由协议核心处理。
这一路径无需创建 aModbusRtu 实例，也不调用 aModbusRtuBindTransport。
Linux 普通串口的一次 read 可能是半帧或多帧，没有上述采集信息或边界保证
就不能直接套用这两种延迟适配方式；Linux 后端仍留到实际迁移时实现。

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
3. 主站先建立采集计划及 aPoll；rs485PortOpen 再绑定 RTU 接收回调并打开 USART。
4. system 先创建总线，再调用 protocolInit(bus_handle)；protocol 装配并创建通信任务。
5. 停止所有调用者后，销毁 aPoll、采集计划和协议，再关闭串口并释放 RTU。
   串口关闭 BUSY 时保留 RTU，后续允许重试关闭。

`aModbus.h` 提供 aModbusServiceConfig_t，统一组合 aModbusConfig_t、
主站采集列表、事务超时及从站处理请求。调度配置由应用组合 aPollJobConfig_t。
该类型只描述配置，库不自动创建任务、不执行轮询调度，也不持有应用任务。
任务名称、栈容量及优先级由应用链路独立配置，不再放在服务配置中。
具体设备的配置实例由 app 定义，库中没有 IDU/FAN 等设备声明。
装配时复制协议模板，填入具体 transport 并绑定私有 aBus；模板及点表保持只读。
静态模板须明确字节等待策略，当前应用为 20 ms；局部配置可调用
aModbusServiceConfigStructInit 获取整组默认值：协议按 aModbusConfigStructInit，
从站处理超时为 100 ms，主站事务超时为 1000 ms。
应用须补齐端口、总线和采集或映射配置，再自行决定任务资源与执行入口。
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

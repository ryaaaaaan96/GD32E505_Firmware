# 产品点表与协议装配

本目录负责产品点表、协议映射、持久化选择和通信任务装配。
`system` 创建并私有持有 aBus 与数据库，恢复参数后调用 `protocolInit(bus)`。
`devices` 只提供串口设备；aModbus 负责纯协议事务；aComm 负责周期、重试、
事件队列、流式收发、静默分帧及数据映射；协议端点负责类型编码，
数据端点负责总线读写。
模块都不创建任务；protocol.c 分别创建并装配协议、映射映射实例和调度器。

| 内容 | 位置 |
| --- | --- |
| IDU、FAN 测试测点唯一清单 | `sig/IDU_test.inc`、`sig/FAN_test.inc` |
| 设备号、自动索引、固定 Key、共享类型 | `inc/IDU_sig_table.h`、`inc/FAN_sig_table.h` |
| 从站地址、权限与通信配置 | `IDU_modbus_slave.c` |
| 主站采集项与通信配置 | `FAN_modbus_master.c` |
| 需要保存的测点 | `persistence/sig_persist.inc` |
| 只读定义、RAM 存储计划、持久化选择 | `protocol_tables.c` |
| 链路实例、端口装配、任务资源及创建 | `protocol.c` |
| 后续 JSON 输入预留 | `config/` |

协议任务仅负责调度：主站通过 PlanGroupCount/Get 注册作业，调用
`aCommPlanExecute` 并传入 request.execution；重试次数在通用作业中配置。
从站注册 serverExecute 回调，调用 `aModbusServerProcess`，同步地址回调进入
aComm 映射。
分组及数据工作项由 aComm 提供，协议编码仍归 aModbus；应用只配置和连接。
aComm 通道通过 aModbusBindChannel 绑定完整传输；
aModbusRtuFrameCheck 作为完整帧检查回调注册。
主从产品配置中的 `timing` 直接给出最终微秒值，protocol.c 原样传入通道，
aComm 在初始化时按端口时基转换为计数；运行期判断间隔并执行等待。

当前 115200、8N1 配置为 `character_us=87`、`byte_gap_us=750`、
`frame_gap_us=1750`。间隔默认宏由 aModbus 提供，产品可以直接改为其他值。
改变波特率或字符格式时，须同步检查主从产品的 timing；
可选用 `aModbusRtuTimingDefaults` 得到新默认值。通道不会自动覆盖用户配置，
`byte_gap_us=0` 明确表示关闭帧内间隔检查，不表示使用默认值。

## 配置与运行资源的边界

- `FAN_modbus_master.c` 描述 **IDU 作为主站访问 FAN**：直接定义远端采集数组，
  配置站号、事务超时、轮询周期及出错后的等待。文件名中的 FAN 是访问对象。
- `IDU_modbus_slave.c` 描述 IDU 对外提供的地址空间、访问权限和处理超时。
  地址空间可以同时包含 IDU 数据、FAN 的本地采集副本及地址段回调示例。
- `protocol_tables.c` 生成 SIG/字段定义、默认值、范围、RAM 绑定和持久化
  选择清单。纯静态模式下，随点表规模变化的 aBus 存储也在这里预留；
  创建总线、持有活动句柄及恢复数据仍由 system 完成。

主站提供 `const protocolClientConfig_t`，分别保存 protocol（纯协议配置）、client（映射配置）与采集节奏；从站提供
`const protocolServerConfig_t`，分别保存 protocol（纯协议配置）、server（映射配置）与处理预算。类型位于
protocol_internal.h，配置不包含任务栈、优先级或运行数组。protocol.c
装配端口、aComm 通道和主从实例；主站另建采集计划，随后注册 aComm 作业并创建任务。
当前从站仅为 TEST_001 登记 MARK_ONLY 关系，不额外订阅变化事件。
本 Demo 主从都使用 aComm 映射与显式端点，任务统一执行
aCommProcess/aCommWait。从站注册一个连续执行的作业，不创建采集计划和临时队列；
ServerProcess 仍为同步事务，等待期间同一消费者不能执行其他作业。
静态资源只在 protocol.c 预留，动态作业数组按实际周期组数申请。
应用通过 submit 回调将请求直接投递到四槽队列，映射实例借用协议与队列。
纯 aModbus 从站也可使用其他地址回调，不要求启用 aComm 或 aBus。

`protocolPersistenceConfigInit` 只向数据库配置填入只读点表和持久化清单。
`system_init.c` 的数据库段持有 KV/TSDB 实例及静态索引 RAM，并结合
`system_device.c` 提供的 aMemory 区域完成装配。点表文件不包含 FlashDB
私有布局或 Flash 分区配置。公开数量 `PROTOCOL_PERSIST_SIG_COUNT` 由同一
持久化清单在编译期推导，system 不需要知道具体的 IDU/FAN 测点。

## 100 个 TEST 测点

旧 Counter、Motor 测点及其 Key 已移除，不保留兼容入口。
当前使用 TEST 名称，不代表真实子板的业务寄存器或控制指令。

两张表各 50 个 SIG，编号均为 TEST_001～050，Key 为 2001～2050。

| 测点 | IDU（deviceID=1） | FAN（deviceID=2） |
| --- | --- | --- |
| TEST_001 | U32，任务静态绑定，每秒加 1，默认 0 | STRUCT：U32 默认 100、范围 0～6000；S32 默认 25 |
| TEST_002 | U8，0～200，默认 2 | U8，0～1，默认 0 |
| TEST_003 | U16，0～50000，默认 3 | U32，0～6000，默认 1500 |
| TEST_004 | U32，0～10000，默认 4 | U16，0～3，默认 0 |
| TEST_005 | S32，-1000～1000，默认 -5 | S32，-1000～1000，默认 -5 |
| TEST_006 | RAW，8 字节，默认 01～08 | RAW，8 字节，默认 01～08 |
| TEST_007 | STRUCT：U32、S32、U16、RAW[4]、U16 保留字段 | STRUCT：U32、S32 |
| TEST_008～050 | U32，0～10000，默认值等于 TEST 编号 | U32，0～10000，默认值等于 TEST 编号 |

两表分别覆盖 U8、U16、U32、S32、RAW、STRUCT。
IDU TEST_007 默认 `{100, 25, 1, {AA, BB, CC, 00}, 0}`，
前三个字段范围分别为 0～6000、-1000～1000、0～3，保留字段仅允许 0。
FAN TEST_007 默认 `{100, 25}`，范围为 0～6000、-1000～1000。

合计 100 个 SIG；组合类型字段不单独计为 SIG。每张表的 sigIndex 从零自增，
对外稳定定位使用 deviceID + sigKey；两张表可以使用相同 Key。
`PROTOCOL_BUS_INSTANCE_ID` 同时用于挂载与 RAM 绑定。

每个设备只有一份 `test` 清单。`ABUS_SIG` 定义普通测点；
`ABUS_BOUND_SIG` 定义已有外部 RAM 绑定的测点，两者生成相同的 aBusSig_t。
两种宏都携带 C 存储类型，同源生成 size 和静态 RAM，避免长度手工计算。
后者只让产品的静态存储展开跳过已绑定项，不新增 aBus 公共类型或运行状态。
IDU TEST_001 在 test_task.c 绑定，FAN TEST_001 在 protocol_tables.c 绑定。
静态模式从同一清单为其余项按 C 类型生成 RAM；动态模式由 aBus 一次分配未绑定数据池。
直接访问绑定变量的同步仍由应用负责，测试任务的自增不与总线接口互斥。

构建调用 `scripts/check_sig.py` 检查每张表的名称、16 位 Key 唯一性。
清单 Key 使用十进制或十六进制字面量，不使用表达式或前导零八进制。
这项检查不占固件 RAM；类型、偏移、范围等继续由 aBus 定义检查负责。
未来 JSON 生成器尚未实现。

## Modbus 测试布局

SIG 保留 X-Macro 清单，用同一份定义生成索引、Key、描述及静态存储。
协议映射直接使用普通 C 数组，不再维护 mapping 目录或映射展开宏：

- `FAN_modbus_master.c` 的 `client_maps[]` 使用 `aCommMappingClientMap_t`，
  定义区域、地址、长度、目标、字序和断帧标志；临时访问与自动采集共用此表。
  同周期项保持数组顺序，`client_map_count` 通过 `sizeof` 推导。
  站号、事务超时在 client 配置，retry_count 在产品采集配置；周期逐项填写。
- `IDU_modbus_slave.c` 的 `maps[]` 使用 `aCommMappingServerMap_t`，
  定义区域、地址、长度、权限、目标和字序；
  `ranges[]` 单独定义地址段回调，两者在 aCommMappingServerConfig_t 中平级配置。
- 两张表分别按区域、地址排序。同一区域内，段与段、映射与映射、段与映射
  均不能重叠；离散映射允许空洞，不需要外围段。数量通过数组 `sizeof` 推导。
- 段和映射的 `quantity` 都是寄存器数或位数，必须显式填写。
  不能用业务字节数或映射项数代替协议数量。
  回调段和映射各自声明权限，命中段后回调失败不再尝试离散映射。

目标类型、字节大小和范围仍从 aBus 查询，目标引用 SIG 清单生成的枚举。
主站采集项和从站离散映射均显式配置 quantity，并检查它与目标长度匹配。
修改字段顺序不会改变映射指向的业务成员或协议地址；初始化继续检查映射合法性。
未来 JSON 生成器可以生成 SIG 清单和普通 C 通信配置，当前手工维护。

USART2 PC10/PC11、PA15 DE，115200、8N1、站号 1。
寄存器地址均从 0 开始，U32/S32 用两个保持寄存器，高字在前。

| 地址 | 目标 | 协议权限 |
| --- | --- | --- |
| 0～1 | IDU TEST_001 | 读写 |
| 2～3、4～5 | FAN TEST_001 的 U32、S32 字段 | 只读 |
| 32、34～35、36 | FAN TEST_002、003、004 | 读写 |
| 38～39、40～43、44～47 | FAN TEST_005、006、007 | 读写 |
| 64～149 | FAN TEST_008～050，每项两个寄存器 | 读写 |
| 256、257、258～259、260～261 | IDU TEST_002、003、004、005 | 读写 |
| 262～265 | IDU TEST_006，RAW[8] | 读写 |
| 266～273 | IDU TEST_007：U32、S32、U16、RAW[4]、保留字段 | 读写 |
| 288～373 | IDU TEST_008～050，每项两个寄存器 | 读写 |

写 U32/S32 必须完整覆盖两个寄存器。地址空洞返回非法地址；越界值拒绝写入。
从站全部 100 条映射（每个 SIG 一条）已填写 quantity；零长度或长度与目标不匹配时，
初始化返回 INVALID_PARAM，应用不会打开串口。U32 的任意一字可单独读取，
但任意一字单独写入都会拒绝；RAW 同样允许部分读取、要求完整写入。
请求可跨相邻映射，不要求请求数量恰好等于某一条映射的长度。
一次 FC10 最多写 123 个寄存器，所以连续 U32 应每批最多 61 项。
当前测试值只改变本地 RAM，不会自动转发为真实风扇控制。

### 组合 SIG 整组映射

FAN TEST_001、TEST_007 各为 8 字节，通过一条 WHOLE 映射覆盖 4 个寄存器。
IDU TEST_007 为 16 字节，通过一条 WHOLE 映射覆盖地址 266～273：

| 地址 | 字段 | 类型 |
| --- | --- | --- |
| 266～267 | u32_value | U32 |
| 268～269 | s32_value | S32 |
| 270 | u16_value | U16 |
| 271～272 | raw_value | RAW[4] |
| 273 | reserved | U16，仅允许 0 |

读取可以截取任意寄存器。例如读取 267、数量 2 返回 U32 低字和 S32 高字；
写入允许一个或多个完整字段，例如 268、数量 3 修改 S32 和 U16。
只写 267、数量 1 或 271、数量 1 会因字段不完整返回异常 02。
字段范围失败返回异常 03，同一 SIG 不提交半成品；默认路径同步遵循 aBus 锁。

C 结构体头使用 aLib 断言固定布局；protocolBusConfigInit 返回 aStatus_t，
在发布配置前主动检查 STRUCT 的字段长度总和，system 检查成功后才创建总线。
完整连续性和业务映射长度由 aComm 映射检查，用户可选检查不替代协议校验。

本次 IDU TEST_007 从 12 字节改为 16 字节：U8 字段改为 U16，RAW[3] 改为
RAW[4]，增加显式保留字段。旧版本保存的 deviceID=1、sigKey=2007 记录会因
长度不匹配在应用恢复流程中被逻辑删除；启动保留该 SIG 默认值并继续恢复其他项。
下次显式保存写入 16 字节的新记录，不需要升级前手工删除或格式化分区。
若要求保留旧值，应在启用这一作废策略前提供迁移；同长度的已描述布局变化
由数据库初始化时的布局标记识别。
其余独立 U8/RAW 测点维持原有编码规则。

### 主站采集 30 个 FAN 测点

FAN_modbus_master.c 配置全部 50 项，TEST_001～030 周期为 1000 ms，
TEST_031～050 周期为零，仅供临时访问。
两个组合 SIG 按整组配置，自动轮询部分合计 30 个 SIG、30 个采集项，覆盖 U8、U16、
U32、S32、RAW 和 STRUCT。同一帧中的同一 SIG 字段一次提交到 aBus。

Demo 的远端地址沿用上述从站 FAN 映射，便于两块 IDU 固件切换主从联调；
这不是实际 FAN 产品的寄存器规范。接真实子板须调整地址、类型和字序。
远端地址不能从本地 sigIndex 推导；本地索引连续不代表远端地址连续。

应用选择采集点及其地址，aComm 映射初始化时按周期分组，合并
同区域的连续项。组内保留原表顺序，不跨空洞；长度由 quantity 显式配置。
达到读取上限 125 个寄存器或 2000 位时结束本帧，后续调用继续剩余项。
不同类型和字序仍可同帧读取，再按各自配置解码。

quantity 表示寄存器数或位数，不是字节数；禁止省略或填写零。
U8/U16 寄存器目标填 1，U32/S32 填 2，RAW[8] 填 4；
组合类型按所选字段填写，位区域的 U8 目标填 1。
初始化在打开串口前检查配置长度与 aBus 目标匹配；固定计划复用校验结果，
即时事务仍检查当前请求，
不匹配返回 INVALID_PARAM，不会自动修改长度或隐式截断/扩展数据。
修改 aBus 类型后需重新核对远端协议，自动分组使用已校验的 quantity。

本 Demo 每个采集项均显式填写 flags，自动合并时写 `flags = 0U`。
需要人为分包时，在新包首项填写：

```c
/* TEST_020 从地址 88 开始；即使紧接上一项，也另起一帧。 */
.flags = ACOMM_MAP_FLAG_BREAK_BEFORE,
```

标志表示“在当前项之前断帧”，不会创建空帧；当前项作为下一帧首项时
正常读取，并继续合并后续连续项。库会拒绝未定义的 flags 位。
拆开同一组合 SIG 的字段后，各帧分别发布，不保证跨帧快照一致。

Demo 在 TEST_020 上设置断帧标志，每轮发送 6 帧 FC03，地址为零基地址：

| 帧 | 起始地址 | 寄存器数量 | 覆盖测点 |
| --- | --- | --- | --- |
| 1 | 2 | 4 | TEST_001 的 U32、S32 |
| 2 | 32 | 1 | TEST_002 |
| 3 | 34 | 3 | TEST_003～004 |
| 4 | 38 | 10 | TEST_005～007，含 RAW 与组合字段 |
| 5 | 64 | 24 | TEST_008～019 |
| 6 | 88 | 22 | TEST_020～030 |

将 TEST_020 的 flags 改为 0U，最后两帧自动合并为地址 64 起 46 个寄存器，整轮
变为 5 帧。地址 33、37 和各段间空洞始终不读取。
真实设备若要求更小的包或特殊边界，可在相应采集项上设置断帧标志。

配置三个不同用途的时间：

- `request_timeout`：每帧事务超时，当前 500 ms，包含通信及发布等待。
- `poll_time_ms`：逐项配置，同周期项分组，整轮起点最小间隔当前 1000 ms。
- `interval_ms / error_delay_ms`：两次事务之间的让出时间，成功 1 ms、
  失败至少 5 ms；它们不是逐点采集周期。

站号使用 client.unit_id；各采集项不重复配置站号和超时。
当前 retry_count=2，每帧超时最多尝试三次；六帧全部超时约 9 s 加帧间等待，
不能保证 1 s 完成一轮。次数耗尽推进其他帧，下轮重置；通信失败保留旧值。
每个 SIG 独立发布，整轮不提供原子快照。
主站模式下，本地 Shell 改写这些采集值会被后续成功采集覆盖，不代表已下发。

### 周期、事件与临时请求

FAN 固定采集计划在初始化时生成，缓存六帧 RTU 请求及 CRC。aComm 的一步
对应一帧，protocol 不再维护 poll_index、轮次时间和周期等待计算。
计划只保存启用映射的下标，直接引用 FAN 的只读点表，不在 RAM 复制映射。
从站没有周期计划，继续处理接收到的请求。板级串口职责保持不变。

主站 Shell 命令：

```text
protocol refresh
```

protocolRefresh 非阻塞设置各组的触发标记，请求提前采集。当前轮次不会被打断，成功/失败后的等待及 RTU 静默仍然生效。
重复投递合并，从站返回 UNSUPPORTED。该命令不会直接从 Shell 操作串口。

带参数的临时读写调用 protocolModbusSubmit。使用
protocolModbusRequestStructInit 初始化，填写 access.read；写操作设置 write，
并用 aModbusClientWriteRequestStructInit 初始化 access.write 后填充参数。
四槽队列复制调度描述，原请求及数据借用至 complete 回调结束，不能提交局部
对象后立即返回。complete 在通信任务调用，response 仅回调期间有效。
access.result 不使用，状态和协议异常统一通过 complete 返回。
临时请求在帧之间执行，不合并、不自动重试；队列满返回 BUSY。

调度采用周期作业、事件、临时请求轮转，持续的外部请求不会饿死周期采集。
当前同步收发可等待 500 ms，事件只能唤醒空闲任务，不能打断在途事务。
独立物理端口应使用独立执行任务，避免一个端口超时拖住其他端口。

### 地址段回调 Demo

以下三个段在 IDU_modbus_slave.c 的 ranges 数组中注册，与原有 SIG 映射并存。
地址均为从零开始的十进制地址；调试工具若使用 4xxxx/3xxxx 编号，需确认偏移。

| 区域/功能码 | 地址 | 内容 | 操作 |
| --- | --- | --- | --- |
| 保持寄存器，03/06/16 | 512～515 | 四个测试值，初始 10、20、30、40 | 读写，单值 0～10000 |
| 保持寄存器，03 | 520～521 | 板卡标识 0x4944、0x5500，即 ASCII IDU | 只读 |
| 输入寄存器，04 | 512～513 | 32 位运行时间，单位 ms，高字在前 | 只读，按 uint32_t 回绕 |

调试时可依次执行：03 读 512 起 4 个，06 写 513 为 123，再读回；
16 写 514 起 2 个为 33、44；03 读 520 起 2 个；04 读 512 起 2 个。
写入 10001 返回非法数据值，单次回调内先检查全部值再提交。
写入 520 返回非法地址；读取 514 起 8 个因跨过 516～519 空洞而拒绝。

回调在 Modbus 任务中同步执行，context 指向本文件私有的数据块；收到的地址
仍是原始协议地址，通过减去段起点得到偏移。寄存器 data 为本机 uint16_t 数组，
协议字节序和 CRC 由库负责。运行时间每次回调只取一次快照，也允许读取其中一字。
示例数据不注册到 aBus，不计入两表各 50 个 SIG，不参与 sig 命令或持久化；
可写值重启后恢复初值，由唯一协议任务访问。实际产品回调可自行访问 aBus 或设备。

### 协议装配

`app_config.h` 的 `APP_MODBUS_MASTER_ENABLE` 仍选择当前唯一物理链路的角色。
从站使用上述映射；主站按上节配置采集 30 个 FAN SIG。
增减轮询项直接修改 FAN_modbus_master.c 的 client_maps 数组，初始化先校验全部目标，
失败时不打开串口。同一区域内地址连续且未要求断帧的项由 aComm 映射合并为一次请求。
整轮最小周期 1000 ms，帧间让出 1 ms，失败让出至少 5 ms；可恢复读取失败
额外重试两次，耗尽后推进列表。
若通信耗时已超周期，不额外等待一整秒。

protocol.c 按链路保存独立实例和轮询状态。后续多端口可注册多个链路，
当前仅装配一条 USART2，不同时启动主从。aStream.context 区分端口实例，
帧流由 aComm 通道提供，经 aModbusBindChannel 转交给协议栈；
新增链路注册自己的 context，复用同一组收发及协议检查回调。
重复 protocolInit 返回 BUSY，不提供运行期重启。

## 持久化与启动

`persistence/sig_persist.inc` 按 deviceID、sigKey 排序，选择 12 项：
IDU TEST_001～009，以及 FAN TEST_002～004。只有这些项占用 SIG 偏移索引 RAM。
运行时按 sigIndex 访问数据库，Flash 键由 deviceID + sigKey 确定；清单重排
不会改变已有记录的身份。每项另有一个 FlashDB 布局标记，数据库初始化时
根据类型、大小和字段 offset/type/size 计算、比较；不匹配先作废旧数据，
再更新标记。正常读写仍保存完整原始数据，不计算或读写标记。
首次升级时没有标记的选定项会被作废，启动保留默认值。含义变化或未描述字段
变化须由产品显式作废旧值或更换 sigKey；需要保留旧值的升级须另行迁移。

启动顺序：Shell/存储/日志 → aBus 默认值 → 打开数据库并恢复选定项 →
协议实例和通信任务 → 放行 Shell → main 创建测试任务。
protocolBusConfigInit 检查产品定义后返回配置，不重置已运行的静态实例。

- 空白介质启动不自动格式化；执行 `db init` 后可以保存参数。
- `sig set`、Modbus 写入和每秒自增只改 RAM；显式 `sig save` 才保存到 Flash。
- 数据库初始化时作废布局不兼容记录；启动恢复时缺失记录保留默认值，
  长度不符仍沿用应用逻辑删除流程。
- 普通 Get 不删除记录；范围错误、介质读取或删除失败仍返回错误。
- `sig load` 可在调试时重新恢复，未列入持久化清单的键返回 NOT_FOUND。
- 单项 `sig load` 作废旧记录后也返回 NOT_FOUND，保留当前 RAM，不自动写回默认值。
- 保存先取快照再写 Flash，不跨 Flash I/O 持有 aBus 锁。
- 当前应用工作缓冲区为 256 字节，较大 SIG 返回 NO_MEMORY；库本身不受此上限约束。
- 每个 SIG 独立保存/恢复，整张表不是事务；中途失败不回滚之前成功的项。
- 保存/恢复流程由应用串行化；启动恢复在生产者启动前，调试命令由 Shell 串行执行。

```text
sig get 1                   # 显示 IDU 所有测点、类型、flags 和范围
sig get 2 2001              # 显示组合类型的所有字段
sig get 2 2001 0            # 读取 U32 字段
sig set 2 2003 3000         # 修改可写测试参数
sig set 1 2002 123          # U8
sig set 1 2003 12345        # U16
sig set 1 2005 -123         # S32
sig set 1 2006 1122334455667788  # RAW，必须完整 8 字节
sig set 1 2007 3 11223344   # STRUCT 的 RAW[4] 字段
sig get 1 2007

db init                     # 首次使用空白介质时显式初始化
sig save                    # 保存全部 12 个选定项
sig save 2 2003             # 只保存一个选定项
sig set 2 2003 2000
sig load 2 2003             # 恢复到先前保存的 3000
sig get 2 2003
sig load                    # 恢复全部已有记录
```

Shell 是本地调试入口，可写组合字段；Modbus 的只读权限属于协议映射，
不会限制本地调试。关闭 aBus、数据库或协议时，对应入口按依赖裁剪。

## 配置对象的 extern 约定

通信配置采用 `extern const` 跨文件共享，声明集中在
[protocol.c](protocol.c)，只由它装配；公共头文件不声明这些对象。
通信组合类型由私有 protocol_internal.h 提供，对象分别定义在 FAN_modbus_master.c、
IDU_modbus_slave.c；protocol.c 在 APP_MODBUS_ENABLE 内按主从角色声明：

```c
#if APP_MODBUS_MASTER_ENABLE
extern const protocolClientConfig_t FAN_modbus_master_config;
#else
extern const protocolServerConfig_t IDU_modbus_slave_config;
#endif
```

定义方和使用方均包含私有组合类型，但公共 protocol.h 不暴露配置对象。
修改对象名称、类型或 const 限定时，必须同步检查定义和 protocol.c 的声明；
普通链接器不能保证发现跨文件的 C 类型不一致。
protocol.c 根据主从宏引用所选对象，再复制配置并装配运行资源。
声明本身不分配对象存储，也不复制配置；每个参与构建的配置对象只有一份定义。
当前配置和引用的映射、采集数组均具有静态存储期，不随函数返回失效。
多个任务仅仅读取这些只读对象，不需要为配置读取额外加锁。

点表不使用 extern：protocol_tables.c 在 ABUS_ENABLE 内展开两份清单，定义
连续的 static const aBusTable_t 数组，通过 protocolBusConfigInit 交给 system 挂载。
初始化不再复制表描述，不需要逐设备的初始化函数。

system 通过 protocol.h 取得点表配置并向 protocolInit 传入总线；业务模块通过设备头文件取得
设备号、测点标识和数据类型，不会间接取得配置对象声明或协议库依赖。
通信配置对象仍具有外部链接；将声明放入 .c 不等于编译器强制的私有访问控制。
约定只有 protocol.c 引用这些对象，其他业务通过数据总线服务访问运行数据。
当前不为这两个通信配置对象新增内部头文件，也不在多个使用方重复维护声明。

| 风险 | 使用要求 |
| --- | --- |
| 声明与定义类型不一致 | 双方使用库提供的统一类型；变更时同步核对对象名称、类型和限定符，普通链接器不能完整检查 C 类型 |
| 条件宏或对齐选项导致结构布局不同 | 保持相关编译单元的配置宏、结构体布局和 ABI 选项一致 |
| 缺少定义、重复定义或引用了被裁剪的角色 | 每个对象仅在一个 .c 中定义；引用与主从编译条件一致，并检查最终链接 |
| 将只读配置当作运行状态修改 | 保持 const，不强制去除限定；运行状态由独立实例持有 |
| 指针目标失效或存在并发修改 | const 不递归保护指针目标；result、context 等可写目标单独约定生命周期及同步 |
| 设备配置依赖扩散 | 引用集中在 protocol.c；公共协议库不引用具体设备对象 |

只读配置不需要为消除 extern 再增加 getter 或运行时注册。未来 JSON
生成器应统一维护设备 .c 的配置定义和 protocol.c 的配套声明，并校验
名称、类型、符号唯一性及主从裁剪关系。当前尚未实现生成工具，声明一致性
依靠源码同步维护；构建与回归测试验证链接和实际功能，不替代完整类型检查。

开源项目也使用相同的跨文件声明机制，具体对象的访问约束仍需分别遵守：

- [Zephyr device.h](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/device.h)
  通过宏为设备树节点生成设备对象的 extern 声明，非可变设备带 const。
- [FreeRTOS heap_4.c](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/portable/MemMang/heap_4.c)
  在 configAPPLICATION_ALLOCATED_HEAP 启用时，通过 extern 引用应用定义的 ucHeap。
- [Linux jiffies.h](https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h)
  声明 jiffies 与 jiffies_64；可写计时状态仍有独立的读取和同步要求。
- 仓库中的 [nr_micro_shell.h](../../func/aShell/nr_micro_shell/inc/nr_micro_shell.h)
  通过 extern 引用 cmd_table 和 cmd_table_size。

### 按 SIG 的异步测试

主站且 ABUS_EVENT_ENABLE 开启时，test 任务每 5 秒调用 protocolRequest，
目标仅填写 FAN_SIG_DEVICE_ID、FAN_SIG_TEST_003、ABUS_SIG_WHOLE。
请求值 101～200 循环，后台从 target 的订阅映射取得地址 34、数量 2、字序和超时。
原有显式地址 protocolModbusSubmit 仍可用于未登记的临时操作。

任务持有静态请求、写值和一项凭据数组；Done 后才复用，完成回调只记结果。
当前仅有一名接收者，若增加重叠订阅，需要同步增加凭据容量并汇总各自结果。
收到写应答不直接覆盖本地采集副本，下一轮读取确认；`sig set` 仍只改本地值。
未轮询项也可用相同方式 READ/WRITE。TEST_001 为协议只读，其余测试项读写。

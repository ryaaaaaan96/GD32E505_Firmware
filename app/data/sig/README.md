# 应用 SIG 模块

本模块维护应用点表，通过私有 aBus handle 提供通用读写接口。
Shell 根据点表查询类型、长度和字段描述，不单独维护测点类型表。

## 标识与层次

```text
handle
└─ deviceID：选择设备的点表
   └─ sigIndex：选择表内的 SIG
      └─ paramIndex：选择 STRUCT 内已登记的字段
```

| 标识 | 含义 | 当前示例 |
| --- | --- | --- |
| deviceID | 设备号，同一 handle 内唯一 | 1：当前应用设备 |
| sigIndex | SIG 在设备表内的下标，从 0 开始 | 0：Motor；1：Counter |
| paramIndex | 字段在 SIG 的 params 数组内的下标，从 0 开始 | Motor 的 0：speed；1：temperature |
| sigKey | 表内唯一、跨版本保持稳定的业务键 | Motor：1001；Counter：42 |

当前 Shell 使用 sigIndex，不使用 sigKey。参数下标由描述数组顺序决定，
不是字段的字节偏移；真正的内存位置由描述中的 offset 指定。

## 当前测点

设备号为 `APP_SIG_DEVICE_ID = 1`，标识定义见 [sig_ids.h](sig_ids.h)。

| sigIndex | 测点 | 类型 | 初始内容 |
| --- | --- | --- | --- |
| 0 | Motor | STRUCT | speed = 100，temperature = 25 |
| 1 | Counter | U32 | 0；测试任务每秒直接加 1 |

Motor 的字段描述：

| paramIndex | 字段 | 类型 | 业务范围 |
| --- | --- | --- | --- |
| 0 | speed | U32 | 0～6000 |
| 1 | temperature | S32 | 无额外上下限，仍受 S32 表示范围限制 |

Counter 没有额外业务范围，允许 0～4294967295；测试任务递增到最大值后回绕。

## Shell 命令

```text
sig get <deviceID> [sigIndex [paramIndex]]
sig set <deviceID> <sigIndex> [paramIndex] <value>
```

尖括号表示必填参数，方括号表示可选参数，实际输入不包含括号。
对于 STRUCT，set 必须填写 paramIndex；对于标量或 RAW SIG，不填写 paramIndex。

例如：

```text
sig set 1        0        0          200
        deviceID sigIndex paramIndex value
```

这条命令将设备 1 的 Motor.speed 设置为 200。

可直接输入的命令：

```text
sig get 1 0
sig get 1 0 0
sig get 1 0 1
sig set 1 0 0 200
sig set 1 0 1 -10
sig get 1 1
sig set 1 1 100
```

依次表示：显示 Motor 的全部登记字段、读取 speed、读取 temperature、
设置 speed、设置 temperature、读取 Counter、设置 Counter。
设置 Counter 后测试任务继续递增，稍后读取可能已超过设置值。

### 类型处理

| 类型 | 读取显示 | 写入格式 |
| --- | --- | --- |
| U8/U16/U32/S32 | 十进制整数 | 十进制整数，检查类型范围和可选业务范围 |
| RAW | 大写十六进制字节串 | 连续十六进制字符串，大小写均可，不带 0x |
| STRUCT | 一次读取整组快照，按 paramIndex 显示登记字段 | 必须指定字段下标，不支持整组文本赋值 |

RAW 写入必须恰好提供 size 个字节，每字节两个字符。例如长度为 3 的 RAW
数据应输入 `AB01FF`。当前产品点表没有 RAW 测点，后续添加后命令可直接使用。
RAW 显示的是原始内存字节，不进行字节序转换。

目前只支持一层 STRUCT，不支持嵌套组合。STRUCT 中未登记的字段不显示，
但通过 C 接口整组读写时仍会复制，包括结构体填充字节。

非法输入、超范围、未知设备或下标会返回错误。命令默认不等待锁，忙时可重试。
Shell 为本次操作申请临时数据缓冲区，结束后释放；申请失败不修改数据。

## C 接口与范围描述

接口声明见 [sig_data.h](sig_data.h)：

- `sigDataInit`：启动时初始化一次，点表和 handle 由本模块私有持有。
- `sigDataGetInfo`：查询 SIG 类型、长度、范围及字段描述。
- `sigDataSet` / `sigDataGet`：通过请求结构体整组读写。
- `sigDataSetParam` / `sigDataGetParam`：通过请求结构体访问 STRUCT 字段。

SIG 和 Param 共用 `const aBusRange_t *range`：NULL 表示没有业务上下限，
非 NULL 时根据所属 type 读取 min/max 的对应联合体成员。
RAW 和 STRUCT 自身的 range 必须为 NULL；STRUCT 的整数参数可各自设置范围。
范围对象和字段描述在实例使用期间必须保持有效且不变。

字段写入使用父 SIG 的锁，只更新目标字节，并检查受影响的范围规则，
不会先读整个结构体再写回。整组写入仍然是完整替换。

## 静态绑定与文件分工

| 文件 | 职责 |
| --- | --- |
| [sig_ids.h](sig_ids.h) | 设备号、SIG 下标和稳定键 |
| [sig_data.c](sig_data.c) | 点表、范围、字段描述、私有实例和通用接口 |
| [sig_data.h](sig_data.h) | 对外接口及静态绑定宏 |
| [sig_modbus_bind.h](sig_modbus_bind.h) | 将私有 SIG 实例绑定到 Modbus 的装配入口 |
| [sig_command.c](sig_command.c) | 通用 Shell 文本解析与显示 |
| [sig_task.c](../../task/sig/sig_task.c) | 持有静态 Counter，通过分散注册绑定，每秒递增 |

`APP_SIG_BIND` 使用 aBus 的 `.abus_bindings` 收集机制，按 instanceID + deviceID + sigIndex
关联静态变量。初始化成功会写入点表默认值；变量不需要对命令层暴露。

任务直接访问绑定变量时不经过 aBus 锁，同步由应用自行决定。当前 Counter
测试任务直接递增，Shell 设置与递增之间不保证原子组合操作。

新增测点时更新点表和标识；STRUCT 同时提供字段描述，需要静态存储时增加绑定。
Shell 命令源码无需同步增加测点分支。aBus 的详细契约见
[aBus README](../../../func/aBus/README.md)。

### 读取整个设备

```text
sig get 1
```

省略 sigIndex 时按下标顺序显示设备 1 的全部 SIG，STRUCT 展开登记字段，
RAW 显示十六进制。保留 `sig get 1 0`（整个 Motor）和
`sig get 1 0 0`（Motor.speed）的用法。
每个 SIG 单独读取快照，整张表不保证来自同一时刻；读取失败时停止并报告错误。

## 读取时显示测点信息

所有 `sig get` 形式都会同时显示定义信息和当前值，例如：

```text
sig[1:0] key=1001 type=STRUCT size=8 flags=0x0001 (LOCK) params=2 range=none
sig[1:0] (2 params)
  param[0] type=U32 offset=0 size=4 range=[0,6000]
  param[0]=100
  param[1] type=S32 offset=4 size=4 range=none
  param[1]=25
sig[1:1] key=42 type=U32 size=4 flags=0x0001 (LOCK) params=0 range=none
sig[1:1]=123
```

- key：稳定的 sigKey；命令定位仍使用 sigIndex。
- type：通用数据类型；enum 到字符串使用 aLib 的 `aDataTypeName`。
- size：字节数；offset：字段相对 SIG 起点的字节偏移。
- flags：原始十六进制标志和逐位名称，由 `aBusSigFlagName` 解释。
- LOCK：该 SIG 请求在读写时使用锁；是否实际创建锁取决于 aBus 锁模式，
  NONE 模式下不使用锁。该标志不保护应用直接访问绑定变量。
- params：登记字段数量；range=none 表示没有额外业务上下限，整数仍受类型限制。

单独读取字段时也会显示父 SIG 信息及该字段信息。`sig set` 继续简洁地回显
本次写入值。未知数据类型或标志的名称返回 UNKNOWN，标志原始数值仍保留。

## 当前实例号

应用定义 `APP_SIG_INSTANCE_ID = 1`、`APP_SIG_DEVICE_ID = 1`。
`sigDataInit` 使用实例号 1，`APP_SIG_BIND` 自动填入相同实例号和设备号，
因此 Motor 和任务持有的 Counter 都归属当前私有 handle。

实例号与设备号是两个独立层级，当前数值恰好相同。Shell 仍只输入设备号，
实例由 app 的私有 handle 确定：`sig get 1` 中的 1 依然是 deviceID。
所有实例的绑定统一保存在 `.abus_bindings`，无需为当前 app 新增链接段。

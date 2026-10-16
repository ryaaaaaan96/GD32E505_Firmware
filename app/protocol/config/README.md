# JSON 点表输入（v1）

[points.json](points.json) 是测点、协议映射和持久化选择的唯一输入。当前包含
IDU/FAN 各 50 个 SIG、主站 50 条映射（30 项轮询）、从站 100 条映射、
3 个回调段和 12 个持久化项。JSON 的上游来源不受限制。

## 使用

在仓库根目录准备 Python 3.10+ 环境：

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r scripts/requirements.txt
python scripts/generate_points.py --validate
python scripts/generate_points.py
python scripts/generate_points.py --check
python scripts/build.py
```

`--validate` 校验并试渲染，不写文件；默认更新有差异的生成文件；`--check`
只检查同步状态，不一致返回非零。`--input` / `--output` 可指定独立输入/输出。
同一输出目录不要并发生成。Python 校验数据和解析引用，Mako 只渲染仓库的固定模板；
JSON 不执行 C、Python 或模板表达式。

build.py 在 CMake 配置前生成；直接 cmake --build 只检查，变更后需先生成。
首次 CMake 配置使用安装依赖的 Python3_EXECUTABLE。CI 安装相同依赖并检查快照。
全部校验、渲染成功后才写文件；单文件原子替换。相同输入/模板生成相同内容，
不写时间戳或绝对路径；未变化文件保持时间不变，避免无关重编译。

## 最小结构

完整结构见 [schema.json](../../../scripts/points/schema.json)。以下示例可通过校验；
它仅说明格式，当前板级绑定和测试代码仍引用现有测点，不能直接用它替换产品输入。

```json
{
  "format_version": 1,
  "devices": [{
    "name": "IDU",
    "device_id": 1,
    "types": [],
    "signals": [{
      "name": "TEST_001",
      "key": 2001,
      "type": "u32",
      "flags": ["LOCK"],
      "default": 100,
      "range": {"min": 0, "max": 6000},
      "persistent": true
    }]
  }],
  "protocols": [{
    "type": "modbus",
    "name": "IDU_modbus_slave",
    "role": "server",
    "maps": [{
      "target": {"device": "IDU", "sig_key": 2001},
      "area": "holding_registers",
      "address": 0,
      "quantity": 2,
      "access": ["READ", "WRITE"],
      "word_order": "high_first"
    }]
  }]
}
```

`protocols` 可以为空；每个设备的 `signals` 不可为空。

## 测点和类型

| 字段 | 语义 |
| --- | --- |
| device_id | 0..65535，同一输入内唯一 |
| name | 设备/SIG 用大写 C 标识符；类型名、成员名用 C 标识符 |
| signals 顺序 | 自动生成连续 sigIndex；JSON 不重复配置 index |
| key | 同设备内唯一的 16 位稳定标识，发布后不要随顺序更改 |
| type | u8/u16/u32/s32 或本设备声明的 RAW/STRUCT 类型 |
| flags | 必填数组，可为空；LOCK 控制同步，DISJOINT 仅用于 STRUCT |
| storage | 默认 managed；bound 表示应用提供分散 RAM 绑定 |
| default | 标量整数、RAW 字节数组、STRUCT 成员对象；省略或 null 为全零 |
| range | 标量可选上下限，不需要时省略 |
| field_ranges | STRUCT 按成员名提供可选的 {min,max} |
| persistent | 默认 false；true 进入持久化选择清单 |
| data_version | 仅用于持久化项，0..4294967295，默认 0；业务含义改变时修改 |

`data_version` 生成到持久化清单，不进入 aBus RAM 状态。零版本兼容原有布局标记，
本次升级不使已有有效记录失效。同一 device_id / key 的版本变化时，数据库打开
流程先作废旧快照，再保存新标记；未保存新值前，应用保留 aBus 默认值。
修改单位、比例、同类型字段的业务含义或 RAW 内部格式时须修改版本，即使 size 不变。
版本用于相等比较，回退版本也会作废旧值；需要保留旧值时须另行设计迁移。
JSON `format_version` 描述生成输入格式，不能替代每项数据的业务版本。

STRUCT 类型示例（写在设备的 types 数组）：

```json
{
  "name": "FanState_t",
  "kind": "struct",
  "size": 8,
  "fields": [
    {"name": "speed", "symbol": "SPEED", "type": "u32", "offset": 0},
    {"name": "temperature", "symbol": "TEMP", "type": "s32", "offset": 4}
  ]
}
```

SIG 可配 `"default": {"speed": 100, "temperature": 25}`、
`"field_ranges": {"speed": {"min": 0, "max": 6000}}`。
RAW 类型例如 `{"name":"Payload_t","kind":"raw","size":8}`；
默认值必须恰好 8 个 0..255 的字节。JSON 整数使用十进制；拒绝浮点、
NaN、Infinity、重复属性、未知字段、越界默认值及默认值不满足范围的情况。

v1 STRUCT 只含标量或 RAW，不支持嵌套。字段按 offset 连续排列、自然对齐，
size 等于全部字段长度之和且没有尾部填充；空隙必须显式声明保留字段。
生成头通过 aLib 再断言 sizeof/offsetof，不使用 packed 强制布局。
这些是生成格式的约束，aBus 底层仍保留更宽泛的定义能力。

## 协议映射

v1 的协议 type 仅支持 modbus，后续协议增加自己的校验与模板，不共用地址语义。

target 使用 device + sig_key，可选 param 使用成员名选择字段，省略则映射整个 SIG。
生成时解析为设备号、SIG/字段枚举，运行时不查 JSON。
area 支持 coils/discrete_inputs/holding_registers/input_registers；
address 是从 0 开始的协议地址，quantity 必须显式填写且与目标长度一致。

U8/U16 占一个寄存器，U32/S32 占两个；RAW/STRUCT 长度必须为偶数，
STRUCT 每个字段也必须占完整寄存器。位区域仅支持 U8、quantity=1。
当前单个映射目标不超过 aModbus 的 250 字节上限。
word_order 为 high_first/low_first，后者只用于 32 位整数或 STRUCT 的 32 位字段。
access 显式填写 READ/WRITE；输入寄存器和离散输入只能读。

从站 maps/ranges 分别按区域和地址排序，并检查溢出和交叉重叠，不要求连续。
主站保持输入顺序，允许重复采集地址；协议容量、请求冲突等运行期校验仍由模块执行。
主站必须填写 poll_time_ms：0 不轮询，"once" 一轮后停止，
1..2147483647 为毫秒周期，符合 aComm 的回绕比较约束。
break_before=true 在该项前断帧，其他连续且兼容的项由 aComm 合并。

subscribe 是事件数组：主站接受 REQUEST_READ/REQUEST_WRITE 或单独 MARK_ONLY；
从站接受 UPDATED/WRITE_COMPLETE 或单独 MARK_ONLY，观察事件需应用提供 notify。
主站 publish 可填 WRITE_COMPLETE；从站必须为空，更新由数据端点发布。
生成器不会代替应用建立事件队列和回调。

从站 ranges 提供 name/area/address/quantity/access、read/write 函数名，
以及可选 context 对象名（生成 &对象）。函数、对象在宿主 .c 定义；
生成器检查权限和标识符，编译器检查签名和是否存在。

## 生成文件与应用边界

| 输出 | 用途 |
| --- | --- |
| inc/<设备>_sig_table.h | 类型、布局断言、连续索引、固定 Key、字段枚举 |
| sig/<设备>_test.inc | SIG/字段定义、范围、默认值，沿用 X-Macro 展开 |
| generated/sig_tables.inc | 汇总点表，纯静态模式生成 managed 项的 RAM 绑定 |
| generated/<协议名>.inc | 普通 C 主从映射和回调段数组 |
| inc/protocol_generated.h | SIG/字段/映射/持久化容量及回调段常量 |
| persistence/sig_persist.inc | 按 deviceID、sigKey 排序的紧凑持久化选择 |
| generated/points_manifest.json | 输入 SHA256 与输出清单 |

生成文件与 JSON 一起提交，便于审查 C 差异。不要编辑带生成标记的文件。
删除输入项后，生成器清理清单中已不再使用的生成文件，保留手写文件。

端口、站号、超时预算、业务回调、任务资源和初始化留在应用 C 代码。
新增协议实例仍需在 protocol.c 装配，生成数组不等于自动启动实例。
managed 项自动补全静态容量；bound 项仍需应用提供正确的 RAM 绑定。
Flash 旧记录迁移不属于生成器职责，改变字段含义/布局时须另行设计。

回归与测量见 [验证指南](../../../docs/testing.md)和
[性能基准](../../../benchmarks/README.md)。

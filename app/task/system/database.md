# 应用数据库

数据库按配置与运行职责拆分：

- `app/devices/system/database_config.c`：KV/TSDB 分区选择、超时和记录容量。
- `app/devices/system/memory_config.c`：绑定现有 SPI1 Flash 并注册分区。
- `database_service.c`：私有持有 KV/TSDB 实例，管理初始化、打开、关闭与读写。
- `database_command.c`：提供 `db` 调试命令，由 Shell 任务调用。

aSystemInit 依次初始化 Flash、aMemory 分区，再调用 appDatabaseInit。
appDatabaseInit 内部调用 aDataBaseInit 后尝试打开已有数据库；空白介质
提示运行 db init，并继续初始化其他应用服务。Shell 任务等待全部服务就绪，命令
不会与启动时的数据库打开操作交错。appDatabaseInit 仅在启动时调用一次。

默认启用 ADATABASE_ENABLE，后端为 FLASH25Q。数据库模块本身支持多实例；
这里只采用应用层单例。时序采集任务暂不自动创建，记录由业务调用追加。

## 分区

布局位于 config/aMemory_layout.h，按已识别的 8 MiB、4 KiB 擦除块配置。

| 数据库 | 分区名 | 起始地址 | 长度 | 地址范围 |
| --- | --- | --- | --- | --- |
| KV | param | 0x00100000 | 128 KiB | 0x00100000 到 0x0011FFFF |
| TSDB | log | 0x00120000 | 512 KiB | 0x00120000 到 0x0019FFFF |

首次执行 db init 允许擦除或修复上述数据库分区，需将这两段地址留给数据库。
有效的已有数据库会直接打开，db init 不会每次清空数据。
0x007FF000 不在数据库分区内，原 Flash 测试仍可使用；不要对活动数据库
区域运行原始 flash test，原始擦写会破坏 FlashDB 元数据。

## Shell 验证

首次使用：

```text
db init
db info
```

KV 按 key 读写文本；通用应用接口仍支持任意二进制数据：

```text
db kv set speed 1200
db kv get speed
db kv set speed 1500
db kv get speed
```

读取同时显示长度、可打印文本和十六进制内容。Shell 单次最多读取 256 字节；
更大的 KV 使用业务接口。写入文本不附加末尾 NUL，业务读出时按长度使用。

TSDB 按显式时间戳追加并查询：

```text
db ts append 1000 motor_started
db ts append 1001 speed_1200
db ts append 1002 speed_1500
db ts list
db ts list 1001 1002
```

如果已有日志的时间戳大于这些示例值，先用 db info 查看 last_time，
再使用更大的时间戳。时间戳为正数且严格递增，同一时间戳不能重复追加。
应用可以使用统一单位的实际时间，也可以使用持续递增的逻辑序号。

列表单次最多显示八条，并受输出字节预算限制；达到限制后打印下一页命令。
例如 Continue: db ts list 1003 9223372036854775807。

验证关闭重开和复位后仍能读到数据：

```text
db close
db init
db kv get speed
db ts list
```

也可复位板子：启动应输出 Database: KV and TSDB ready，再读同一 key 和日志。
验证完需要删除参数时执行：

```text
db kv del speed
```

I/O 失败使对应实例进入故障状态，可先 db close，再 db init 尝试恢复。
若底层 SPI 总线已进入故障状态，需要先恢复外设；当前应用可通过复位重新初始化。

设备初始化后由 memory_config.c 注册 aMemory 设备和分区，再初始化
aDataBase 服务。FlashDB 通过 aMemory 直接访问分区，产品不再配置 FAL。

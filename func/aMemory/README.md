# aMemory

func 层的同步存储接口，统一设备生命周期、注册、分区和读写擦除。模块不依赖 aOS、
具体芯片或 FlashDB，不分配堆、不创建任务、不缓存写入。

```text
业务 / FlashDB → aMemory 分区句柄 → 设备操作回调 → 设备驱动
```

## 定义和初始化

- `aMemoryDevice_t`：名称、上下文、操作表和几何参数。
- `aMemoryOps_t`：设备初始化、释放和同步读写擦除回调。
- `aMemoryPartition_t`：名称、所属设备、偏移、长度和访问权限。
- `aMemoryHandle_t`：内部固定数组中的运行句柄，业务不能直接修改。
- `aMemoryConfig_t`：初始化时使用的设备、分区指针表。

`AMEMORY_DEVICE_DEFINE` 声明只读设备描述，支持指定成员初始化。
`AMEMORY_PARTITION_DEFINE` 声明只读分区描述。宏不执行设备初始化，
不依赖 GCC 分散段或链接脚本；把描述地址加入配置数组后，统一注册。

```c
AMEMORY_DEVICE_DEFINE(external_flash,
    .name = "external", .context = &flash_context, .ops = &flash_ops,
    .geometry = {
        .capacity = 8U * 1024U * 1024U,
        .read_granularity = 1U, .write_granularity = 1U,
        .erase_granularity = 4096U,
        .program_bits = 1U, .erased_value = 0xFFU
    }
);
AMEMORY_PARTITION_DEFINE(parameters, "parameters", external_flash,
    0x100000U, 0x20000U, AMEMORY_ACCESS_ALL);
```

应用提供设备操作表和区域描述，调用 `aMemoryInit` 后统一完成：

1. 校验名称唯一、设备归属、容量、能力、粒度、区域越界及重叠。
2. 按设备表顺序调用 `ops.init(context, geometry)`；回调初始化驱动，
   核对配置中的容量与实际介质能力，不擦写业务区域。
3. 全部设备成功后发布区域句柄，业务才可 Find / Read / Write / Erase。

`init` 和 `deinit` 必须成对注册；都为 NULL 表示借用外部已就绪设备，
例如无需硬件初始化的 RAM 区域。初始化失败时，按逆序清理，包含失败设备；
因此 `deinit` 须支持尚未或仅部分初始化的状态。正常 DeInit 也按逆序释放。
若清理失败，返回清理错误并保留进度；业务不可访问，重新 Init 返回 BUSY，
应用保留配置后重试 DeInit。成功释放的设备不会被重复释放。
生命周期回调不得重入 aMemory 的 Init / DeInit。

描述、设备/区域指针数组、操作表、名称及上下文保持有效且不变，直到释放
完成（包括失败后的清理重试）。配置结构本身只在初始化调用期间使用。
同一物理设备只能注册一个设备对象，多个逻辑区域用分区表示。

当前产品的设备回调、私有 Flash 实例及区域描述统一放在
`app/devices/system/system_device.c` 的存储段，
`appSystemMemoryConfigInit` 只提供注册配置，不访问硬件。
`app/task/system/system_init.c` 的私有 `memoryInit()` 装配配置并调用
`aMemoryInit`，由 aMemory 初始化设备。顶层编排使用一次 memoryInit 调用。
布局位于 `config/aMemory_layout.h`；增加设备只需提供回调、描述及分区并加入表中。
当前顺序为 Shell → memory → log → database。应用设备回调使用 ASHELL_PRINT
输出探测信息，不依赖 aLog；启动文字先入队，全部服务就绪后由 Shell 任务发送。
后续需要数据库的日志输出仍须在 database 就绪后挂接。
配置也可由应用在启动时填写，前提是成功注册后一直保持稳定。

## 访问契约

初始化后通过 `aMemoryFind(name)` 获取分区句柄；名称只在查找时使用。
`aMemoryGetInfo` 返回分区容量、所属设备及几何参数。

```c
aMemoryReadRequest_t request;
const aMemoryHandle_t *partition = aMemoryFind("parameters");

aMemoryReadRequestStructInit(&request);
request.address = 0U;
request.data = buffer;
request.size = sizeof(buffer);
status = aMemoryRead(partition, &request);
```

公共请求地址相对分区；设备回调收到的是相对整个设备的地址。地址和容量
使用 `uint64_t`，单次长度为 `size_t`。运行时直接访问句柄，不遍历注册表。
请求、缓冲区和回调收到的换算后请求均只在本次调用期间有效。

读写地址和长度按对应字节粒度对齐；缓冲区本身无额外对齐要求。
擦除按擦除块对齐。写入粒度不是页大小，分页编程由设备驱动负责。
`program_bits` 以位为单位，表达数据库需要的介质编程能力。

写入不会隐式擦除；支持的操作由设备回调和分区 access 共同决定。
不支持的回调可为 NULL，对应分区不能声明该能力。读写失败可能已经
处理部分数据；不提供回滚。零长度请求仍检查句柄、权限、地址和对齐，
检查通过后返回成功，不访问设备，缓冲区允许 NULL。

成功的 write 必须完成底层介质提交。RAM 回调表示内存更新完成；用于
持久化的 Flash 或文件回调必须完成实际编程或必要的同步，不能仅入队。
文件、EEPROM 等需自行提供适配，本模块当前只接入板载 SPI Flash。

超时原样传给设备回调，默认 5000 ms；是否支持 NO_WAIT 由设备决定。
不新增公共锁，设备驱动负责共享设备同步。跨多次调用的事务由上层负责。
生命周期不得与访问并发，须先停止使用者，再关闭数据库，最后关闭 aMemory。
注册了生命周期回调的设备由 aMemory 释放，应用不得再次释放。

长期使用者可调用 `aMemoryRetain` / `aMemoryRelease` 成对持有模块引用；
存在引用时 DeInit 返回 BUSY。数据库模块会自动持有此引用，调用者不能
代替它释放。DeInit 后全部借用句柄失效，再初始化后需要重新查找。

## 应用关联数据库

区域分配由应用决定。`system_device.c` 通过 `aMemoryFind` 取得 `param`、
`log` 区域，将句柄分别写入 KV / TSDB 配置的 `memory` 字段。aDataBase 及其
FlashDB 适配不按名称查表，也不初始化物理设备。

EEPROM、FRAM 可注册读写回调并省略 erase；区域只授予读写权限。
`aMemoryWrite` 不模拟擦除或自动搬移，后端保留真实介质语义。
当前 FlashDB 适配要求可擦除且几何参数兼容，不能把任意 EEPROM/FRAM
区域直接传给数据库就视为兼容；业务直接使用 aMemory 读写不受此限制。

## 配置和验证

在 `config/aclass_config.cmake` 中设置：

```cmake
set(AMEMORY_ENABLE ON)
set(AMEMORY_MAX_PARTITIONS 16)
```

分区上限为 1..256；元数据留在应用的只读定义中，运行表每个分区只保存
一个描述指针。未启用时不构建模块。

```sh
SANITIZE=1 python3 tests/memory/run.py
SANITIZE=1 python3 tests/database/run.py
python3 tests/database/test_flash_chain.py
```

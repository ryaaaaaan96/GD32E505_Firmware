# 应用层组织

- 系统 console 实例随 ASHELL_ENABLE 启用或裁剪，不再单独设置应用 USART 开关。
- devices/system/app_system_device.h：系统设备的 LED 句柄、console 流专用初始化接口。
- devices/system/app_system_device.c：统一管理系统设备的 LED 与 USART 配置结构、缓冲区和私有句柄；参数直接在本文件中配置。
- task/system/system.c：统一管理 LED 状态任务和 Shell 任务，内部逐项初始化设备、服务并创建任务；对外仅提供 aSystemInit。
- task/system/system.h：声明系统服务启动入口 aSystemInit。
- main.c：初始化驱动和 OS，创建 appInitTask 后启动调度器；初始化任务完成系统功能初始化后通过 aOSTaskExit 自退出。

每个实例由应用保证只调用一次 Init，直接返回底层初始化结果，不缓存阶段或错误。
运行阶段传递已取得的句柄；不通过重复 Init 获取句柄，也不提供重入保护。
无独立 Open、统一设备初始化入口或分散注册。仅启动阶段单线程调用。
设备参数在 devices 下对应 .c 文件的配置结构及缓冲区声明中直接设置；任务栈、优先级和单次使用的延时在使用处设置。重复使用的状态灯闪烁周期保留为 system.c 内的具名常量。各头文件的包含保护宏保留原位，平台功能裁剪由 config 下的 CMake 配置控制。

详见 [设备按实例初始化](../docs/architecture.md) 和 [接口规范](../docs/interface_contract.md)。

当前不做运行时跨设备资源冲突检查。后续可使用 Python/CMake 增加构建期告警，尚未实现；设备参数、能力和初始化错误检查继续保留。

业务任务统一归 app/task 所有，func 不创建/删除任务，也不配置任务优先级和栈大小。
func 提供 Init/Process/DeInit 或事件处理入口；app 决定调用线程、周期和停止顺序。
销毁 func 实例前必须停止其调用者并等待在途操作结束，禁止强行删除持有模块锁的任务。
system 设备公开 appSystemStatusLedInit 与 appSystemConsoleInit，保留按实例初始化，不做统一重初始化。

控制台通过 `appSystemConsoleInit(void)` 初始化 USART、绑定 Stream 并初始化 aShell 单例，
USART 类型、私有控制台句柄指针和适配回调只在 app/devices 内部使用；
流接口不携带 context，回调直接访问已绑定的控制台句柄。aStream_t 定义在
platform/aLib/include/aStream.h；Shell 配置在设备初始化函数内构造，任务层创建并运行 Shell 任务。
flush 非空时表示显式提交缓冲输出；NULL 表示 write 已直接提交，无需刷新。
flush 不等待线路完成、不丢弃输入；Shell 不自动调用。

当前控制台使用 aDevUsartCreate 动态分配对象，Shell 初始化失败时调用
aDevUsartDestroy 回收；产品必须开启 ADEV_USART_DYNAMIC_ENABLE。

## SIG 数据与测试任务

`app/sig` 持有私有 aBus handle，由 aSystemInit 在 Shell 初始化后
调用 appSigInit，再启动 app/task/sig 的测试任务。关闭 Shell 时
数据与测试任务仍初始化；关闭 aBus 时一并裁剪。

业务统一通过 `appSigSet(request)`、`appSigGet(request)` 整组读写。
请求使用 aBusSetIndexRequest_t / aBusGetIndexRequest_t，通过对应
StructInit 初始化后填写 sigIndex、缓冲区、长度和锁等待时间。
sigIndex 是 app_sig_ids.h 的枚举下标，不是稳定 sigKey。
未初始化返回 NOT_READY，未知下标返回 NOT_FOUND，长度错误返回
INVALID_PARAM。请求与缓冲区只在调用期间借用。

motor 初始值为转速 100、温度 25，转速允许 0..6000。
Counter 初值为 0，在 app/task/sig/app_sig_task.c 中定义为静态变量，
通过旁边的 APP_SIG_BIND 分散注册绑定。任务每次延时
1000 ms 后直接 counter++，UINT32_MAX 后回绕到零。
实际周期包含调度时间，不作为精确计时器。

表和 handle 均由 app_sig.c 私有持有。
APP_SIG_BIND 使用统一的 APP_SIG_DEVICE_ID 与参数下标，直接交给
ABUS_RAM_BIND_EXPORT 注册。aBus 按 instanceID + deviceID + sigIndex 匹配，
只使用 .abus_bindings 收集；描述和收集指针均为 const，存放 Flash。
同一绑定设备号只允许一个活动实例，由应用保证。
绑定只关联存储，不额外提供并发保护。直接访问绑定变量的同步策略
由应用自行决定；当前自增测试不做同步，Shell 设置值可能被自增覆盖。
通用读写仍遵循 aBus 锁配置，timeout 仅用于 aBus 锁等待，不能保护
任务绕过接口直接访问绑定变量。
动态创建启用时分配实例元数据，否则使用静态实例；两个测点的数据
均静态绑定。aBus 自身保持多实例能力。

### Shell 命令

命令在 app/sig/app_sig_command.c 注册，按 deviceID + sigIndex 数值访问：

- `sig get 1 1`：读取 Counter（APP_BUS_COUNTER）。
- `sig set 1 1 100`：设置 Counter 为 100，任务随后继续自增。

当前 Shell 支持标量、RAW 和 STRUCT，类型和参数描述均来自点表。
ID 和整数值采用十进制，RAW 使用十六进制；非法输入不修改数据。
命令默认不等待锁，忙时返回错误，用户可重试。
已删除 app/protocol 和 bus_demo，业务命令归 sig 模块管理。

当前应用挂载一张表，通用读写请求的 deviceID 使用 APP_SIG_DEVICE_ID。
aBus 支持一个 handle 挂载多表；应用后续扩展时可传入表数组与表数量，
并按 deviceID 校验和路由请求。Shell 显式接收 deviceID 和 sigIndex，通过 appSigGetInfo 查询类型和长度，
不维护测点类型表，也不引用具体测点枚举。

### 通用 SIG 命令

```text
sig get 1 0           # Motor：显示全部已登记字段
sig get 1 0 0         # Motor.speed
sig set 1 0 0 200     # speed 范围为 0..6000
sig set 1 0 1 -10     # temperature：S32，无业务上下限
sig get 1 1           # Counter
sig set 1 1 100       # Counter：U32，无业务上下限
```

命令从 SIG 定义获取类型和字段描述。RAW SIG/字段也支持按完整长度进行连续
十六进制读写；Motor 为 STRUCT，必须指定参数才能文本赋值。
SIG 和 Param 都通过可选 range 指针指定上下限，NULL 表示不限制业务范围。
字段操作由 aBus 在原 SIG 锁内完成；任务直接修改绑定变量的同步仍由应用负责。

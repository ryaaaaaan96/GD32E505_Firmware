# aDev_Flash25q

模块封装官方 SFUD，当前已实现同步 SPI 后端。公共接口见
[aDev_flash25q.h](aDev_flash25q.h)，完整边界见
[链路设计](../../docs/spi_flash_design.md)。

```text
应用 Read / Write / Erase → Flash25Q → SFUD → SPI port → aDrvSpi
```

## 当前板级配置

| 项目 | 配置 |
| --- | --- |
| 外设 | GD32 SPI1 / ADRV_SPI_1 |
| SCK / MOSI / MISO | PB13 / PB15 / PB14 |
| 软件 CS | PB12，低有效 |
| 模式 | 主机、8 位、MSB、Mode 0 |
| 分频 | 64，基于控制器输入时钟 |
| 容量 | JEDEC ID / SFDP / SFUD 参数表探测 |

板级实例位于 app/devices/system/app_system_flash.c，句柄私有。
aSystemInit 在 Shell 初始化之后调用 appSystemFlashInit，启动只探测，
不执行编程或擦除。成功时打印 JEDEC ID、容量和擦除粒度；失败时返回错误。
Flash 具体型号仍需通过实际板子核对，当前没有上板验证结论。

## 使用

- BusInitStatic 创建控制器和总线锁；每个物理控制器只能创建一个 bus。
- ConfigStructInit 后设置 bus、cs_pin，可选 expected_capacity。
- InitStatic / Create 创建 Flash 对象，静态布局见 instance 头文件。
- Read / Write / Erase 使用各自请求结构体和 StructInit；GetInfo 查询探测信息。
- Write 不隐式擦除；Erase 必须按探测的 erase_size 对齐。
- 完成所有业务访问后先 DeInitStatic / Destroy，再 BusDeInitStatic。
- 所有生命周期调用由应用串行编排，不能与正在运行的读写交错。
- 当前不支持 NO_WAIT；使用正数毫秒预算或 FOREVER。
- 状态轮询超时可能意味着芯片仍忙；总线传输故障会使 bus 失效，必须
  先关闭借用实例，再重新初始化 bus。

应用可以使用 appSystemFlashRead/Write/Erase/GetInfo，业务代码不接触 SPI。
擦写测试由独立的 [app/flash_test](../../app/flash_test/README.md) 提供，
通过 Shell 的 flash test 命令手动触发，不在启动时自动执行。

## 同步与官方源码

官方路径为 SFUD/sfud/src，上游提交：
`6b4bef82e6c603b783a17968f1b75da89cc5e2f8`。
本次未修改上游源码，项目配置在 config，适配代码在 port。
项目配置优先于官方示例配置，SFUD 的 OBJECT target 最终并入 aFlash25q，
避免静态库循环引用导致移植符号无法链接。

SFUD 页编程有共享静态缓冲区和一次内部数据复制，模块锁串行保护所有
实例的完整操作。总线锁只保护一次 CS 事务，WIP 等待期间释放。
这不是端到端零拷贝或不同 Flash 的并行操作实现。

QSPI 后端尚未接入；现有 aDrvQspi 不能作为本模块的已验证后端。
当前 ADEV_FLASH25Q_ENABLE 依赖 ADRV_MODULE_SPI_ENABLE，不依赖 QSPI。
ADEV_FLASH25Q_STATIC_ENABLE / DYNAMIC_ENABLE 控制对象创建接口。

## 验证

```sh
python3 tests/flash25q/run.py
python3 tests/flash25q/build_matrix.py
cmake --build build/Debug -j 4
```

Host 测试使用真实 SFUD 与字节级 SPI NOR 模型，覆盖分页、回读、严格擦除
范围、锁/传输失败、WIP 超时及资源释放；模拟器走无 SFDP 参数表回退。
硬件引脚、实际波形、特定型号 SFDP 内容和写保护状态仍需上板验证。

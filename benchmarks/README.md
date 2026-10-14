# 性能基准

当前测量 aBus 定位/复制开销和真实 Modbus 采集计划构建；板端提供 CPU 周期测量。
测试不触碰产品测点或 Flash。

## 主机测量

激活仓库虚拟环境后，在根目录执行：

```sh
python scripts/benchmark.py
python scripts/benchmark.py --output build/benchmarks/baseline
python scripts/benchmark.py --baseline build/benchmarks/baseline/results.json
python scripts/benchmark.py --quick --output build/benchmarks/smoke
```

需要主机 GCC、pthread 和代码生成依赖。默认输出 build/benchmarks/；每个锁模式
单独编译，生成 results.json、results.csv、各模式原始 jsonl 和测试可执行文件。
JSON 记录编译器、完整参数、平台、Git revision/dirty、点表 SHA256、采样配置及结果。
`--cc clang` 可换编译器，对比时须注明编译器差异。

| 项目 | 工作负载 |
| --- | --- |
| aBus Index / Key 读写 | 16 / 128 / 1024 个 SIG；4 / 32 / 128 字节 |
| 锁 | NONE / BUS / SIG 三种编译配置；每项均声明 LOCK |
| 类型 | 4 字节使用 U32，其余 RAW；无额外范围规则和事件订阅 |
| 统计 | 4 批预热；256 个计时样本，每批 32 次 API 调用 |
| 计时 | CLOCK_MONOTONIC 纳秒；单线程、真实 pthread 无竞争锁 |
| 计划 | 生成的 FAN 主站点表，真实 aModbus 回调和 aComm 分组/准备报文 |

读写顺序循环遍历点表，Key 非有序且不重复；计时包含请求定位、状态检查和循环开销。
每项验证返回值、读回内容和校验和，结束后检查申请内存已全部释放。
主机 OS 支持代码仅供基准，不代表 aOS 已完成 Linux 后端。

min/median/p99/max 是**每批平均每次 API 耗时的分布**，不是单次调用尾延迟。
clock_pair 记录两次时钟读取的中位成本，不从结果中强行扣除。
内存峰值为 aOSAlloc 申请的字节，包括测试定义、工作区、数据、状态和锁对象，
不包含 malloc 管理开销、栈或只读区，不等于 MCU RAM 占用。

计划测量使用实际点表，关闭事件订阅，只测计划构建；任何 I/O 调用都会失败。
每种配置创建/销毁 64 次，仅计时 Create，对比原始配置和全部逐点断帧的帧数、
平均构建时间和新增申请字节峰值。当前 50 条映射中 30 项轮询，原配置 6 帧，
逐点断帧 30 帧。帧数减少 80% 不等于吞吐提升 80%；响应和线路耗时需上板测量。

`--quick` 缩为 16 个计时样本，供 CI 检查工作负载、结果和资源释放，不用于性能结论。
CI 不使用跨机器耗时阈值。baseline 对比输出新/旧 median 比值，小于 1 表示本次更快。
应在同一机器、编译参数、点表和负载下多次复测；测量时尽量避免后台构建，保留原报告。

## MCU 测量

默认演示固件启用 aBus 动态接口及 Shell 时可用：

```text
sys heap
bench bus
sys heap
sys stack shell
```

使用 aDrv 的 DWT 计数接口，不清零共享计数器；独立创建 16 项 U32 测试表，
每项 128 批 × 8 次操作，输出 CPU 频率、锁模式、事件开关及 cycles/op 统计。
按 32 位无符号差处理一次回绕，每批须短于回绕周期。
周期数除以 CPU_Hz / 1000000 即微秒，180 MHz 时除以 180。

测试不关中断、不暂停其他任务，输出在计时区间之外。分配不足时返回错误并回收
测试资源，无额外线程。关闭 APP_DEMO_ENABLE、aBus 动态接口或 Shell 时不编译；
纯静态产品仍正常使用生成点表。Debug/Release 分别测，记录优化等级、负载及栈水位。

当前已编译验证，尚无本轮真实板端结果。后续在空闲、Modbus 轮询、日志负载下
分别测量，补充端到端时延和丢帧统计。Flash 擦写/GC、掉电、ISR 最坏延迟及
锁竞争尚未包含在本基准中。

## 文件职责

- bench_bus.c/h：主机和 MCU 共用 aBus 工作负载，注入时钟和输出。
- host_port.c：主机真实时钟、互斥锁及申请字节统计。
- bench_plan.c：真实生成点表的 Modbus 计划对比，不访问串口。
- scripts/benchmark.py：编译三种锁模式、测量、报告和历史对比。
- app/task/system/command_benchmark.c：Shell 板端入口，继承应用编译选项。

点表维护流程见 [JSON 生成说明](../app/protocol/config/README.md)。

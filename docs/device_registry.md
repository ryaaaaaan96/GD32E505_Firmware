# 应用设备按实例初始化

## 接口

```c
#include "app_usart.h"
aDevUsartHandle_t *usart = NULL;
aStatus_t status = appUsartInit(APP_USART_CONSOLE, &usart);

#include "app_led.h"
aDevLedHandle_t *led = NULL;
status = appLedInit(APP_LED_STATUS, &led);
```

每类设备只提供 Init(id, &handle) 一个应用访问接口，合并初始化与获取句柄。
第一次调用初始化指定实例；后续返回同一句柄或保存的失败结果。不保留 Open 兼容接口。
类型、编号、配置与静态存储均由 app/devices 管理，device 层保持单设备通用操作。
不依赖链接段或分散加载。

## 启动顺序与责任

main：aDrvInit → aOSInit → aSystemInit → aOSRun。
不做运行时跨设备引脚/DMA 等资源冲突检查，应用负责正确配置。后续可通过 Python 或 CMake 添加构建期告警，当前未实现。
设备接口自身的参数、能力检查和初始化错误处理继续保留。编号分别定义在 app_led.h 和 app_usart.h。

statusInit 内调用 appLedInit，然后创建任务；shellInit 内调用 appUsartInit，
然后配置 Shell。不存在集中初始化所有实例的 appDevicesInit。
各实例独立记录初始化结果，一个实例失败不自动初始化、回滚或使另一个实例失效。
当前 aSystemInit/main 遇到错误停止后续启动。

## 生命周期与错误

- 仅用于驱动/OS 就绪后的启动阶段单线程调用，禁止 ISR 或并发调用。
- NULL 输出指针返回 INVALID_PARAM；未知或未配置编号返回 NOT_FOUND。
- 失败清空输出指针；初始化失败保存原始错误，后续不自动重试。
- 初始化中重入同一实例返回 BUSY；状态标志不是线程同步机制。
- 成功返回共享静态生命周期句柄，不分配第二个实例、不重复配置硬件。
- 首次初始化可能分配 OS 内部资源；不宣称整个初始化过程无堆分配。
- 借用者不得反初始化、销毁或改变设备配置；无 Close/引用计数。
- 运行期持有或传递已获得句柄；设备操作并发规则由 aDev 接口负责。

Shell 关闭时 console 未配置，appUsartInit 返回 NOT_FOUND；USART 模块关闭时不编译其应用实现。

## 单一接口的取舍

优点是业务只需一次调用，不能忘记先 Init 再 Open，且只初始化选中的实例。
代价是无法表达“仅查询且禁止初始化”，首次调用耗时及副作用不同于后续调用；
初始化顺序由业务首次使用顺序决定。当前以启动阶段调用限制控制这些风险。
未来如需运行期并发首次初始化、热重配置或独立查询，必须另行设计同步和生命周期，
不能把当前幂等行为解释为线程安全。

## 验证

SANITIZE=1 python3 tests/app_devices/run.py 覆盖正常、LED/USART 失败、Shell 关闭，
检查按实例独立初始化、同实例重入、重复调用、输出清空、共享句柄及失败不重试。
硬件初始化由测试替身实现；构建测试不代表上板验证。

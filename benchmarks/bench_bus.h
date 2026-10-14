/* 主机和 MCU 共用的测量工作负载，不依赖 Shell 或具体计时硬件。 */
#ifndef BENCH_BUS_H
#define BENCH_BUS_H

#include "aBus.h"

typedef struct {
    const char *operation;
    size_t points;
    size_t bytes;
    uint32_t samples;
    uint32_t batch;
    uint32_t minimum;
    uint32_t median;
    uint32_t p99;
    uint32_t maximum;
    uint32_t clock_pair;
    uint32_t checksum;
} benchResult_t;

typedef struct {
    size_t points; /**< 1..2048；只创建一张独立测试表。 */
    size_t bytes; /**< 4 为 U32；其余 1..128 为 RAW。 */
    uint32_t samples; /**< 16..256；统计样本是批次内的平均每次操作耗时。 */
    uint32_t batch; /**< 1..64；每个批次必须小于计时器的一次回绕周期。 */
    uint32_t (*clock)(void); /**< 单调递增低 32 位，单位由调用者声明。 */
    void (*report)(const benchResult_t *result);
} benchBusConfig_t;

/* 不访问产品数据；正常及失败出口都释放测试数据和实例。
 * 回调只在测量批次之外运行；不关闭中断，不暂停其他任务。
 * 调用者串行执行，report 借用结果至返回。
 */
aStatus_t benchBusRun(const benchBusConfig_t *config);

#endif

#ifndef A_STREAM_H
#define A_STREAM_H

#include "aLib.h"

/**
 * @brief 同步字节流；仅描述操作，不拥有设备资源或创建任务。
 * read/write 返回 0..size 的实际长度（允许部分进度）；0 表示无进展/输入结束。
 * read/write 失败返回 -1 并设置 aOS 项目 errno：A_EAGAIN 为暂不可用，
 * A_ETIMEDOUT 为预算耗尽，其他值为具体错误。成功不清除历史 errno。
 * timeout 为本次调用总预算；NO_WAIT 只尝试当前状态。
 * 回调返回后不得保留 buffer；write 不得修改发送内容。
 * 默认任务上下文使用，不承诺 ISR 安全。
 * 使用者检查所需操作是否非空；具体设备由应用适配函数内部绑定。
 * 无上下文参数，同一套回调不区分多个实例；多实例须提供各自的适配函数。
 * 适配器持有的资源在所有使用者退出前必须有效；并发能力由适配器约定。
 * Linux 适配器必须转换 POSIX errno，不能直接套用其数值。
 */
typedef struct {
    aSSize_t (*read)(void *buffer, size_t size, aTimeout_t timeout);
    aSSize_t (*write)(const void *buffer, size_t size, aTimeout_t timeout);
    /** 可选缓冲输出提交操作，返回 aStatus_t；timeout 为本次提交总预算。
     * 非 NULL：write 可先缓存，flush 将待发送数据提交到实际输出接口。
     * NULL：write 已直接提交输出，无需额外刷新，调用者跳过 flush 即可。
     * OK 表示待发送数据已提交，不表示线路发送完成、持久化或对端收到。
     * 不丢弃输入；失败可已提交部分数据，剩余数据和重试规则由适配器明确。
     * 调用者须协调并发写入以建立明确的提交边界。
     */
    aStatus_t (*flush)(aTimeout_t timeout);
} aStream_t;

/** @brief 初始化为空流接口；stream 为 NULL 时不操作，不释放底层资源。 */
static inline void aStreamStructInit(aStream_t *stream)
{
    if (stream == NULL) return;
    stream->read = NULL;
    stream->write = NULL;
    stream->flush = NULL;
}

#endif

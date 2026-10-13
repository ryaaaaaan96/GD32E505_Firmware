/** @brief 通用通信通道：流式收发或按静默间隔分帧，不解释报文内容。 */
#ifndef A_COMM_CHANNEL_H
#define A_COMM_CHANNEL_H

#include "aComm.h"
#include "aStream.h"

typedef struct aCommChannel aCommChannel_t;
typedef enum {
    ACOMM_CHANNEL_STREAM, /**< 直接消费底层字节流，允许半包、粘包和 EOF。 */
    ACOMM_CHANNEL_GAP     /**< 按真实接收时间分帧，每次 read 不跨帧。 */
} aCommChannelMode_t;

/** 应用提供最终时间，单位为微秒；通道不推导或覆盖协议默认值。
 * character_us：时间戳在字符完成时采集，识别间隔需补一个字符时间。
 * byte_gap_us：帧内允许的最大空闲，0 表示不检查帧内间隔。
 * frame_gap_us：完整帧之间的最小静默，GAP 模式必须大于零。
 * 初始化时按 io.ticks_per_second 向上取整为计数，运行期只比较计数差。
 * 帧间隔与字符时间之和须在 32 位计数的半周期内，否则返回 UNSUPPORTED。
 * STREAM 模式不使用此配置。
 */
typedef struct {
    uint32_t character_us;
    uint32_t byte_gap_us;
    uint32_t frame_gap_us;
} aCommTiming_t;

/** GAP 时 ticks/enter/exit/wait_transmit_complete 必填。
 * ticks 固定频率、32 位自然回绕，不得暂停；enter/exit 排除接收生产者。
 * wait_transmit_complete 确认物理发送结束及方向引脚释放。
 * STREAM 时仅按需提供 discard_input、wait_transmit_complete。
 * context、函数和流在通道销毁前有效，端口生命周期仍由应用管理。
 */
typedef struct {
    void *context;
    uint32_t ticks_per_second;
    uint32_t (*ticks)(void *context);
    void (*enter)(void *context);
    void (*exit)(void *context);
    aStatus_t (*wait_transmit_complete)(void *context, aTimeout_t timeout);
    aStatus_t (*discard_input)(void *context, aTimeout_t timeout);
    void (*clear_error)(void *context);
} aCommChannelIo_t;

/** 完整帧校验在消费任务中调用，不在 ISR 中解释协议。
 * OK 接受；NOT_FOUND 忽略并继续等待（不重置预算）；其他状态结束本次读取。
 * 只做校验或筛选，不执行 I/O、不等待；data 只在回调期间借用。
 * 不能重入通道。NULL 接受所有完整帧。
 */
typedef aStatus_t (*aCommFrameCheckFn_t)(void *context,
    const void *data, size_t size);

typedef struct {
    aCommChannelMode_t mode;
    aStream_t stream; /**< STREAM 要求 read/write；GAP 只使用 write/flush。 */
    aCommChannelIo_t io;
    aCommTiming_t timing;
    aCommFrameCheckFn_t frame_check; /**< 仅 GAP 使用，由应用注册协议能力。 */
    void *frame_context;
} aCommChannelConfig_t;

/** 借用通道的通用传输接口；可注册给任意同步协议栈。
 * 所有调用由同一消费者串行执行，不创建任务锁。
 * GAP 操作结束调用 finish，仅释放当前帧，保留排队的后续帧。
 * STREAM 的 prepare_frame/finish 为 NULL；discard_input 和
 * wait_transmit_complete 只在底层提供对应能力时非空。
 */
typedef struct {
    aStream_t stream;
    void *context;
    aStatus_t (*discard_input)(void *context, aTimeout_t timeout);
    aStatus_t (*prepare_frame)(void *context, aTimeout_t timeout);
    aStatus_t (*wait_transmit_complete)(void *context, aTimeout_t timeout);
    void (*finish)(void *context);
} aCommTransport_t;

/** 默认 STREAM，需填充底层 read/write；不分配资源。 */
void aCommChannelConfigStructInit(aCommChannelConfig_t *config);
#if ACOMM_STATIC_ENABLE
/** 静态实例由应用持有，只能初始化尚未使用的实例。 */
aStatus_t aCommChannelInitStatic(const aCommChannelConfig_t *config,
    aCommChannel_t *handle);
aStatus_t aCommChannelDeInitStatic(aCommChannel_t *handle);
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommChannelCreate(const aCommChannelConfig_t *config,
    aCommChannel_t **handle_out);
aStatus_t aCommChannelDestroy(aCommChannel_t *handle);
#endif

/** GAP 单生产者入口，可在 ISR 调用；STREAM 模式不接受此入口。
 * 错误、容量耗尽或帧内超时丢弃整帧，满队列丢弃新帧。
 * 线程生产者必须与 io.enter/exit 使用同一同步机制。
 * 开启生产者前完成初始化，停止生产者和消费者后才能销毁。
 */
void aCommChannelReceive(void *context, uint8_t byte, aStatus_t status);
/** 时间戳来自真实采集时刻，与 io.ticks/aOSGetUptimeMs 同源。
 * 批量补送需持有生产者保护，并在消费者进入保护前交付全部待处理字节；
 * 不能按波特率推算时间或把普通流读取时间当作逐字节接收时间。
 */
void aCommChannelReceiveAt(aCommChannel_t *handle, uint8_t byte,
    aStatus_t status, uint32_t rx_ticks, uint32_t rx_ms);
/** 覆盖输出，借用至通道销毁；不得在 DeInit/Destroy 后继续调用。
 * STREAM 保留底层的部分进度与 EOF；GAP 耗尽当前帧返回 EIO 而非 EOF。
 * 无帧：NO_WAIT 为 EAGAIN，等待耗尽为 ETIMEDOUT。
 */
aStatus_t aCommChannelBindTransport(aCommChannel_t *handle,
    aCommTransport_t *transport);

#endif

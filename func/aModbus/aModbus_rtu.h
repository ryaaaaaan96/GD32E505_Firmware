/** @brief 通用 RTU 分帧传输；不依赖串口、DWT 或产品宏。 */
#ifndef A_MODBUS_RTU_H
#define A_MODBUS_RTU_H

#include "aModbus.h"

typedef struct aModbusRtuHandle aModbusRtuHandle_t;

/** 底层操作和 context 在 RTU 销毁前有效。
 * ticks 为固定频率的 32 位自由运行计数，可自然回绕，运行时不得暂停。
 * enter/exit 在消费任务中调用，必须排除接收生产者及其内存访问。
 * MCU 可屏蔽接收中断；线程生产者须与这两个回调使用同一同步机制。
 * wait_transmit_complete 须确认线路完成及 DE 释放。
 * clear_error 只清硬件接收错误，可为空；不得重入 RTU。 */
typedef struct {
    void *context;
    uint32_t ticks_per_second;
    uint32_t (*ticks)(void *context);
    void (*enter)(void *context);
    void (*exit)(void *context);
    aStatus_t (*wait_transmit_complete)(void *context, aTimeout_t timeout);
    void (*clear_error)(void *context);
} aModbusRtuIo_t;

typedef struct {
    aModbusRole_t role; /**< 与借用此传输的协议实例角色一致。 */
    uint8_t unit_id; /**< 从站地址，与协议实例一致；主站不在此过滤目标。 */
    uint8_t character_bits; /**< 起始位、数据、校验、停止位总和，8N1 为 10。 */
    uint32_t baud_rate;
    aModbusRtuIo_t io;
    /** 物理输出流：write 必填，flush 可为空；read 不使用。
     * 输入由 aModbusRtuReceive 或 aModbusRtuReceiveAt 提供。 */
    aStream_t output;
} aModbusRtuConfig_t;

void aModbusRtuConfigStructInit(aModbusRtuConfig_t *config);
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusRtuInitStatic(const aModbusRtuConfig_t *config,
                             aModbusRtuHandle_t *handle);
aStatus_t aModbusRtuDeInitStatic(aModbusRtuHandle_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusRtuCreate(const aModbusRtuConfig_t *config,
                         aModbusRtuHandle_t **handle_out);
aStatus_t aModbusRtuDestroy(aModbusRtuHandle_t *handle);
#endif

/** 单生产者入口；context 是已初始化的 RTU handle。
 * 可在 ISR 调用，不阻塞、不分配内存；每字节完成后立即调用。
 * ERROR 时 byte 无效并丢弃当前帧。接收线程必须先取得 io 同步保护。
 * 不能把普通串口批量读取后补送的字节当成真实逐字节时间戳。
 * 初始化须早于开启 RX；销毁前停止 RX 并等待所有协议调用退出。 */
void aModbusRtuReceive(void *context, uint8_t byte, aStatus_t status);
/**
 * @brief 按实际采集时间输入一个字节，复用逐字节分帧逻辑。
 * @param[in,out] handle 已初始化的实例；NULL 或未就绪时不操作。
 * @param[in] byte 接收字节；status 非 OK 时无效，丢弃当前帧。
 * @param[in] status 本字节的接收状态。
 * @param[in] rx_ticks 字符完成时的 io.ticks 计数，允许 32 位回绕。
 * @param[in] rx_ms 同次采集的 aOSGetUptimeMs 毫秒值，允许 32 位回绕。
 *
 * 不阻塞、不分配内存；与 Receive 共用单生产者及生命周期约束。
 * 两个时间必须来自与当前 io.ticks/aOS 相同的时基，按采集顺序交付，
 * 不得用补送时刻或按波特率推算的时间代替；仅接收初始化后的数据。
 * 批量补送须在同一 io 同步保护内完成。消费及发送辅助操作取得保护时，
 * 适配层必须已提交全部待交付数据，否则按当前时间检查静默会提前截帧。
 * 不能满足此约束或仅有完整帧边界时，使用自定义 aModbusTransport_t。
 */
void aModbusRtuReceiveAt(aModbusRtuHandle_t *handle, uint8_t byte,
                       aStatus_t status, uint32_t rx_ticks, uint32_t rx_ms);
/** 读取当前帧或提交物理输出；流回调由 BindTransport 统一适配。
 * 正长度 Read 不跨帧，当前帧耗尽后再读返回 -1/A_EIO，不表示流 EOF。
 * 无帧时按 timeout 返回 -1/A_EAGAIN 或 -1/A_ETIMEDOUT。
 * 仅任务上下文，同一实例由一个协议消费者串行调用。 */
aSSize_t aModbusRtuRead(aModbusRtuHandle_t *handle, void *data, size_t size,
                      aTimeout_t timeout);
aSSize_t aModbusRtuWrite(aModbusRtuHandle_t *handle, const void *data,
                       size_t size, aTimeout_t timeout);
/** 填写完整 transport，覆盖原有流及辅助操作；无须应用提供帧适配函数。
 * stream.context 与 transport.context 均绑定 handle；可选 flush
 * 转交物理 output.context，物理流无 flush 时保持 NULL。
 * 协议只借用实例；销毁前先停止消费者和接收生产者。
 */
aStatus_t aModbusRtuBindTransport(aModbusRtuHandle_t *handle,
                                aModbusTransport_t *transport);

#endif

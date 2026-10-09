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
     * 输入由 aModbusRtuReceive 提供，防止批量读取丢失逐字节时序。 */
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
/** 将已分帧的输入和物理输出适配成 aStream 的无 context 回调。
 * 应用为每个端口分别提供薄适配函数，转调对应实例的 Read/Write。
 * 仅任务上下文，同一实例由一个协议消费者串行调用。 */
aSSize_t aModbusRtuRead(aModbusRtuHandle_t *handle, void *data, size_t size,
                      aTimeout_t timeout);
aSSize_t aModbusRtuWrite(aModbusRtuHandle_t *handle, const void *data,
                       size_t size, aTimeout_t timeout);
/** 补充 RTU 辅助操作，保留应用已设置的 stream.read/write/flush。
 * read/write 必填；flush 须对应 config.output.flush，可同时为空。
 * 协议只借用实例；销毁前先停止消费者和接收生产者。
 */
aStatus_t aModbusRtuBindTransport(aModbusRtuHandle_t *handle,
                                aModbusTransport_t *transport);

#endif

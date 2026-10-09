#ifndef APP_RS485_DEVICE_H
#define APP_RS485_DEVICE_H

#include "aStream.h"

/* 每字节完成后在 ISR 中调用；错误时 byte 无效。
 * context 从 Open 前就绪，直到 Close 成功后才可释放。
 */
typedef void (*rs485ReceiveFn_t)(void *context, uint8_t byte,
                               aStatus_t status);

/* 板载串口能力，只描述硬件操作，不携带协议类型或协议实例。
 * output 只提供写入，接收通过 ISR 回调交付，以保留字节间隔。
 * 时基固定频率、32 位自然回绕；运行期不得修改内核频率或暂停计数。
 * enter/exit 排除 RX ISR；操作中的 context 保留，当前板端不使用。
 */
typedef struct {
    aStream_t output;
    uint32_t baud_rate;
    uint32_t ticks_per_second;
    uint8_t character_bits;
    uint32_t (*ticks)(void *context);
    void (*enter)(void *context);
    void (*exit)(void *context);
    aStatus_t (*wait_transmit_complete)(void *context, aTimeout_t timeout);
    void (*clear_error)(void *context);
} rs485Port_t;

/* 准备时基并填写能力；尚未打开串口或占用接收者，不分配协议内存。 */
aStatus_t rs485PortPrepare(rs485Port_t *port);
/* 独占打开 USART2；回调在接口返回前就可能执行。 */
aStatus_t rs485PortOpen(rs485ReceiveFn_t receive, void *context);
/* 先停止使用方；BUSY 时保留串口和回调，允许重试。 */
aStatus_t rs485PortClose(void);

#endif

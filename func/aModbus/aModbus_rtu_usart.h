/** @brief 可选 USART + 内核周期计数适配；应用只提供设备和协议配置。 */
#ifndef A_MODBUS_RTU_USART_H
#define A_MODBUS_RTU_USART_H

#include "aModbus_rtu.h"
#include "aDev_usart.h"

typedef struct aModbusRtuUsartHandle aModbusRtuUsartHandle_t;

typedef struct {
    aModbusRole_t role;
    uint8_t unit_id;
    /** RX 必须为 INTERRUPT_CALLBACK，回调及 context 由模块设置。
     * TX 缓冲区由应用提供并保持有效至销毁；串口配置只在初始化时借用。
     * 不允许同时登记其他字节回调；DE 继续由 aDevUsart 管理。 */
    aDevUsartConfig_t usart;
} aModbusRtuUsartConfig_t;

void aModbusRtuUsartConfigStructInit(aModbusRtuUsartConfig_t *config);
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusRtuUsartInitStatic(const aModbusRtuUsartConfig_t *config,
                                  aModbusRtuUsartHandle_t *handle);
aStatus_t aModbusRtuUsartDeInitStatic(aModbusRtuUsartHandle_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusRtuUsartCreate(const aModbusRtuUsartConfig_t *config,
                              aModbusRtuUsartHandle_t **handle_out);
aStatus_t aModbusRtuUsartDestroy(aModbusRtuUsartHandle_t *handle);
#endif
/** 一个传输实例仅供一个协议实例借用；先销毁协议，再销毁传输。
 * 销毁时先关闭 RX，再释放 RTU 状态；BUSY 时实例保留，可重试。
 * 静态/动态表示适配实例的存储；串口存储遵循 aDev 分配配置。
 * 通信时内核频率固定，且不进入会暂停周期计数的休眠或调试状态。 */
aStatus_t aModbusRtuUsartGetTransport(aModbusRtuUsartHandle_t *handle,
                                    aModbusTransport_t *transport);

#endif

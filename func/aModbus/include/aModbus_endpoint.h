/** @brief aComm 接入入口；协议实例内嵌公共端点，无总线实现依赖。 */
#ifndef AMODBUS_ENDPOINT_H
#define AMODBUS_ENDPOINT_H
#include "aModbus_client.h"
#include "aModbus_server.h"
#include "aComm_endpoint.h"
#include "aComm_channel.h"
#define AMODBUS_WORD_HIGH_FIRST 0U
#define AMODBUS_WORD_LOW_FIRST 1U

/** 返回协议实例中的公共首成员，不分配独立端点。
 * NULL 返回 NULL；配置检查直接使用 Modbus 规则，不需要伪实例。
 */
aCommEndpoint_t *aModbusCommEndpoint(aModbusHandle_t *handle);
/** 只转交通道回调，不持有或销毁通道。 */
aStatus_t aModbusBindChannel(aCommChannel_t *channel,
    aModbusTransport_t *transport);
#endif

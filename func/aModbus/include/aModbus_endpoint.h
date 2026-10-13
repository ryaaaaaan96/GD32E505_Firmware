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
 * NULL 返回只供配置检查使用的规则端点，不能执行事务。
 */
aCommEndpoint_t *aModbusCommEndpoint(aModbusHandle_t *handle);
/** 只转交通道回调，不持有或销毁通道。 */
aStatus_t aModbusBindChannel(aCommChannel_t *channel,
    aModbusTransport_t *transport);
#endif

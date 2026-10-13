/** @brief 可选 aComm 端点；协议核心可单独使用，无总线依赖。 */
#ifndef AMODBUS_ENDPOINT_H
#define AMODBUS_ENDPOINT_H
#include "aModbus_client.h"
#include "aModbus_server.h"
#include "aComm_endpoint.h"
#include "aComm_channel.h"
#define AMODBUS_WORD_HIGH_FIRST 0U
#define AMODBUS_WORD_LOW_FIRST 1U

typedef struct {
    aCommEndpoint_t base; /**< 首成员，只有本模块恢复具体对象。 */
    aModbusHandle_t *protocol; /**< 借用；NULL 仅用于配置预检查及从站映射。 */
    aCommAddressReadFn_t read;
    aCommAddressWriteFn_t write;
    void *context; /**< 从站地址回调上下文。 */
} aModbusEndpoint_t;
/** 不分配、不创建锁；调用后由应用按需注册从站回调。 */
void aModbusEndpointInit(aModbusEndpoint_t *endpoint,
    aModbusHandle_t *protocol);
aStatus_t aModbusEndpointServerRead(void *context,
    const aModbusAddressReadRequest_t *request);
aStatus_t aModbusEndpointServerWrite(void *context,
    const aModbusAddressWriteRequest_t *request);
/** 只转交通道回调，不持有或销毁通道。 */
aStatus_t aModbusBindChannel(aCommChannel_t *channel,
    aModbusTransport_t *transport);
#endif

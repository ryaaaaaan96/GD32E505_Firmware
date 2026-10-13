#ifndef PROTOCOL_INTERNAL_H
#define PROTOCOL_INTERNAL_H
#include "aComm_mapping.h"
#include "aModbus_endpoint.h"
#include "aBus_endpoint.h"

/* 产品分别描述协议和业务映射；实例、任务及生命周期由 protocol.c 装配。 */
typedef struct {
    aModbusClientConfig_t protocol;
    aCommMappingClientConfig_t client;
    aCommTiming_t timing; /**< 最终通道时间，单位为微秒，不自动覆盖。 */
    uint16_t retry_count;
    uint32_t interval_ms;
    uint32_t error_delay_ms;
} protocolClientConfig_t;

typedef struct {
    aModbusServerConfig_t protocol;
    aCommMappingServerConfig_t server;
    aModbusServerProcessRequest_t process;
    aCommTiming_t timing; /**< 最终通道时间，单位为微秒，不自动覆盖。 */
    uint32_t error_delay_ms;
} protocolServerConfig_t;
#endif

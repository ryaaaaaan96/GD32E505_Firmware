/** @brief 可选通信端点；不改变 aBus 核心的独立使用方式。 */
#ifndef ABUS_ENDPOINT_H
#define ABUS_ENDPOINT_H
#include "aBus.h"
#include "aComm_endpoint.h"
typedef struct {
    aCommEndpoint_t base; /**< 首成员，仅本模块内部恢复具体对象。 */
    aBusHandle_t *bus; /**< 借用，所有映射销毁后才可释放总线。 */
} aBusEndpoint_t;
#if ABUS_EVENT_ENABLE
/** 静态映射由应用提供数组；动态映射按 subscription_size 一次分配。 */
typedef struct {
    aBusSubscriber_t node;
    aBusEndpoint_t *endpoint;
    aCommSubscriptionConfig_t config;
} aBusEndpointSubscription_t;
#endif
/** 不分配内存、不增加锁；仅绑定已初始化的数据实例。 */
aStatus_t aBusEndpointInit(aBusEndpoint_t *endpoint, aBusHandle_t *bus);
#endif

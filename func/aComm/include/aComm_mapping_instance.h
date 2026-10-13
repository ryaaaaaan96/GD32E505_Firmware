/** @brief 静态实例布局；初次使用须零初始化，运行后禁止复制或移动。 */
#ifndef ACOMM_MAPPING_INSTANCE_H
#define ACOMM_MAPPING_INSTANCE_H
#include "aComm_mapping.h"
#include "aAtomic.h"
/* 只为订阅项提供运行槽和端点原生节点，未订阅项不占 RAM。 */
typedef struct {
    void *native;
    const void *definition;
    aCommMapping_t *owner;
    aAtomicBool_t busy;
    aCommNotifyInput_t input;
    aCommRequest_t work;
} aCommMappingSubscription_t;
struct aCommMapping {
    aCommMappingSubscription_t *subscriptions; /**< 静态创建前由应用填入。 */
    size_t subscription_capacity;
    size_t subscription_count;
    /* 主从配置共用存储；config 指向当前角色的 binding 成员。 */
    union {
        aCommMappingClientConfig_t client;
        aCommMappingServerConfig_t server;
    };
    aCommMappingConfig_t *config;
    aCommMappingRole_t role;
    size_t plan_count;
    aTimepoint_t deadline;
    aBool_t ready;
    aBool_t dynamic;
    aAtomicBool_t active;
    _Alignas(max_align_t) uint8_t snapshot[ACOMM_VALUE_CAPACITY];
};
/* 静态计划帧数组可按采集项数提供上限，实际帧数初始化后查询。 */
typedef struct {
    size_t first;
    size_t count;
    uint16_t quantity;
    uint32_t poll_time_ms;
    aCommPrepared_t request; /**< 由协议栈准备的固定读取请求。 */
} aCommPlanFrame_t;
struct aCommPlan {
    aCommPlanFrame_t *frames;
    size_t *indices; /**< 启用项在 config.items 中的下标，不复制只读映射。 */
    size_t capacity; /**< frames 数组容量。 */
    size_t index_capacity; /**< indices 数组容量。 */
    aCommPlanConfig_t config;
    aCommMapping_t *owner;
    size_t count;
    aBool_t ready;
    aBool_t dynamic;
};
#endif

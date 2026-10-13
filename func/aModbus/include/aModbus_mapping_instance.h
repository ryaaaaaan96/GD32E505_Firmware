/** @brief 静态实例布局；初次使用须零初始化，运行后禁止复制或移动。 */
#ifndef AMODBUS_MAPPING_INSTANCE_H
#define AMODBUS_MAPPING_INSTANCE_H
#include "aModbus_mapping.h"
#include "aAtomic.h"
#include "aComm_instance.h"
struct aModbusMapping {
    aCommBinding_t comm; /**< 通用数据工作区、订阅槽和事件请求由 aComm 管理。 */
    /* 主从配置共用存储；config 指向当前角色的 binding 成员。 */
    union {
        aModbusMappingClientConfig_t client;
        aModbusMappingServerConfig_t server;
    };
    aModbusMappingConfig_t *config;
    aModbusMappingRole_t role;
    size_t plan_count;
    aTimepoint_t deadline;
    aBool_t ready;
    aBool_t dynamic;
    aAtomicBool_t active;
};
#endif

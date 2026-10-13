#ifndef ACOMM_TABLE_INTERNAL_H
#define ACOMM_TABLE_INTERNAL_H
#include "aComm_instance.h"
/* 调度实例与独立计划共用的存储入口，不受对外分配开关影响。 */
aStatus_t aCommPlanMeasure(const aCommPlanConfig_t *config,
    size_t *frames, size_t *items, size_t *groups);
aStatus_t aCommPlanBind(const aCommPlanConfig_t *config, aCommPlan_t *plan);
aStatus_t aCommPlanUnbind(aCommPlan_t *plan);
#endif

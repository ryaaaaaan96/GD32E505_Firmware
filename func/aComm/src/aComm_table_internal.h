#ifndef ACOMM_TABLE_INTERNAL_H
#define ACOMM_TABLE_INTERNAL_H
#include "aComm_instance.h"
/* 调度实例与独立计划共用的存储入口，不受对外分配开关影响。 */
typedef struct {
    size_t frames;
    size_t items;
    size_t groups;
} aCommPlanSize_t;
aStatus_t aCommPlanMeasure(const aCommPlanConfig_t *config,
    aCommPlanSize_t *size);
/* 仅供已测量并验证配置的内部初始化路径调用。 */
aStatus_t aCommPlanBindMeasured(const aCommPlanConfig_t *config,
    aCommPlan_t *plan, const aCommPlanSize_t *size);
aStatus_t aCommPlanUnbind(aCommPlan_t *plan);
#endif

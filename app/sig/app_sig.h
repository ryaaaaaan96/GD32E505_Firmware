#ifndef APP_SIG_H
#define APP_SIG_H

#include "aBus.h"
#include <stdint.h>
#include "app_sig_ids.h"

typedef struct {
    uint32_t speed;
    int32_t temperature;
} appBusMotor_t;

/* 静态对象按应用设备号与下标注册，由 aBus 统一收集。
 * 绑定只关联存储，直接访问绑定变量的并发同步由应用自行决定。
 */
#define APP_SIG_BIND(name, index, object) \
    ABUS_STORAGE_EXPORT(name, APP_SIG_INSTANCE_ID, APP_SIG_DEVICE_ID, \
                        index, object)

/* 仅启动阶段单线程调用一次，先完成 aOS 初始化。 */
aStatus_t appSigInit(void);
/* 通用整组读写：sigIndex 为 appBusSigIndex_t 下标，不是 sigKey。
 * 仅任务上下文；未初始化返回 NOT_READY；未知下标返回 NOT_FOUND。
 * 请求和缓冲区仅在调用期间借用，长度必须与 SIG 完全一致。
 * timeout 仅用于 aBus 锁等待；不保护应用对绑定变量的直接访问。
 */
/* 按已挂载表查询类型和长度；不暴露表和 handle。 */
aStatus_t appSigGetInfo(const aBusSigQuery_t *query, aBusSigInfo_t *info);
aStatus_t appSigSet(const aBusSetIndexRequest_t *request);
aStatus_t appSigGet(const aBusGetIndexRequest_t *request);

/* STRUCT 字段访问，共用 aBus 的整组锁及范围规则。 */
aStatus_t appSigSetParam(const aBusSetParamRequest_t *request);
aStatus_t appSigGetParam(const aBusGetParamRequest_t *request);

#endif

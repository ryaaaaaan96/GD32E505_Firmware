#ifndef APP_DATA_BUS_SERVICE_H
#define APP_DATA_BUS_SERVICE_H

#include "aBus.h"

/* 产品提供点表；服务私有持有运行实例，不依赖具体测点或设备号。 */
typedef struct {
    uint16_t instanceID;
    const aBusTable_t *tables;
    size_t table_count;
#if !ABUS_DYNAMIC_ENABLE
    aBusHandle_t *instance; /**< 已用 aBusInstanceStructInit 配置的静态实例。 */
#endif
} dataBusConfig_t;

static inline void dataBusConfigStructInit(dataBusConfig_t *config)
{
    if (config != NULL) {
        const dataBusConfig_t initial = {.tables = NULL};
        *config = initial;
    }
}

/* 仅启动阶段单线程调用一次；重复初始化返回 BUSY。
 * config 仅调用期间借用；点表、默认值及静态实例须在应用运行期间有效。
 * 动态功能开启时分配实例元数据，否则使用 config->instance。
 */
aStatus_t dataBusInit(const dataBusConfig_t *config);

/* 仅任务上下文；未初始化返回 NOT_READY。查询不暴露 handle 或数据地址。
 * Shell 等外部入口先解析稳定键，业务可直接使用枚举下标。
 */
aStatus_t dataBusResolveKey(const aBusSigKeyQuery_t *query,
    size_t *sigIndex);
aStatus_t dataBusGetInfo(const aBusSigQuery_t *query, aBusSigInfo_t *info);

/* 按下标读写整组，长度必须完全一致；请求和缓冲区仅在调用期间借用。
 * timeout 仅控制 aBus 锁等待，不保护直接访问绑定变量的代码。
 */
aStatus_t dataBusSet(const aBusSetIndexRequest_t *request);
aStatus_t dataBusGet(const aBusGetIndexRequest_t *request);

/* STRUCT 字段访问，共用 SIG 锁和范围规则，其他字段保持原值。 */
aStatus_t dataBusSetParam(const aBusSetParamRequest_t *request);
aStatus_t dataBusGetParam(const aBusGetParamRequest_t *request);

#endif

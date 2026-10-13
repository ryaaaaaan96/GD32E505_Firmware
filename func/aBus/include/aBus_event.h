/** @file aBus_event.h
 * @brief 通用关联、非阻塞通知和异步请求。仅任务上下文。
 * 注册/解除注册仅在启动或停止所有调用者后执行，不支持并发热插拔。
 * 回调禁止等待、I/O、动态分配及自旋，只设置标记或尝试登记工作。
 */
#ifndef ABUS_EVENT_H
#define ABUS_EVENT_H

#include "aBus_table.h"

#ifndef ABUS_EVENT_ENABLE
#define ABUS_EVENT_ENABLE 1
#endif
#if ABUS_EVENT_ENABLE != 0 && ABUS_EVENT_ENABLE != 1
#error "ABUS_EVENT_ENABLE must be 0 or 1"
#endif

#define ABUS_SIG_WHOLE SIZE_MAX
#define ABUS_EVENT_UPDATED (UINT32_C(1) << 0)
#define ABUS_EVENT_WRITE_COMPLETE (UINT32_C(1) << 1)
#define ABUS_REQUEST_READ (UINT32_C(1) << 2)
#define ABUS_REQUEST_WRITE (UINT32_C(1) << 3)
#define ABUS_ALL_EVENTS (ABUS_EVENT_UPDATED | ABUS_EVENT_WRITE_COMPLETE | \
                         ABUS_REQUEST_READ | ABUS_REQUEST_WRITE)
#define ABUS_SUBSCRIBE_MARK_ONLY (UINT32_C(1) << 31)
#define ABUS_PROVIDER_ANY 0U
#define ABUS_PROVIDER_MODBUS 1U
#define ABUS_PROVIDER_DATABASE 2U
#define ABUS_PROVIDER_APPLICATION 3U

/* 定位信息不包含注册策略；配置目标的前三级含义与它一致。 */
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    size_t paramIndex;
} aBusTarget_t;

typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    size_t paramIndex;
    uint32_t subscribe_events;
    uint32_t publish_events;
} aBusTargetConfig_t;

static inline void aBusTargetStructInit(aBusTarget_t *target)
{
    if (target != NULL) {
        const aBusTarget_t defaults = {.paramIndex = ABUS_SIG_WHOLE};
        *target = defaults;
    }
}

static inline void aBusTargetConfigStructInit(aBusTargetConfig_t *target)
{
    if (target != NULL) {
        const aBusTargetConfig_t defaults = {.paramIndex = ABUS_SIG_WHOLE};
        *target = defaults;
    }
}

/** 配置检查与事件编译开关独立，关闭事件后仍可检查产品定义。 */
static inline aStatus_t aBusTargetConfigCheck(const aBusTargetConfig_t *target)
{
    if (target == NULL) return A_STATUS_INVALID_PARAM;
    uint32_t mask = target->subscribe_events;
    if ((mask != 0U && mask != ABUS_SUBSCRIBE_MARK_ONLY &&
         (mask & ~ABUS_ALL_EVENTS) != 0U) ||
        (target->publish_events & ~ABUS_EVENT_WRITE_COMPLETE) != 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}

#if ABUS_EVENT_ENABLE
#include "aAtomic.h"

typedef struct aBusHandle aBusHandle_t;
typedef struct aBusSubscriber aBusSubscriber_t;
typedef struct aBusRequest aBusRequest_t;
typedef struct aBusDelivery aBusDelivery_t;

typedef struct {
    size_t offset; /**< 相对整组 SIG，而不是相对请求数据。 */
    size_t size;
} aBusSpan_t;

typedef struct {
    size_t matched;
    size_t accepted;
    size_t rejected;
    aStatus_t status; /**< 首个投递故障；不表示后台执行结果。 */
} aBusDispatchResult_t;

typedef struct {
    aBusHandle_t *bus;
    aBusTarget_t target; /**< 原始目标，完整保留。 */
    uint32_t event;
    aBusSpan_t source; /**< 原始操作范围。 */
    aBusSpan_t binding; /**< 接收节点的范围。 */
    aBusSpan_t overlap; /**< 本节点只处理该交集，不扩大写入。 */
    const aBusSubscriber_t *subscriber;
    const void *definition;
    const void *data; /**< 写请求的完整原始数据；普通通知不携带快照。 */
    aStatus_t status; /**< 观察性完成通知的执行状态。 */
    aBusDelivery_t *delivery; /**< 请求的稳定凭据；通知时为 NULL。 */
} aBusNotifyInput_t;

typedef struct {
    aBool_t accepted;
} aBusNotifyOutput_t;

typedef aStatus_t (*aBusNotifyFn_t)(void *context,
    const aBusNotifyInput_t *input, aBusNotifyOutput_t *output);

typedef struct {
    uint32_t event_mask;
    uint32_t type;
    const void *owner;
    const void *definition;
    aBusNotifyFn_t notify;
    void *context;
} aBusSubscriberConfig_t;

/** 节点由提供者持有，初始化后不移动。业务只读 config，不修改 next/bus/list。
 * 单个节点只挂一条链；同实例重叠映射会分别接收，不按 owner 去重。
 */
struct aBusSubscriber {
    aBusSubscriberConfig_t config;
    aBusSubscriber_t *next;
    aBusHandle_t *bus;
    aBusSubscriber_t **list; /**< 所属链表头；避免重复保存目标下标。 */
};

typedef struct {
    aBusTarget_t target;
    uint32_t event;
    const void *owner; /**< NULL 多播；非 NULL 仅匹配该提供者实例。 */
    aStatus_t status;
} aBusPublishRequest_t;

/** 完成回调必须非阻塞，可早于 aBusRequest 返回；不同接收者可并发完成。
 * input 在回调期间借用。回调返回后该处理者不得访问请求及数据。
 */
typedef void (*aBusCompleteFn_t)(void *context,
    const aBusNotifyInput_t *input, aStatus_t status);

typedef struct {
    aBusTarget_t target;
    uint32_t event; /**< READ 或 WRITE，单个请求只选一种。 */
    const void *owner;
    const void *data; /**< WRITE 必填，READ 必须为 NULL。 */
    size_t size; /**< WRITE 必须等于原始目标长度，READ 为零。 */
    aBusCompleteFn_t complete;
    void *context;
} aBusRequestConfig_t;

/** 调用者预分配凭据数组，每个匹配接收者占一项，拒绝项也占本轮槽位。
 * 容量不足的节点返回 BUSY，其他节点照常投递；不在运行时申请堆。
 * pending 包含分发保护，Done 为真后才允许复用对象/数组/写入数据。
 */
struct aBusRequest {
    aBusDelivery_t *deliveries;
    size_t capacity;
    aBusRequestConfig_t config;
    aAtomicUint_t pending;
};

struct aBusDelivery {
    aBusNotifyInput_t input;
    aBusRequest_t *request;
    aAtomicBool_t pending;
};

typedef struct {
    aBusTarget_t target;
    uint32_t type; /**< ANY 表示不过滤类别。 */
    const void *owner;
    size_t index; /**< 所选精确链表中匹配项的序号，不做范围广播。 */
} aBusAssociationQuery_t;

void aBusSubscriberStructInit(aBusSubscriber_t *node);
void aBusSubscriberConfigStructInit(aBusSubscriberConfig_t *config);
void aBusPublishRequestStructInit(aBusPublishRequest_t *request);
void aBusRequestConfigStructInit(aBusRequestConfig_t *config);
void aBusRequestStructInit(aBusRequest_t *request,
    aBusDelivery_t *deliveries, size_t capacity);
aBool_t aBusRequestDone(const aBusRequest_t *request);
aStatus_t aBusSubscribe(aBusHandle_t *handle, const aBusTarget_t *target,
    aBusSubscriber_t *node, const aBusSubscriberConfig_t *config);
aStatus_t aBusUnsubscribe(aBusSubscriber_t *node);
/** 精确查询同时返回普通订阅与仅标记节点；不存在返回 NOT_FOUND。 */
aStatus_t aBusAssociationGet(aBusHandle_t *handle,
    const aBusAssociationQuery_t *query, aBusSubscriberConfig_t *info);
/** 多于一个匹配返回 BUSY，禁止隐式选第一项。 */
aStatus_t aBusAssociationFind(aBusHandle_t *handle,
    const aBusAssociationQuery_t *query, aBusSubscriberConfig_t *info);
/** result 可为 NULL；无人接收为 OK，matched 为零，不触发完成回调。 */
aStatus_t aBusPublish(aBusHandle_t *handle,
    const aBusPublishRequest_t *request, aBusDispatchResult_t *result);
aStatus_t aBusRequest(aBusHandle_t *handle,
    const aBusRequestConfig_t *config, aBusRequest_t *request,
    aBusDispatchResult_t *result);
/** 已接收凭据恰好完成一次；重复调用返回 NOT_READY。 */
aStatus_t aBusComplete(aBusDelivery_t *delivery, aStatus_t status);
/** 取出并清除自动 UPDATED 的投递失败标记，不打印、不阻塞。 */
aBool_t aBusNotificationFailed(aBusHandle_t *handle);
#endif
#endif

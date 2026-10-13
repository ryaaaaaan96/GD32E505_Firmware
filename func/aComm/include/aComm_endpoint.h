/** @brief 通信端点契约；仅依赖 aLib，不依赖调度器或任何具体协议。 */
#ifndef ACOMM_ENDPOINT_H
#define ACOMM_ENDPOINT_H
#include "aDataLayout.h"
#include "aLib.h"

#define ACOMM_DATA_WHOLE SIZE_MAX
#define ACOMM_EVENT_UPDATED (UINT32_C(1) << 0)
#define ACOMM_EVENT_WRITE_COMPLETE (UINT32_C(1) << 1)
#define ACOMM_REQUEST_READ (UINT32_C(1) << 2)
#define ACOMM_REQUEST_WRITE (UINT32_C(1) << 3)
#define ACOMM_SUBSCRIBE_MARK_ONLY (UINT32_C(1) << 31)
#ifndef ACOMM_VALUE_CAPACITY
#define ACOMM_VALUE_CAPACITY 256U
#endif
#ifndef ACOMM_PREPARED_CAPACITY
#define ACOMM_PREPARED_CAPACITY 64U
#endif

typedef struct aCommEndpoint aCommEndpoint_t;
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    size_t paramIndex;
} aCommTarget_t;
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    size_t paramIndex;
    uint32_t subscribe_events;
    uint32_t publish_events;
} aCommTargetConfig_t;
typedef struct { uint16_t deviceID; size_t sigIndex; } aCommDataQuery_t;
typedef struct {
    aDataType_t type;
    size_t size;
    const aDataRange_t *range;
    const aDataField_t *params;
    size_t param_count;
} aCommDataInfo_t;
typedef struct {
    aDataType_t type;
    size_t size;
    const aDataRange_t *range;
    uint16_t quantity;
    const aDataField_t *params;
    size_t param_count;
} aCommValueInfo_t;
typedef struct {
    uint8_t exception; /**< 端点提供的错误详情，通用层不解释。 */
    aBool_t started;
    aBool_t retryable;
} aCommEndpointResult_t;
typedef struct {
    const void *target; /**< 只由注册的端点解释，调用期间借用。 */
    void *data;
    size_t size;
    aTimeout_t timeout;
    aCommEndpointResult_t *result;
} aCommReadAccess_t;
typedef struct {
    const void *target;
    const void *data;
    size_t size;
    aTimeout_t timeout;
    aCommEndpointResult_t *result;
} aCommWriteAccess_t;
typedef struct {
    uint8_t unit_id;
    uint16_t area;
    uint16_t address;
    uint16_t quantity;
} aCommAddress_t;
typedef struct {
    uint16_t area, address, quantity;
    void *data;
    size_t size;
    aTimeout_t timeout;
} aCommAddressReadRequest_t;
typedef struct {
    uint16_t area, address, quantity;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aCommAddressWriteRequest_t;
typedef aStatus_t (*aCommAddressReadFn_t)(void *,
    const aCommAddressReadRequest_t *);
typedef aStatus_t (*aCommAddressWriteFn_t)(void *,
    const aCommAddressWriteRequest_t *);
typedef struct {
    aCommDataQuery_t target;
    void *buffer;
    size_t capacity;
    aStatus_t (*edit)(void *, void *, size_t);
    void *context;
    aTimeout_t timeout;
} aCommDataUpdate_t;
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    void *dst;
    size_t size;
    aTimeout_t timeout;
    size_t paramIndex;
} aCommDataRead_t;
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    const void *src;
    size_t size;
    aTimeout_t timeout;
    size_t paramIndex;
} aCommDataWrite_t;
typedef struct { size_t offset, size; } aCommSpan_t;
typedef struct {
    aCommEndpoint_t *endpoint;
    aCommTarget_t target;
    uint32_t event;
    aCommSpan_t source, overlap;
    const void *data;
    aStatus_t status;
    void *delivery; /**< 稳定完成凭据，由端点实现解释。 */
} aCommNotifyInput_t;
typedef struct { aBool_t accepted; } aCommNotifyOutput_t;
typedef aStatus_t (*aCommNotifyFn_t)(void *, const aCommNotifyInput_t *,
    aCommNotifyOutput_t *);
typedef struct {
    aCommTarget_t target;
    uint32_t event;
    aStatus_t status;
} aCommPublishRequest_t;
typedef struct {
    uint32_t events;
    uint32_t type; /**< 关联查询的业务标签，通用层不据此分派。 */
    const void *owner;
    const void *definition;
    aCommNotifyFn_t notify;
    void *context;
} aCommSubscriptionConfig_t;

/** 数据端可选能力；update 必须覆盖读取、编辑及提交的同步区间。 */
typedef struct {
    aStatus_t (*info)(aCommEndpoint_t *, const aCommDataQuery_t *,
        aCommDataInfo_t *);
    aStatus_t (*update)(aCommEndpoint_t *, const aCommDataUpdate_t *);
    size_t subscription_size; /**< 原生订阅对象大小，动态创建统一分配。 */
    aStatus_t (*subscribe)(aCommEndpoint_t *, const aCommTarget_t *, void *,
        const aCommSubscriptionConfig_t *);
    aStatus_t (*unsubscribe)(void *);
    aStatus_t (*publish)(aCommEndpoint_t *, const aCommPublishRequest_t *);
    aStatus_t (*complete)(void *, aStatus_t);
} aCommDataOps_t;

/** 线性地址端的协议规则；回调不修改映射，不执行调度。
 * unit_bits 为 1 或 8 的倍数；缓冲区为端点约定的数据表示。
 * copy 处理单位区间与业务值的转换，write 表示解码写入业务值。
 * prepare/read_prepared 可选，预编码对象是字节表示，禁止强转为私有结构。
 */
typedef struct {
    uint16_t unit_bits;
    uint16_t read_limit, write_limit;
    uint32_t address_limit;
    aBool_t writable;
} aCommAreaInfo_t;
typedef struct {
    uint8_t data[ACOMM_PREPARED_CAPACITY];
    size_t size;
    aCommAddress_t address;
} aCommPrepared_t;
typedef struct {
    aStatus_t (*ready)(aCommEndpoint_t *);
    aStatus_t (*area)(aCommEndpoint_t *, uint16_t, aCommAreaInfo_t *);
    aStatus_t (*check)(aCommEndpoint_t *, const aCommAddress_t *,
        const void *, size_t, aBool_t);
    aStatus_t (*describe)(const aCommDataInfo_t *, const aCommTarget_t *,
        uint16_t, uint8_t, aCommValueInfo_t *);
    aStatus_t (*layout)(const aCommValueInfo_t *);
    aStatus_t (*copy)(const aCommValueInfo_t *, uint16_t, uint8_t,
        size_t, size_t, void *, void *, aBool_t);
    aStatus_t (*slice)(const aCommDataInfo_t *, size_t, uint16_t *, uint8_t *);
    aBool_t (*merge)(aCommEndpoint_t *, const aCommAddress_t *,
        const aCommAddress_t *);
    aStatus_t (*prepare)(aCommEndpoint_t *, const aCommAddress_t *,
        aCommPrepared_t *);
    aStatus_t (*read_prepared)(aCommEndpoint_t *, const aCommPrepared_t *,
        const aCommReadAccess_t *);
    uint32_t provider_type;
} aCommAddressOps_t;

typedef struct {
    aStatus_t (*read)(aCommEndpoint_t *, const aCommReadAccess_t *);
    aStatus_t (*write)(aCommEndpoint_t *, const aCommWriteAccess_t *);
    const aCommDataOps_t *data;
    const aCommAddressOps_t *address;
} aCommEndpointOps_t;
struct aCommEndpoint {
    const aCommEndpointOps_t *ops;
};

/** 仅在对象确为对应具体端点时，才允许从首成员恢复外层对象。
 * 端点和引用的配置在使用期间不可移动；对象生命周期由应用管理。
 * read/write 为同步操作，只借用本次数据；排队请求借用至完成回调。
 */
aStatus_t aCommEndpointRead(aCommEndpoint_t *, const aCommReadAccess_t *);
aStatus_t aCommEndpointWrite(aCommEndpoint_t *, const aCommWriteAccess_t *);
aStatus_t aCommDataInfo(aCommEndpoint_t *, const aCommDataQuery_t *,
    aCommDataInfo_t *);
aStatus_t aCommDataUpdate(aCommEndpoint_t *, const aCommDataUpdate_t *);
aStatus_t aCommDataRead(aCommEndpoint_t *, const aCommDataRead_t *);
aStatus_t aCommDataWrite(aCommEndpoint_t *, const aCommDataWrite_t *);
aStatus_t aCommTargetConfigCheck(const aCommTargetConfig_t *);
#endif

/**
 * @file aBus.h
 * @brief 多设备 RAM 数据表。业务仅使用不透明 handle；仅任务/启动上下文。
 * 定义对象保持有效且不变，所有生命周期操作由应用串行化。
 */
#ifndef ABUS_H
#define ABUS_H

#include "aLib.h"
#include "aDataType.h"

#define ABUS_LOCK_NONE 0
#define ABUS_LOCK_BUS 1
#define ABUS_LOCK_SIG 2
#ifndef ABUS_LOCK_MODE
#define ABUS_LOCK_MODE ABUS_LOCK_BUS
#endif
#ifndef ABUS_STATIC_ENABLE
#define ABUS_STATIC_ENABLE 1
#endif
#ifndef ABUS_DYNAMIC_ENABLE
#define ABUS_DYNAMIC_ENABLE 1
#endif
#if ABUS_LOCK_MODE < ABUS_LOCK_NONE || ABUS_LOCK_MODE > ABUS_LOCK_SIG
#error "Invalid ABUS_LOCK_MODE"
#endif
#if (ABUS_STATIC_ENABLE != 0 && ABUS_STATIC_ENABLE != 1) || \
    (ABUS_DYNAMIC_ENABLE != 0 && ABUS_DYNAMIC_ENABLE != 1)
#error "Allocation switches must be 0 or 1"
#endif
#if !ABUS_STATIC_ENABLE && !ABUS_DYNAMIC_ENABLE
#error "Enable at least one aBus allocation API"
#endif

/** @brief 只控制读写时使用锁；不影响创建，NONE 模式忽略。 */
#define ABUS_SIG_FLAG_LOCK (1U << 0)

/** @brief 单个 SIG 标志位的静态名称；组合位应逐位查询。 */
static inline const char *aBusSigFlagName(uint16_t flag)
{
    switch (flag) {
    case 0U: return "NONE";
    case ABUS_SIG_FLAG_LOCK: return "LOCK";
    default: return "UNKNOWN";
    }
}

/** @brief 可选整数范围；联合体成员由所属 SIG/Param 的 type 决定。 */
typedef struct {
    aDataValue_t min;
    aDataValue_t max;
} aBusRange_t;

/** @brief STRUCT 字段描述；允许未登记字段，不支持嵌套 STRUCT。 */
typedef struct {
    size_t offset; /**< 相对 SIG 起点，建议使用 offsetof。 */
    size_t size; /**< 必须大于零；标量须与类型大小一致。 */
    aDataType_t type;
    const aBusRange_t *range; /**< NULL 不限范围；RAW 必须为 NULL。 */
} aBusParam_t;

/** @brief 只读 SIG 定义，当前值统一存放在连续数据区。 */
typedef struct {
    uint16_t sigKey; /**< 同表唯一且跨版本稳定，不要求连续。 */
    uint16_t flags;
    aDataType_t type; /**< 整组类型；可解析结构体用 STRUCT，字节块用 RAW。 */
    size_t size; /**< 完整快照字节数，必须大于零。 */
    const void *default_data; /**< size 字节默认值；NULL 表示清零。 */
    const aBusRange_t *range; /**< 标量可选；RAW/STRUCT 必须为 NULL。 */
    const aBusParam_t *params; /**< 仅 STRUCT 使用；可为 NULL。 */
    size_t param_count;
} aBusSig_t;

typedef struct aBusHandle aBusHandle_t;

/** @brief 只读表定义；表及引用的定义在实例使用期保持有效且不变。 */
typedef struct {
    const aBusSig_t *sigs; /**< 连续的 Flash 定义表。 */
    size_t sig_count;
    uint16_t deviceID; /**< 绑定所属逻辑表标识；应用保证唯一性。 */
} aBusTable_t;

/** @brief 按 instanceID + deviceID + sigIndex 匹配的只读绑定描述。
 * 含绑定的同一 instanceID 只允许一个活动实例，由应用保证。
 * 存储不得重叠；初始化统一写默认值，失败不修改绑定数据。
 */
typedef struct {
    uint16_t instanceID; /**< 所属 handle 的逻辑标识，与表定义无关。 */
    uint16_t deviceID;
    size_t sigIndex;
    void *data;
    size_t size;
} aBusStorageBinding_t;

#if defined(__GNUC__)
/** object 必须为具有静态生命周期的可写对象，不是指向缓冲区的指针。 */
#define ABUS_STORAGE_EXPORT(name, instance_id, device_id, index, object) \
    static const aBusStorageBinding_t name = {                         \
        (instance_id), (device_id), (index), &(object), sizeof(object)  \
    };                                                                \
    static const aBusStorageBinding_t *const name##_registration       \
        __attribute__((used, section(".abus_bindings"),                \
                       aligned(sizeof(void *)))) = &(name)
#else
/* 其他工具链需实现独立的收集适配。 */
#error "aBus storage collection currently requires GCC-compatible sections"
#endif

#define ABUS_TABLE_DEFAULT { NULL, 0U, 0U }

static inline void aBusTableStructInit(aBusTable_t *table)
{
    if (table != NULL) {
        const aBusTable_t defaults = ABUS_TABLE_DEFAULT;
        *table = defaults;
    }
}

/** 定义及默认数据在实例使用期间保持有效且不变。
 * instanceID 选择静态绑定的归属，包含 0 的所有 uint16_t 值均有效。
 * tables 为连续表数组，table_count 大于零；deviceID 在 handle 内唯一。
 * 挂载集合在初始化后固定，数组保持有效至销毁。
 * 所有存储互不重叠；初始化失败不修改静态绑定数据。
 * 生命周期操作由应用串行化，静态入口要求全部绑定；动态入口为未绑定 SIG 分配数据。
 */
#if ABUS_STATIC_ENABLE
aStatus_t aBusInitStatic(uint16_t instanceID,
    const aBusTable_t *tables, size_t table_count,
    aBusHandle_t *handle);
/** 仅释放锁；停止使用后调用。拒绝动态实例。 */
aStatus_t aBusDeInitStatic(aBusHandle_t *handle);
#endif
#if ABUS_DYNAMIC_ENABLE
/** 失败时清空输出并回收已创建资源。 */
aStatus_t aBusCreate(uint16_t instanceID,
    const aBusTable_t *tables, size_t table_count,
    aBusHandle_t **handle_out);
/** 释放全部动态资源；拒绝静态实例。NULL 返回 OK。 */
aStatus_t aBusDestroy(aBusHandle_t *handle);
#endif

/** @brief 查询定位；与读写请求使用相同的设备号和表内下标。 */
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
} aBusSigQuery_t;

/** @brief 复制出的只读元信息，不暴露内部表或数据地址。 */
typedef struct {
    uint16_t sigKey;
    uint16_t flags;
    aDataType_t type;
    size_t size;
    const aBusRange_t *range; /**< 借用只读规则，生命周期同 handle。 */
    const aBusParam_t *params; /**< 借用只读字段描述，禁止修改。 */
    size_t param_count;
} aBusSigInfo_t;

/** 仅就绪实例可查询；未知设备/下标返回 NOT_FOUND。
 * 失败不修改 info。定义不可变，查询无需加锁。 */
aStatus_t aBusGetSigInfo(aBusHandle_t *handle,
    const aBusSigQuery_t *query, aBusSigInfo_t *info);

/** @brief 按下标整组写入；缓冲区不得别名总线存储。 */
typedef struct {
    uint16_t deviceID; /**< handle 内的目标设备。 */
    size_t sigIndex;
    const void *src;
    size_t size;
    aTimeout_t timeout;
} aBusSetIndexRequest_t;

static inline void aBusSetIndexRequestStructInit(
    aBusSetIndexRequest_t *request)
{
    if (request != NULL) {
        const aBusSetIndexRequest_t initial = {
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = initial;
    }
}

/** @brief 按下标整组读取；缓冲区不得别名总线存储。 */
typedef struct {
    uint16_t deviceID; /**< handle 内的目标设备。 */
    size_t sigIndex;
    void *dst;
    size_t size;
    aTimeout_t timeout;
} aBusGetIndexRequest_t;

static inline void aBusGetIndexRequestStructInit(
    aBusGetIndexRequest_t *request)
{
    if (request != NULL) {
        const aBusGetIndexRequest_t initial = {
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = initial;
    }
}

/** @brief 按参数下标访问 STRUCT 字段；不支持嵌套组合。 */
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    size_t paramIndex;
    const void *src;
    size_t size;
    aTimeout_t timeout;
} aBusSetParamRequest_t;

typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    size_t paramIndex;
    void *dst;
    size_t size;
    aTimeout_t timeout;
} aBusGetParamRequest_t;

static inline void aBusSetParamRequestStructInit(aBusSetParamRequest_t *r)
{
    if (r != NULL) {
        const aBusSetParamRequest_t initial = {
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *r = initial;
    }
}

static inline void aBusGetParamRequestStructInit(aBusGetParamRequest_t *r)
{
    if (r != NULL) {
        const aBusGetParamRequest_t initial = {
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *r = initial;
    }
}

/** 参数长度必须完全匹配；未知参数返回 NOT_FOUND，非 STRUCT 返回
 * UNSUPPORTED。与整组访问共用 SIG 锁，保留其他字段；范围失败不修改。
 * 重叠字段的范围同样检查。缓冲区不得与整个 SIG 数据区重叠。
 * 参数/长度错误返回 INVALID_PARAM；无锁访问仍由应用保证同步。 */
aStatus_t aBusSetParam(aBusHandle_t *handle,
    const aBusSetParamRequest_t *request);
aStatus_t aBusGetParam(aBusHandle_t *handle,
    const aBusGetParamRequest_t *request);

/** @brief 按稳定键整组写入；缓冲区不得别名总线存储。 */
typedef struct {
    uint16_t deviceID; /**< handle 内的目标设备。 */
    uint16_t sigKey;
    const void *src;
    size_t size;
    aTimeout_t timeout;
} aBusSetKeyRequest_t;

static inline void aBusSetKeyRequestStructInit(
    aBusSetKeyRequest_t *request)
{
    if (request != NULL) {
        const aBusSetKeyRequest_t initial = {
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = initial;
    }
}

/** @brief 按稳定键整组读取；缓冲区不得别名总线存储。 */
typedef struct {
    uint16_t deviceID; /**< handle 内的目标设备。 */
    uint16_t sigKey;
    void *dst;
    size_t size;
    aTimeout_t timeout;
} aBusGetKeyRequest_t;

static inline void aBusGetKeyRequestStructInit(
    aBusGetKeyRequest_t *request)
{
    if (request != NULL) {
        const aBusGetKeyRequest_t initial = {
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = initial;
    }
}

/** 请求默认不等待锁；调用前填写定位字段、缓冲区和完整长度。
 * 长度必须与 SIG 一致；请求和写入源在调用期间保持稳定，调用后不持有。
 * 参数契约由 assert 检查。范围/加锁失败不修改数据；解锁失败不回滚。
 * timeout 仅控制锁等待；无锁访问由应用串行化或外部同步。
 * 未就绪返回 NOT_READY，越界下标或未知键返回 NOT_FOUND。
 * 设备查找 O(T)；表内 Index 定位 O(1)，Key 查找 O(N)。
 * sigIndex 是当前表内位置，业务应使用枚举；跨版本标识使用 sigKey。
 */
aStatus_t aBusSetByIndex(aBusHandle_t *handle,
    const aBusSetIndexRequest_t *request);
aStatus_t aBusGetByIndex(aBusHandle_t *handle,
    const aBusGetIndexRequest_t *request);
aStatus_t aBusSetByKey(aBusHandle_t *handle,
    const aBusSetKeyRequest_t *request);
aStatus_t aBusGetByKey(aBusHandle_t *handle,
    const aBusGetKeyRequest_t *request);

#endif

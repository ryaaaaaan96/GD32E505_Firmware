/** @file aBus.h
 * @brief 板内整组数据交换；仅启动/任务上下文，不支持 ISR。
 * 定义、状态引用及数据由应用持有，须保持有效且定义不变至 DeInit。
 * Init 写入已登记字段的默认值，其余内容由应用初始化。
 * 运行时禁止绕过接口并发访问内部数据。
 */
#ifndef ABUS_H
#define ABUS_H

#include "aLib.h"
#include "aScalar.h"

/* 模式影响公共状态结构布局，库与调用方必须使用相同配置。 */
#define ABUS_LOCK_NONE 0
#define ABUS_LOCK_BUS 1
#define ABUS_LOCK_SIG 2

#ifndef ABUS_LOCK_MODE
#define ABUS_LOCK_MODE ABUS_LOCK_BUS
#endif
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE && \
    ABUS_LOCK_MODE != ABUS_LOCK_BUS && ABUS_LOCK_MODE != ABUS_LOCK_SIG
#error "ABUS_LOCK_MODE must be NONE(0), BUS(1) or SIG(2)"
#endif

#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
#include "aOS.h"
#endif

/** @brief 控制读写时使用锁，不控制锁创建；顶层 NONE 模式忽略此位。 */
#define ABUS_SIG_FLAG_LOCK (1U << 0)

/** @brief 只读字段规则；未列出的成员仅复制，不校验或设置默认值。 */
typedef struct {
    uint16_t offset; /**< 相对整组起点的字节偏移。 */
    uint16_t flags; /**< 保留且必须为零；与 offset 相邻以减少填充。 */
    aScalarType_t type; /**< 决定三个值联合体使用哪个成员。 */
    aScalar_t min; /**< 最小允许值，包含边界。 */
    aScalar_t max; /**< 最大允许值，包含边界。 */
    aScalar_t default_value; /**< Init 成功时写入，必须在范围内。 */
} aBus_ItemDef;

/** @brief 应用持有的数据引用；运行时不可替换 data。 */
typedef struct {
    void *data; /**< 至少具有组定义 size 字节，不与其他组重叠。 */
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    aOSMutex_t mutex; /**< 内部锁槽，应用初始置 NULL，运行中不得修改。 */
#endif
} aBus_SigState;

typedef struct {
    uint16_t sigID; /**< 应用须保证全表唯一，运行时不检查重复。 */
    uint16_t size; /**< 非零；调用时必须与此大小完全一致。 */
    uint16_t flags; /**< 有锁模式下 LOCK 启用同步；其余情况应用串行化。 */
    uint16_t itemCount;
    const aBus_ItemDef *items;
    aBus_SigState *state;
} aBus_SigDef;

/** @brief 配置结构本身仅在 Init 期间读取；定义表为借用。 */
typedef struct {
    const aBus_SigDef *signals;
    size_t signal_count;
} aBusConfig_t;

#define ABUS_CONFIG_DEFAULT { NULL, 0U }

static inline void aBusConfigStructInit(aBusConfig_t *config)
{
    if (config != NULL) {
        const aBusConfig_t defaults = ABUS_CONFIG_DEFAULT;
        *config = defaults;
    }
}

/** @brief 初始化单例，检查定义、创建锁，最后写入字段默认值。
 * 重复初始化返回 BUSY；无效配置返回 INVALID_PARAM，分配失败返回
 * NO_MEMORY。失败不保留内部资源或修改数据；重复字段/重叠字段无效。
 * sigID 唯一性由应用/构建工具保证；当前尚未接入 Python 校验。
 * ABUS_DEF_CHECK_ENABLE 控制定义检查，关闭时由应用保证定义合法。
 * config、signals、数量等调用参数使用 assert 检查，由 NDEBUG 控制。
 * 数据区不得与配置、定义表、规则或状态引用等元数据重叠。
 * 所有生命周期调用由应用串行化，初始化完成后才可发布给其他任务。
 */
aStatus_t aBusInit(const aBusConfig_t *config);

/** @brief 停止全部使用者后销毁；不释放应用数据，允许重复调用。 */
void aBusDeInit(void);

/** @brief 整组替换；输入须稳定且不得与任何总线数据区重叠。
 * timeout 仅为锁等待预算。校验失败不修改当前值。
 * 未初始化返回 NOT_READY，ID 缺失返回 NOT_FOUND。
 * 范围错误返回 INVALID_PARAM；获取锁失败返回 aOS 状态，不执行复制。
 * 解锁异常时数据已经复制，仍返回解锁错误；不能视为事务回滚。
 * 空指针、长度不符、非法超时及当前组重叠通过 assert 检查。
 * NDEBUG 关闭断言后，违反调用契约不保证返回错误或安全执行。
 * 与其他组的别名始终由调用者避免。
 */
aStatus_t aBusSetSig(uint16_t sigID, const void *src, uint16_t size,
                     aTimeout_t timeout);

/** @brief 复制完整快照；输出不得与任何总线数据区重叠。
 * 错误及锁等待语义同 SetSig；获取锁失败时输出不变。
 * 无内部锁的组由调用者保证全部读写串行化，不隐式启用原子操作。
 */
aStatus_t aBusGetSig(uint16_t sigID, void *dst, uint16_t size,
                     aTimeout_t timeout);

#endif

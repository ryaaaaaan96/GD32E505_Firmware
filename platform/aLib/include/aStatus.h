/**
 * @file aStatus.h
 * @brief 全平台非流式操作的统一状态码。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 返回值只表示操作状态；数据通过带类型的输出参数返回。
 * 除接口另有说明外，仅成功后读取输出参数；各模块不得重复定义错误枚举。
 * 流式接口使用 aSSize_t 和 aOS errno，不直接返回此枚举作为字节数。
 */

#ifndef A_STATUS_H
#define A_STATUS_H

/** @brief 可失败的非流式操作结果；各接口注明实际可能返回的子集。 */
typedef enum {
    A_STATUS_OK = 0, /**< 操作成功。 */
    A_STATUS_ERROR = -1, /**< 一般 I/O 或内部错误。 */
    A_STATUS_INVALID_PARAM = -2, /**< 输入参数不合法。 */
    A_STATUS_TIMEOUT = -3, /**< 有限等待预算耗尽。 */
    A_STATUS_BUSY = -4, /**< 资源被占用或当前不可立即完成。 */
    A_STATUS_UNSUPPORTED = -5, /**< 构建或硬件不支持该能力。 */
    A_STATUS_NOT_READY = -6, /**< 对象/前置状态未就绪。 */
    A_STATUS_NO_MEMORY = -7, /**< 无法分配所需内存。 */
    A_STATUS_CANCELLED = -8, /**< 请求已取消。 */
    A_STATUS_NOT_FOUND = -9 /**< 指定对象或标识不存在。 */
} aStatus_t;

#endif

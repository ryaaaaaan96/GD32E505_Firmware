/**
 * @file aFifo.h
 * @brief 调用方提供存储的定长元素 FIFO。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 无动态分配，无内部锁；所有访问（含 Count/Peek）由调用方串行化。
 * 元素按值复制；元素中保存的指针不会转移所指对象的所有权。
 * 存储区至少 capacity * element_size 字节，并保持有效到最后一次访问结束。
 */

#ifndef AFIFO_H
#define AFIFO_H
#include "aStatus.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief FIFO 状态；除初始化外，字段由 FIFO 操作维护，不得直接修改游标。 */
typedef struct {
    unsigned char *storage; /**< 调用方提供的元素存储。 */
    size_t element_size; /**< 每个元素的字节数。 */
    size_t capacity; /**< 最大元素数。 */
    size_t head; /**< 下一个写入槽。 */
    size_t tail; /**< 当前队首槽。 */
    size_t count; /**< 当前元素数量，允许使用全部 capacity 个槽。 */
} aFifo_t;

/**
 * @brief 初始化一个空 FIFO，不清空存储区内容。
 * @param[out] fifo FIFO 状态，不得为 NULL。
 * @param[in] storage 调用方提供的可写存储区，不得为 NULL。
 * @param[in] capacity 元素数量，必须大于 0。
 * @param[in] element_size 单个元素的字节数，必须大于 0。
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 空指针、零容量/元素大小或容量乘积溢出。
 * @warning 不得重新初始化正在被其他上下文访问的 FIFO。
 */
static inline aStatus_t aFifoInit(aFifo_t *fifo, void *storage,
                                 size_t capacity, size_t element_size)
{
    if (fifo == NULL || storage == NULL || capacity == 0U ||
        element_size == 0U || capacity > SIZE_MAX / element_size)
        return A_STATUS_INVALID_PARAM;
    *fifo = (aFifo_t) { .storage = storage, .capacity = capacity,
                       .element_size = element_size };
    return A_STATUS_OK;
}

/**
 * @brief 将一个元素复制到队尾，不等待空间。
 * @param[in,out] fifo 已初始化的 FIFO。
 * @param[in] element 至少 element_size 字节的源对象；不得与目标槽重叠。
 * @retval A_STATUS_OK 入队成功。
 * @retval A_STATUS_BUSY 队列已满，未修改队列。
 * @retval A_STATUS_INVALID_PARAM 空指针或未配置存储。
 */
static inline aStatus_t aFifoPush(aFifo_t *fifo, const void *element)
{
    if (fifo == NULL || element == NULL || fifo->storage == NULL)
        return A_STATUS_INVALID_PARAM;
    if (fifo->count == fifo->capacity) return A_STATUS_BUSY;
    memcpy(fifo->storage + fifo->head * fifo->element_size, element,
        fifo->element_size);
    fifo->head = (fifo->head + 1U) % fifo->capacity;
    ++fifo->count;
    return A_STATUS_OK;
}

/**
 * @brief 复制队首元素但不出队。
 * @param[in] fifo 已初始化的 FIFO。
 * @param[out] element 至少 element_size 字节的目标对象；不得与源槽重叠。
 * @retval A_STATUS_OK 已复制队首。
 * @retval A_STATUS_NOT_READY 队列为空，输出不变。
 * @retval A_STATUS_INVALID_PARAM 空指针或未配置存储。
 */
static inline aStatus_t aFifoPeek(const aFifo_t *fifo, void *element)
{
    if (fifo == NULL || element == NULL || fifo->storage == NULL)
        return A_STATUS_INVALID_PARAM;
    if (fifo->count == 0U) return A_STATUS_NOT_READY;
    memcpy(element, fifo->storage + fifo->tail * fifo->element_size,
        fifo->element_size);
    return A_STATUS_OK;
}

/**
 * @brief 复制并移除队首元素。
 * @param[in,out] fifo 已初始化的 FIFO。
 * @param[out] element 至少 element_size 字节的目标对象，不得为 NULL 或与源槽重叠。
 * @return 与 aFifoPeek() 相同；失败不移动消费游标。
 */
static inline aStatus_t aFifoPop(aFifo_t *fifo, void *element)
{
    aStatus_t status = aFifoPeek(fifo, element);
    if (status != A_STATUS_OK) return status;
    fifo->tail = (fifo->tail + 1U) % fifo->capacity;
    --fifo->count;
    return A_STATUS_OK;
}

/**
 * @brief 获取当前已入队的元素数量。
 * @param[in] fifo 已初始化的 FIFO；允许为 NULL。
 * @return 元素数量；NULL 返回 0。不是线程安全快照。
 */
static inline size_t aFifoCount(const aFifo_t *fifo)
{
    return fifo != NULL ? fifo->count : 0U;
}
#endif

#include "aDrv_dma_memory.h"
#include "aDrv_internal.h"

/* 由芯片链接脚本导出实际容量，不能把整个 ARM SRAM 地址窗口视为可用。 */
extern unsigned char __adrv_dma_ram_start[], __adrv_dma_ram_end[];
extern unsigned char __adrv_dma_flash_start[], __adrv_dma_flash_end[];

static aBool_t contains(uintptr_t start, uintptr_t end,
                        uintptr_t address, size_t size)
{
    /* 用减法检查末端，避免 address + size 回绕。 */
    return address >= start && address < end && size <= end - address;
}

aStatus_t aDrvPrivateDmaMemoryCheck(const void *buffer, size_t size,
                                   aBool_t writable)
{
    const uintptr_t address = (uintptr_t)buffer;

    if (buffer == NULL || size == 0U)
        return A_STATUS_INVALID_PARAM;
    /* GD32E505 SRAM 无需缓存维护，循环 RX 同样可以直接使用。 */
    if (contains((uintptr_t)__adrv_dma_ram_start,
                 (uintptr_t)__adrv_dma_ram_end, address, size))
        return A_STATUS_OK;
    if (!writable &&
        contains((uintptr_t)__adrv_dma_flash_start,
                 (uintptr_t)__adrv_dma_flash_end, address, size))
        return A_STATUS_OK;
    return A_STATUS_INVALID_PARAM;
}

aStatus_t aDrvDmaBufferCheck(const void *buffer, size_t size,
                            aDrvDmaBufferUsage_t usage)
{
    if ((unsigned)usage > ADRV_DMA_BUFFER_RX_CIRCULAR)
        return A_STATUS_INVALID_PARAM;
    /* 当前 SRAM 无缓存行约束；有缓存的后端在此检查整个保留区的策略。 */
    return aDrvPrivateDmaMemoryCheck(buffer, size,
                                     usage != ADRV_DMA_BUFFER_TX);
}

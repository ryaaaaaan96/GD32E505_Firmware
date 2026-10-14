#include <assert.h>
#include "../../platform/aDrv/src/aDrv_fault.c"

int main(void)
{
    uint32_t stack[26] = {0};
    const uintptr_t begin = (uintptr_t)stack;
    const uintptr_t end = begin + sizeof(stack);
    aDrvFaultInfo_t info;

    /* 基础和 FPU 扩展帧的核心寄存器都从最低地址开始。 */
    assert(frame_source(begin, 0xFFFFFFFDU, 0U, begin, end) == stack);
    assert(frame_source(begin, 0xFFFFFFEDU, 0U, begin, end) == stack);
    assert(frame_source(begin, 0xFFFFFFFDU, 0U, begin, begin + 32U) == stack);
    assert(frame_source(begin, 0xFFFFFFEDU, 0U, begin, begin + 32U) == NULL);
    assert(frame_source(begin + 1U, 0xFFFFFFFDU, 0U, begin, end) == NULL);
    assert(frame_source(end - 28U, 0xFFFFFFFDU, 0U, begin, end) == NULL);
    assert(frame_source(UINTPTR_MAX - 3U, 0xFFFFFFFDU, 0U, begin, end) == NULL);
    assert(frame_source(begin, 0U, 0U, begin, end) == NULL);
    for (unsigned bit = 0U; bit < 32U; bit++) {
        if (((1U << bit) & 0x103838U) != 0U)
            assert(frame_source(begin, 0xFFFFFFFDU, 1U << bit,
                                begin, end) == NULL);
    }
    assert(aDrvFaultGetInfo(NULL) == A_STATUS_INVALID_PARAM);
    aDrvFaultClear();
    assert(aDrvFaultGetInfo(&info) == A_STATUS_NOT_FOUND);
    g_aDrvFaultRecord.info.pc = 0x08001234U;
    g_aDrvFaultRecord.info.frame_valid = 1U;
    g_aDrvFaultRecord.checksum = checksum(&g_aDrvFaultRecord.info);
    g_aDrvFaultRecord.magic = FAULT_MAGIC;
    aDrvFaultInit();
    assert(aDrvFaultGetInfo(&info) == A_STATUS_OK && info.pc == 0x08001234U);
    g_aDrvFaultRecord.info.pc++;
    assert(aDrvFaultGetInfo(&info) == A_STATUS_NOT_FOUND);
    aDrvFaultInit();
    assert(g_aDrvFaultRecord.magic == 0U);
    return 0;
}

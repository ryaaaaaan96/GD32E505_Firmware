#include "aDrv_fault.h"
#include "gd32e50x.h"
#include <stddef.h>

#define FAULT_MAGIC 0x41464C31U /* AFL1：修改记录布局时同步修改。 */

typedef struct {
    uint32_t magic;
    uint32_t checksum;
    aDrvFaultInfo_t info;
} aDrvFaultRecord_t;

/* 独立于 .bss 清零；链接脚本必须保留此段。 */
volatile aDrvFaultRecord_t g_aDrvFaultRecord
    __attribute__((section(".noinit.fault"), aligned(4)));

static uint32_t checksum(const volatile aDrvFaultInfo_t *info)
{
    const volatile uint8_t *bytes = (const volatile void *)info;
    uint32_t value = FAULT_MAGIC;
    for (size_t i = 0U; i < sizeof(*info); i++)
        value = (value ^ bytes[i]) * 16777619U;
    return value;
}

aStatus_t aDrvFaultGetInfo(aDrvFaultInfo_t *info)
{
    if (info == NULL) return A_STATUS_INVALID_PARAM;
    if (g_aDrvFaultRecord.magic != FAULT_MAGIC ||
        checksum(&g_aDrvFaultRecord.info) != g_aDrvFaultRecord.checksum)
        return A_STATUS_NOT_FOUND;
    *info = g_aDrvFaultRecord.info;
    return A_STATUS_OK;
}

void aDrvFaultClear(void)
{
    g_aDrvFaultRecord.magic = 0U;
}

/* 由 aDrvInit 引用，确保静态库中的强 HardFault_Handler 被链接。
 * 只使未完成/损坏记录无效，不清除一次有效的历史故障。 */
void aDrvFaultInit(void)
{
    if (g_aDrvFaultRecord.magic != FAULT_MAGIC ||
        checksum(&g_aDrvFaultRecord.info) != g_aDrvFaultRecord.checksum)
        aDrvFaultClear();
}

/* 当前无 TrustZone 的异常帧：低地址始终为 R0..xPSR；浮点扩展在其后。
 * 只读取核心寄存器。栈错误、非法 EXC_RETURN 或越界均拒绝读取。
 * 布局参考 Armv8-M Exception Model User Guide 的 Stack frames。 */
static const uint32_t *frame_source(uintptr_t stack, uint32_t exception_return,
    uint32_t cfsr, uintptr_t begin, uintptr_t end)
{
    const size_t size = (exception_return & (1U << 4)) ? 32U : 104U;
    if ((exception_return & 0xFFFFFFE3U) != 0xFFFFFFE1U ||
        (cfsr & 0x103838U) != 0U || (stack & 3U) != 0U ||
        stack < begin || stack > end || size > end - stack)
        return NULL;
    return (const uint32_t *)stack;
}

#if defined(__arm__)
extern uint32_t _sdata, _estack;
/* 故障可能由 MSP 溢出引发，切换到专用栈后才运行 C 代码。 */
static uint8_t fault_stack[256] __attribute__((used, aligned(8)));

static void fault_capture(uint32_t *stack, uint32_t exception_return)
    __attribute__((used, noreturn));
static void fault_capture(uint32_t *stack, uint32_t exception_return)
{
    volatile aDrvFaultInfo_t *info = &g_aDrvFaultRecord.info;
    const uint32_t *frame;
    volatile uint8_t *bytes = (volatile void *)info;

    g_aDrvFaultRecord.magic = 0U;
    for (size_t i = 0U; i < sizeof(*info); i++) bytes[i] = 0U;
    info->exception_return = exception_return;
    info->stack_pointer = (uint32_t)(uintptr_t)stack;
    info->cfsr = SCB->CFSR;
    info->hfsr = SCB->HFSR;
    info->mmfar = SCB->MMFAR;
    info->bfar = SCB->BFAR;
    frame = frame_source((uintptr_t)stack, exception_return, info->cfsr,
                         (uintptr_t)&_sdata, (uintptr_t)&_estack);
    if (frame != NULL) {
        info->r0 = frame[0]; info->r1 = frame[1];
        info->r2 = frame[2]; info->r3 = frame[3];
        info->r12 = frame[4]; info->lr = frame[5];
        info->pc = frame[6]; info->xpsr = frame[7];
        info->frame_valid = 1U;
    }
    g_aDrvFaultRecord.checksum = checksum(info);
    __DMB();
    g_aDrvFaultRecord.magic = FAULT_MAGIC;
    __DSB();
    for (;;) __WFI();
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "cpsid i\n"
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "movs r3, #0\n"
        "msr msplim, r3\n"
        "ldr r2, =fault_stack + 256\n"
        "msr msp, r2\n"
        "b fault_capture\n");
}
#endif

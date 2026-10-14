/** @brief Cortex-M 故障快照；不依赖 OS、日志或 Flash。 */
#ifndef ADRV_FAULT_H
#define ADRV_FAULT_H

#include "aLib.h"

typedef struct {
    uint32_t exception_return;
    uint32_t stack_pointer;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar; /**< 仅在 CFSR.MMARVALID 置位时有意义。 */
    uint32_t bfar; /**< 仅在 CFSR.BFARVALID 置位时有意义。 */
    uint32_t frame_valid; /**< 为零时不使用下面的堆栈寄存器。 */
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
} aDrvFaultInfo_t;

/** 读取最近故障；无有效记录返回 NOT_FOUND。只在正常任务/启动阶段调用。
 * 当前 GD32 放入 .noinit，启动不清零；仅可能跨保留 SRAM 的复位保存，
 * 不保证跨掉电、下载或调试器复位保存。校验仅用于诊断，不作为业务状态。
 * 故障处理只保存并停机，不在异常上下文输出日志、写 Flash 或自动复位。
 */
aStatus_t aDrvFaultGetInfo(aDrvFaultInfo_t *info);
void aDrvFaultClear(void);

#endif

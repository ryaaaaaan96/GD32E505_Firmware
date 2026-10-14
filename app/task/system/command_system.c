#include "aOS.h"
#include "aDrv_fault.h"
#include "aShell.h"
#include <string.h>

/* 按需采样，不创建监控任务；诊断调用也不分配 OS 堆。 */
static int system_command(int argc, char **argv)
{
    aStatus_t status = A_STATUS_INVALID_PARAM;
    if (argc == 2 && strcmp(argv[1], "heap") == 0) {
        aOSHeapInfo_t info;
        status = aOSHeapGetInfo(&info);
        if (status == A_STATUS_OK)
            ASHELL_REPLY("heap: reserved=%lu free=%lu minimum_free=%lu B\r\n",
                (unsigned long)info.total, (unsigned long)info.free,
                (unsigned long)info.minimum_free);
    } else if ((argc == 2 || argc == 3) && strcmp(argv[1], "stack") == 0) {
        size_t bytes;
        const char *name = argc == 3 ? argv[2] : NULL;
        status = aOSTaskStackGetFree(name, &bytes);
        if (status == A_STATUS_OK)
            ASHELL_REPLY("%s: minimum_free_stack=%lu B\r\n",
                name == NULL ? "current" : name, (unsigned long)bytes);
    } else if (argc == 2 && strcmp(argv[1], "fault") == 0) {
        aDrvFaultInfo_t info;
        /* context 是借用指针，可能来自已删除的任务，只打印地址。 */
        ASHELL_REPLY("OS fault: code=%lu status=%ld context=%p\r\n",
            (unsigned long)g_aOSFaultRecord.code,
            (long)g_aOSFaultRecord.status, (void *)g_aOSFaultRecord.context);
        status = aDrvFaultGetInfo(&info);
        if (status == A_STATUS_NOT_FOUND) {
            ASHELL_REPLY("No retained CPU fault\r\n");
            return 0;
        }
        if (status == A_STATUS_OK) {
            ASHELL_REPLY("CPU fault: CFSR=%08lx HFSR=%08lx\r\n",
                (unsigned long)info.cfsr, (unsigned long)info.hfsr);
            ASHELL_REPLY("SP=%08lx EXC_RETURN=%08lx frame_valid=%lu\r\n",
                (unsigned long)info.stack_pointer,
                (unsigned long)info.exception_return,
                (unsigned long)info.frame_valid);
            if (info.frame_valid)
                ASHELL_REPLY("PC=%08lx LR=%08lx xPSR=%08lx\r\n",
                    (unsigned long)info.pc, (unsigned long)info.lr,
                    (unsigned long)info.xpsr);
            ASHELL_REPLY("BFAR=%08lx MMFAR=%08lx (check CFSR valid bits)\r\n",
                (unsigned long)info.bfar, (unsigned long)info.mmfar);
        }
    } else if (argc == 3 && strcmp(argv[1], "fault") == 0 &&
               strcmp(argv[2], "clear") == 0) {
        aDrvFaultClear();
        aOSRecordFault(AOS_FAULT_NONE, A_STATUS_OK, NULL);
        ASHELL_REPLY("Fault records cleared\r\n");
        return 0;
    } else {
        ASHELL_REPLY("sys heap | stack [task] | fault [clear]\r\n");
        return -1;
    }
    if (status != A_STATUS_OK)
        ASHELL_REPLY("sys failed: %d\r\n", (int)status);
    return status == A_STATUS_OK ? 0 : -1;
}

ASHELL_CMD_EXPORT(sys, system_command, "Heap, task stack and fault diagnostics");

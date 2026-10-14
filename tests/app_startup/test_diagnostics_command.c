#include "aOS.h"
#include "aDrv_fault.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char output[2048];
static size_t used;
static aStatus_t heap_status = A_STATUS_OK;
static aStatus_t fault_status = A_STATUS_NOT_FOUND;
static unsigned cleared;
static aDrvFaultInfo_t fault;
aOSFaultRecord_t g_aOSFaultRecord;

int test_reply(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int size = vsnprintf(output + used, sizeof(output) - used, format, args);
    va_end(args);
    assert(size >= 0 && (size_t)size < sizeof(output) - used);
    used += (size_t)size;
    return size;
}

aStatus_t aOSHeapGetInfo(aOSHeapInfo_t *info)
{
    if (heap_status == A_STATUS_OK) *info = (aOSHeapInfo_t){40960, 6000, 4000};
    return heap_status;
}

aStatus_t aOSTaskStackGetFree(const char *name, size_t *bytes)
{
    if (name != NULL && strcmp(name, "modbus") != 0) return A_STATUS_NOT_FOUND;
    *bytes = 512U;
    return A_STATUS_OK;
}

aStatus_t aDrvFaultGetInfo(aDrvFaultInfo_t *info)
{
    if (fault_status == A_STATUS_OK) *info = fault;
    return fault_status;
}

void aDrvFaultClear(void) { cleared++; }
void aOSRecordFault(aOSFaultCode_t code, aStatus_t status, const char *context)
{
    assert(code == AOS_FAULT_NONE && status == A_STATUS_OK && context == NULL);
    cleared++;
}

int test_command(int argc, char **argv);
static int command(int argc, char **argv)
{
    used = 0U;
    output[0] = '\0';
    return test_command(argc, argv);
}

int main(void)
{
    char *args[] = {"sys", "heap", "modbus"};
    assert(command(2, args) == 0);
    assert(strstr(output, "reserved=40960 free=6000 minimum_free=4000 B"));
    heap_status = A_STATUS_UNSUPPORTED;
    assert(command(2, args) == -1 && strstr(output, "sys failed"));
    args[1] = "stack";
    assert(command(2, args) == 0 && strstr(output, "current:"));
    assert(command(3, args) == 0 && strstr(output, "modbus:"));
    args[2] = "missing";
    assert(command(3, args) == -1);
    args[1] = "fault";
    /* 已失效的 context 只能显示地址，不能解引用。 */
    g_aOSFaultRecord.context = (const void *)1;
    assert(command(2, args) == 0 && strstr(output, "No retained CPU fault"));
    fault_status = A_STATUS_OK;
    fault.pc = 0x08001234U;
    assert(command(2, args) == 0 && strstr(output, "PC=") == NULL);
    fault.frame_valid = 1U;
    assert(command(2, args) == 0 && strstr(output, "PC=08001234"));
    args[2] = "clear";
    assert(command(3, args) == 0 && cleared == 2U);
    args[2] = "bad";
    assert(command(3, args) == -1 && cleared == 2U);
    assert(command(1, args) == -1);
    puts("System diagnostic command output/error/clear checks passed");
}

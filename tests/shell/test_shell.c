#include "aShell.h"
#include "aOS.h"
#include "shell.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations, mutexes, handled, removed;
static unsigned fail_at, allocation_attempt;
static int16_t input_result = 1;
void *aOSAlloc(size_t size)
{
    if (++allocation_attempt == fail_at) return NULL;
    void *p = malloc(size);
    if (p) ++allocations;
    return p;
}
void aOSFree(void *p) { if (p) { --allocations; free(p); } }
aStatus_t aOSRecursiveMutexCreate(aOSRecursiveMutex_t *p)
{ ++mutexes; *p = p; return A_STATUS_OK; }
void aOSRecursiveMutexDestroy(aOSRecursiveMutex_t *p)
{ if (*p) { --mutexes; *p = NULL; } }
aStatus_t aOSRecursiveMutexLock(aOSRecursiveMutex_t p, aTimeout_t timeout)
{ (void)timeout; assert(p); return A_STATUS_OK; }
aStatus_t aOSRecursiveMutexUnlock(aOSRecursiveMutex_t p)
{ assert(p); return A_STATUS_OK; }
void shellInit(Shell *s, char *p, uint16_t n) { assert(s && p && n >= 64); }
void shellRemove(Shell *s) { assert(s); ++removed; }
void shellHandler(Shell *s, char c) { assert(s && c == 'x'); ++handled; }
void shellWriteString(Shell *s, const char *p) { assert(s && p); }
static int16_t read_input(char *p, uint16_t n)
{ assert(n == 64); memset(p, 'x', n); return input_result; }
static int16_t write_output(char *p, uint16_t n) { (void)p; return (int16_t)n; }
int main(void)
{
    aShellConfig_t config;
    aShellConfigStructInit(&config);
    assert(aShellProcess() == A_STATUS_NOT_READY);
    assert(aShellInit(&config) == A_STATUS_INVALID_PARAM);
    config.read = read_input; config.write = write_output;
    for (unsigned i = 1; i <= 2; ++i) {
        allocation_attempt = 0; fail_at = i;
        assert(aShellInit(&config) == A_STATUS_NO_MEMORY);
        assert(!allocations && !mutexes);
    }
    fail_at = 0;
    assert(aShellInit(&config) == A_STATUS_OK);
    assert(aShellInit(&config) == A_STATUS_BUSY);
    assert(aShellProcess() == A_STATUS_OK && handled == 1);
    input_result = 0;
    assert(aShellProcess() == A_STATUS_BUSY && handled == 1);
    input_result = 64;
    assert(aShellProcess() == A_STATUS_OK && handled == 65);
    input_result = 65;
    assert(aShellProcess() == A_STATUS_ERROR && handled == 65);
    input_result = -1;
    assert(aShellProcess() == A_STATUS_ERROR);
    aShellPrint("test %d", 1);
    assert(aShellDeInit() == A_STATUS_OK);
    assert(!allocations && !mutexes && removed == 1);
    assert(aShellDeInit() == A_STATUS_NOT_READY);
    /* No task creation/deletion mock exists: linking proves func owns none. */
    return 0;
}

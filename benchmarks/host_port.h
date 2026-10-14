#ifndef BENCH_HOST_PORT_H
#define BENCH_HOST_PORT_H
#include <stddef.h>
#include <stdint.h>
uint32_t benchClock(void);
size_t benchMemoryLive(void);
size_t benchMemoryPeak(void);
void benchMemoryResetPeak(void);
#endif

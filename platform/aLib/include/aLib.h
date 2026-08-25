#ifndef A_LIB_H
#define A_LIB_H

#include <stdint.h>

#define ALIB_WAIT_FOREVER UINT32_MAX
#if defined(__GNUC__) || defined(__clang__)
#define ALIB_USED __attribute__((used))
#define ALIB_WEAK __attribute__((weak))
#define ALIB_SECTION(name_) __attribute__((section(name_), used))
#else
#define ALIB_USED
#define ALIB_WEAK
#define ALIB_SECTION(name_)
#endif

#endif

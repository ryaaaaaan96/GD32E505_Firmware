#ifndef A_CRC_H
#define A_CRC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief CRC-32/ISO-HDLC（IEEE）：反射多项式 0xEDB88320，初值/异或值全 1。
 * 首次传入 crc=0，分段计算时传入上段返回值；空段返回原 crc。
 * size 非零时 data 须指向至少 size 字节；size 为零时允许 NULL。
 * 无状态、无分配，不要求数据对齐；仅计算字节，不解释数据类型或字节序。
 */
static inline uint32_t aCrc32(uint32_t crc, const void *data, size_t size)
{
    /* 每次处理 4 位，常量表占 64 字节。 */
    static const uint32_t table[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
    };
    const uint8_t *bytes = (const uint8_t *)data;

    crc = ~crc;
    while (size-- != 0U) {
        crc ^= *bytes++;
        crc = (crc >> 4U) ^ table[crc & 0x0FU];
        crc = (crc >> 4U) ^ table[crc & 0x0FU];
    }
    return ~crc;
}

#endif

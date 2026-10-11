#include "aMemory.h"
#include "aMemory_layout.h"
#include "aShell.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>

static aStatus_t address_parse(const char *text, uint32_t *address)
{
    char *end;
    uintmax_t value;
    const char *p = text;
    int base = 10;

    if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
        base = 16;
        p += 2;
    }
    if (*p == '\0') return A_STATUS_INVALID_PARAM;
    for (const char *digit = p; *digit != '\0'; ++digit) {
        if ((*digit >= '0') && (*digit <= '9')) continue;
        if ((base == 16) &&
            (((*digit >= 'a') && (*digit <= 'f')) ||
             ((*digit >= 'A') && (*digit <= 'F')))) continue;
        return A_STATUS_INVALID_PARAM;
    }
    errno = 0;
    value = strtoumax(p, &end, base);
    if ((errno == ERANGE) || (*end != '\0') || (value > UINT32_MAX))
        return A_STATUS_INVALID_PARAM;
    *address = (uint32_t)value;
    return A_STATUS_OK;
}

static uint8_t pattern(uint32_t offset)
{
    return (uint8_t)((offset * 37U) ^ (offset >> 8U) ^ 0xA5U);
}

static aStatus_t verify(const aMemoryHandle_t *region,
                        uint32_t address, uint32_t size, aBool_t erased)
{
    aMemoryReadRequest_t request;
    uint8_t buffer[256];
    uint8_t expected;
    uint32_t offset;
    size_t i;
    aStatus_t status;

    aMemoryReadRequestStructInit(&request);
    request.data = buffer;
    for (offset = 0U; offset < size; offset += (uint32_t)request.size) {
        request.address = address + offset;
        request.size = size - offset;
        if (request.size > sizeof(buffer)) request.size = sizeof(buffer);
        status = aMemoryRead(region, &request);
        if (status != A_STATUS_OK) return status;
        for (i = 0U; i < request.size; ++i) {
            expected = erased ? 0xFFU : pattern(offset + (uint32_t)i);
            if (buffer[i] != expected) {
                ASHELL_REPLY("Mismatch at 0x%08lX: expected %02X, got %02X"
                             "\r\n",
                             (unsigned long)(request.address + i),
                             (unsigned)expected, (unsigned)buffer[i]);
                return A_STATUS_ERROR;
            }
        }
    }
    return A_STATUS_OK;
}

static aStatus_t sector_test(const aMemoryHandle_t *region,
                             uint32_t address, const aMemoryInfo_t *info)
{
    const aMemoryGeometry_t *geometry = &info->geometry;
    aMemoryEraseRequest_t erase;
    aMemoryWriteRequest_t write;
    uint8_t buffer[320];
    uint32_t offset;
    size_t i;
    aStatus_t status;

    if ((geometry->erase_granularity == 0U) ||
        (geometry->capacity > UINT32_MAX) ||
        (address % geometry->erase_granularity != 0U) ||
        (address > geometry->capacity) ||
        (geometry->erase_granularity > geometry->capacity - address))
        return A_STATUS_INVALID_PARAM;
    ASHELL_REPLY("Erasing %lu bytes at 0x%08lX; previous data is lost.\r\n",
                 (unsigned long)geometry->erase_granularity,
                 (unsigned long)address);
    aMemoryEraseRequestStructInit(&erase);
    erase.address = address;
    erase.size = geometry->erase_granularity;
    status = aMemoryErase(region, &erase);
    if (status != A_STATUS_OK) return status;
    status = verify(region, address, geometry->erase_granularity, A_TRUE);
    if (status != A_STATUS_OK) return status;
    ASHELL_REPLY("Erase verify OK\r\n");

    aMemoryWriteRequestStructInit(&write);
    write.data = buffer;
    /* 320 字节分块跨越常见的 256 字节页，验证驱动的分页处理。 */
    for (offset = 0U; offset < geometry->erase_granularity;
         offset += (uint32_t)write.size) {
        write.address = address + offset;
        write.size = geometry->erase_granularity - offset;
        if (write.size > sizeof(buffer)) write.size = sizeof(buffer);
        for (i = 0U; i < write.size; ++i)
            buffer[i] = pattern(offset + (uint32_t)i);
        status = aMemoryWrite(region, &write);
        if (status != A_STATUS_OK) return status;
    }
    status = verify(region, address, geometry->erase_granularity, A_FALSE);
    if (status == A_STATUS_OK)
        ASHELL_REPLY("Flash test PASS: %lu bytes at 0x%08lX\r\n",
                     (unsigned long)geometry->erase_granularity,
                     (unsigned long)address);
    return status;
}

static int command_flash(int argc, char **argv)
{
    aMemoryInfo_t info;
    const aMemoryHandle_t *region;
    aStatus_t status;
    uint32_t address;
    aBool_t test;

    test = (argc == 3) && (strcmp(argv[1], "test") == 0);
    if (!test && !((argc == 2) && (strcmp(argv[1], "info") == 0))) {
        ASHELL_REPLY("flash info\r\n"
                     "flash test <offset>: destroys one erase sector in "
                     "flash_test region\r\n");
        return -1;
    }
    region = aMemoryFind(AMEMORY_PART_TEST_NAME);
    status = aMemoryGetInfo(region, &info);
    if (status != A_STATUS_OK) goto failed;
    if (!test) {
        ASHELL_REPLY("Region %s on %s: %lu bytes, erase %lu bytes\r\n",
                     info.name, info.device_name,
                     (unsigned long)info.geometry.capacity,
                     (unsigned long)info.geometry.erase_granularity);
        return 0;
    }
    status = address_parse(argv[2], &address);
    if (status == A_STATUS_OK) status = sector_test(region, address, &info);
    if (status == A_STATUS_OK) return 0;
failed:
    ASHELL_REPLY("Flash command failed: %d\r\n", (int)status);
    return -1;
}

ASHELL_CMD_EXPORT(flash, command_flash, "Flash info / destructive sector test");

#include "app_system_flash.h"
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

static aStatus_t verify(uint32_t address, uint32_t size, aBool_t erased)
{
    aDevFlash25qReadRequest_t request;
    uint8_t buffer[256];
    uint8_t expected;
    uint32_t offset;
    size_t i;
    aStatus_t status;

    aDevFlash25qReadRequestStructInit(&request);
    request.data = buffer;
    for (offset = 0U; offset < size; offset += (uint32_t)request.size) {
        request.address = address + offset;
        request.size = size - offset;
        if (request.size > sizeof(buffer)) request.size = sizeof(buffer);
        status = appSystemFlashRead(&request);
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

static aStatus_t sector_test(uint32_t address, const aDevFlash25qInfo_t *info)
{
    aDevFlash25qEraseRequest_t erase;
    aDevFlash25qWriteRequest_t write;
    uint8_t buffer[320];
    uint32_t offset;
    size_t i;
    aStatus_t status;

    if ((info->erase_size == 0U) || (address % info->erase_size != 0U) ||
        (address > info->capacity) ||
        (info->erase_size > info->capacity - address))
        return A_STATUS_INVALID_PARAM;
    ASHELL_REPLY("Erasing %lu bytes at 0x%08lX; previous data is lost.\r\n",
                 (unsigned long)info->erase_size, (unsigned long)address);
    aDevFlash25qEraseRequestStructInit(&erase);
    erase.address = address;
    erase.size = info->erase_size;
    status = appSystemFlashErase(&erase);
    if (status != A_STATUS_OK) return status;
    status = verify(address, info->erase_size, A_TRUE);
    if (status != A_STATUS_OK) return status;
    ASHELL_REPLY("Erase verify OK\r\n");

    aDevFlash25qWriteRequestStructInit(&write);
    write.data = buffer;
    /* 320 字节分块跨越常见的 256 字节页，验证驱动的分页处理。 */
    for (offset = 0U; offset < info->erase_size;
         offset += (uint32_t)write.size) {
        write.address = address + offset;
        write.size = info->erase_size - offset;
        if (write.size > sizeof(buffer)) write.size = sizeof(buffer);
        for (i = 0U; i < write.size; ++i)
            buffer[i] = pattern(offset + (uint32_t)i);
        status = appSystemFlashWrite(&write);
        if (status != A_STATUS_OK) return status;
    }
    status = verify(address, info->erase_size, A_FALSE);
    if (status == A_STATUS_OK)
        ASHELL_REPLY("Flash test PASS: %lu bytes at 0x%08lX\r\n",
                     (unsigned long)info->erase_size, (unsigned long)address);
    return status;
}

static int command_flash(int argc, char **argv)
{
    aDevFlash25qInfo_t info;
    aStatus_t status;
    uint32_t address;
    aBool_t test;

    test = (argc == 3) && (strcmp(argv[1], "test") == 0);
    if (!test && !((argc == 2) && (strcmp(argv[1], "info") == 0))) {
        ASHELL_REPLY("flash info\r\n"
                     "flash test <address>: destroys one erase sector; "
                     "use an unused region\r\n");
        return -1;
    }
    status = appSystemFlashGetInfo(&info);
    if (status != A_STATUS_OK) goto failed;
    if (!test) {
        ASHELL_REPLY("JEDEC %02X %02X %02X, %lu bytes, erase %lu bytes\r\n",
                     (unsigned)info.manufacturer_id,
                     (unsigned)info.memory_type, (unsigned)info.capacity_id,
                     (unsigned long)info.capacity,
                     (unsigned long)info.erase_size);
        return 0;
    }
    status = address_parse(argv[2], &address);
    if (status == A_STATUS_OK) status = sector_test(address, &info);
    if (status == A_STATUS_OK) return 0;
failed:
    ASHELL_REPLY("Flash command failed: %d\r\n", (int)status);
    return -1;
}

ASHELL_CMD_EXPORT(flash, command_flash, "Flash info / destructive sector test");

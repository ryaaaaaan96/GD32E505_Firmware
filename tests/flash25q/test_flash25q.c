#include "aDev_flash25q_instance.h"
#include "aOS.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Byte-level SPI NOR model: the real device port and upstream SFUD run here. */
static uint8_t memory[8U * 1024U * 1024U];
static uint8_t wire_rx, command, wel;
static uint32_t address, tick;
static size_t position, page_start, program_count, allocation_count;
static int selected, pending, fail_io, stuck_busy, unknown_id;
static int fail_mutex, fail_lock;
static unsigned chip_transactions, abort_count;

void *aOSAlloc(size_t size)
{
    void *p = malloc(size);
    if (p) ++allocation_count;
    return p;
}
void aOSFree(void *p)
{
    if (p) { --allocation_count; free(p); }
}
aStatus_t aOSMutexCreate(aOSMutex_t *mutex)
{
    if (fail_mutex) return A_STATUS_NO_MEMORY;
    *mutex = aOSAlloc(sizeof(int));
    assert(*mutex);
    *(int *)*mutex = 0;
    return A_STATUS_OK;
}
void aOSMutexDestroy(aOSMutex_t *mutex)
{
    assert(*(int *)*mutex == 0);
    aOSFree(*mutex);
    *mutex = NULL;
}
aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout)
{
    (void)timeout;
    if (fail_lock) return A_STATUS_TIMEOUT;
    assert(mutex && !*(int *)mutex);
    *(int *)mutex = 1;
    return A_STATUS_OK;
}
aStatus_t aOSMutexUnlock(aOSMutex_t mutex)
{
    assert(mutex && *(int *)mutex);
    *(int *)mutex = 0;
    return A_STATUS_OK;
}
uint32_t aOSGetUptimeMs(void) { return tick; }
void aOSDelayMs(uint32_t ms) { tick += ms; }
aBool_t aOSPollWaitExpired(const aTimepoint_t *end)
{
    return aTimepointExpired(end, ++tick);
}

void aDrvSpiConfigStructInit(aDrvSpiConfig_t *c)
{
    memset(c, 0, sizeof(*c));
    c->mode = ADRV_SPI_MODE_MASTER;
    c->dataBits = 8;
    c->sckPin = c->mosiPin = c->misoPin = c->csPin = ADRV_PIN_NONE;
}
void aDrvSpiHandleStructInit(aDrvSpiHandle_t *h)
{
    memset(h, 0, sizeof(*h));
}
aStatus_t aDrvSpiInitStatic(const aDrvSpiConfig_t *c, aDrvSpiHandle_t *h)
{
    assert(c->spiId == ADRV_SPI_1);
    assert(c->csPin == ADRV_PIN_NONE);
    h->initialized = A_TRUE;
    return A_STATUS_OK;
}
aStatus_t aDrvSpiDeInitStatic(aDrvSpiHandle_t *h)
{
    h->initialized = A_FALSE;
    return A_STATUS_OK;
}
aStatus_t aDrvSpiAbort(aDrvSpiHandle_t *h)
{
    (void)h;
    pending = 0;
    ++abort_count;
    return A_STATUS_OK;
}
aStatus_t aDrvSpiIsComplete(aDrvSpiHandle_t *h, aBool_t *complete)
{
    assert(h->initialized);
    if (fail_io) return A_STATUS_ERROR;
    *complete = A_TRUE;
    return A_STATUS_OK;
}
void aDrvGpioConfigStructInit(aDrvGpioConfig_t *c)
{
    memset(c, 0, sizeof(*c));
}
aStatus_t aDrvGpioInit(const aDrvGpioConfig_t *c, aDrvGpioHandle_t *h)
{
    h->pin = c->pin;
    h->initialized = A_TRUE;
    assert(c->initial_level == ADRV_GPIO_HIGH);
    return A_STATUS_OK;
}
aStatus_t aDrvGpioDeInit(aDrvGpioHandle_t *h)
{
    h->initialized = A_FALSE;
    return A_STATUS_OK;
}
aStatus_t aDrvGpioWrite(const aDrvGpioHandle_t *h, aDrvGpioLevel_t level)
{
    assert(h->initialized);
    if (level == ADRV_GPIO_LOW) {
        assert(!selected && !pending);
        selected = 1;
        position = address = 0;
        command = 0;
        ++chip_transactions;
    } else if (selected) {
        assert(!pending);
        if (command == 0x06) wel = 2;
        if (command == 0x04 || command == 0x99) wel = 0;
        if (command == 0x02) { wel = 0; ++program_count; }
        if (command == 0x20 || command == 0xD8 || command == 0x52) {
            size_t n = command == 0x20 ? 4096U :
                       command == 0x52 ? 32768U : 65536U;
            assert(wel && position == 4 && address + n <= sizeof(memory));
            memset(memory + address, 0xFF, n);
            wel = 0;
        }
        selected = 0;
    }
    return A_STATUS_OK;
}
aStatus_t aDrvSpiTryWrite(aDrvSpiHandle_t *h, const void *data)
{
    uint8_t value = *(const uint8_t *)data;
    (void)h;
    assert(selected && !pending);
    wire_rx = 0xFF;
    if (position == 0) command = value;
    else if (command == 0x9F) {
        const uint8_t id[] = {0xEF, 0x40, 0x17};
        assert(position <= 3);
        wire_rx = unknown_id ? 0 : id[position - 1];
    } else if (command == 0x05) wire_rx = wel | (stuck_busy ? 1 : 0);
    else if (command == 0x03 || command == 0x02 || command == 0x20 ||
             command == 0xD8 || command == 0x52) {
        if (position <= 3) {
            address = (address << 8) | value;
            if (position == 3) page_start = address / 256;
        } else if (command == 0x03) wire_rx = memory[address++];
        else if (command == 0x02) {
            assert(wel && address / 256 == page_start);
            memory[address++] &= value;
        }
    }
    ++position;
    pending = 1;
    return A_STATUS_OK;
}
aStatus_t aDrvSpiTryRead(aDrvSpiHandle_t *h, void *data)
{
    (void)h;
    assert(selected && pending);
    *(uint8_t *)data = wire_rx;
    pending = 0;
    return A_STATUS_OK;
}

int main(void)
{
    aDevFlash25qBusConfig_t bc;
    aDevFlash25qBus_t bus;
    aDevFlash25qConfig_t config;
    aDevFlash25qHandle_t *handle;
#if ADEV_FLASH25Q_STATIC_ENABLE
    aDevFlash25qHandle_t instance;
#endif
    aDevFlash25qInfo_t info;
    aDevFlash25qReadRequest_t read;
    aDevFlash25qWriteRequest_t write;
    aDevFlash25qEraseRequest_t erase;
    uint8_t src[600], dst[600];
    size_t i;
    unsigned before;

    _Static_assert(ADRV_SPI_0 == 0 && ADRV_SPI_1 == 1 &&
                   ADRV_SPI_2 == 2, "SPI IDs match hardware");
    memset(memory, 0xFF, sizeof(memory));
    for (i = 0; i < sizeof(src); ++i) src[i] = (uint8_t)i;
    aDevFlash25qBusConfigStructInit(&bc);
    bc.spi.spiId = ADRV_SPI_1;
    fail_mutex = 1;
    assert(aDevFlash25qBusInitStatic(&bc, &bus) == A_STATUS_NO_MEMORY);
    assert(allocation_count == 0);
    fail_mutex = 0;
    assert(aDevFlash25qBusInitStatic(&bc, &bus) == A_STATUS_OK);
    aDevFlash25qConfigStructInit(&config);
    config.bus = &bus;
    config.cs_pin = ADRV_PIN(ADRV_GPIO_PORT_B, 12);
#if ADEV_FLASH25Q_DYNAMIC_ENABLE
    unknown_id = 1;
    assert(aDevFlash25qCreate(&config, &handle) == A_STATUS_NOT_FOUND);
    assert(handle == NULL && bus.references == 0 && allocation_count == 2);
    unknown_id = 0;
    config.expected_capacity = 123;
    assert(aDevFlash25qCreate(&config, &handle) == A_STATUS_UNSUPPORTED);
    assert(handle == NULL && allocation_count == 2);
    config.expected_capacity = 0;
    assert(aDevFlash25qCreate(&config, &handle) == A_STATUS_OK);
#else
    assert(aDevFlash25qInitStatic(&config, &instance) == A_STATUS_OK);
    handle = &instance;
#endif
#if ADEV_FLASH25Q_STATIC_ENABLE && ADEV_FLASH25Q_DYNAMIC_ENABLE
    config.cs_pin = ADRV_PIN(ADRV_GPIO_PORT_B, 11);
    assert(aDevFlash25qInitStatic(&config, &instance) == A_STATUS_OK);
    assert(bus.references == 2);
    assert(aDevFlash25qDestroy(&instance) == A_STATUS_INVALID_PARAM);
    assert(aDevFlash25qDeInitStatic(&instance) == A_STATUS_OK);
    assert(bus.references == 1);
#endif
    assert(aDevFlash25qGetInfo(handle, &info) == A_STATUS_OK);
    assert(info.capacity == sizeof(memory) && info.erase_size == 4096);
    assert(info.manufacturer_id == 0xEF);
    assert(aDevFlash25qBusDeInitStatic(&bus) == A_STATUS_BUSY);
    aDevFlash25qReadRequestStructInit(&read);
    aDevFlash25qWriteRequestStructInit(&write);
    aDevFlash25qEraseRequestStructInit(&erase);
    read.address = write.address = 250;
    read.size = write.size = sizeof(src);
    read.data = dst;
    write.data = src;
    assert(aDevFlash25qWrite(handle, &write) == A_STATUS_OK);
    assert(program_count == 4); /* 6 + 256 + 256 + 82 bytes. */
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_OK);
    assert(memcmp(src, dst, sizeof(src)) == 0);
    erase.size = 4096;
    erase.address = 1;
    before = chip_transactions;
    assert(aDevFlash25qErase(handle, &erase) == A_STATUS_INVALID_PARAM);
    assert(chip_transactions == before);
    erase.address = 0;
    assert(aDevFlash25qErase(handle, &erase) == A_STATUS_OK);
    for (i = 0; i < 4096; ++i) assert(memory[i] == 0xFF);
    read.address = UINT32_MAX;
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_INVALID_PARAM);
    read.address = 0;
    read.timeout = A_TIMEOUT_NO_WAIT;
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_UNSUPPORTED);
    read.timeout = A_TIMEOUT_MS(3);
    stuck_busy = 1;
    before = tick;
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_TIMEOUT);
    assert(tick - before == 3 && !selected && !bus.fault);
    stuck_busy = 0;
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_OK);
    fail_lock = 1;
    before = chip_transactions;
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_TIMEOUT);
    assert(before == chip_transactions);
    fail_lock = 0;
    fail_io = 1;
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_ERROR);
    assert(bus.fault && abort_count == 1 && !selected);
    fail_io = 0;
    assert(aDevFlash25qRead(handle, &read) == A_STATUS_NOT_READY);
#if ADEV_FLASH25Q_DYNAMIC_ENABLE
#if ADEV_FLASH25Q_STATIC_ENABLE
    assert(aDevFlash25qDeInitStatic(handle) == A_STATUS_INVALID_PARAM);
#endif
    assert(aDevFlash25qDestroy(handle) == A_STATUS_OK);
#else
    assert(aDevFlash25qDeInitStatic(handle) == A_STATUS_OK);
#endif
    assert(aDevFlash25qBusDeInitStatic(&bus) == A_STATUS_OK);
    assert(allocation_count == 0);
    puts("Flash25Q real SFUD + byte-level SPI tests passed");
    return 0;
}

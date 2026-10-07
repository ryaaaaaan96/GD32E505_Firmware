#ifndef TEST_MEMORY_FIXTURE_H
#define TEST_MEMORY_FIXTURE_H

static const aMemoryOps_t memory_ops = {
    .read = read_bytes, .write = write_bytes, .erase = erase_bytes
};
AMEMORY_DEVICE_DEFINE(memory_device,
    .name = "test-flash", .context = memory, .ops = &memory_ops,
    .geometry = {
        .capacity = sizeof(memory), .read_granularity = 1U,
        .write_granularity = 1U,
        .erase_granularity = AMEMORY_FLASH_BLOCK_SIZE,
        .program_bits = 1U, .erased_value = 0xFFU
    }
);
AMEMORY_PARTITION_DEFINE(param_part, AMEMORY_PART_PARAM_NAME, memory_device,
    AMEMORY_PART_PARAM_OFFSET, AMEMORY_PART_PARAM_SIZE, AMEMORY_ACCESS_ALL);
AMEMORY_PARTITION_DEFINE(log_part, AMEMORY_PART_LOG_NAME, memory_device,
    AMEMORY_PART_LOG_OFFSET, AMEMORY_PART_LOG_SIZE, AMEMORY_ACCESS_ALL);

static const aMemoryDevice_t *const memory_devices[] = { &memory_device };
static const aMemoryPartition_t *const memory_partitions[] = {
    &param_part, &log_part
};

static aStatus_t memory_start(void)
{
    aMemoryConfig_t config;

    aMemoryConfigStructInit(&config);
    config.devices = memory_devices;
    config.device_count = 1U;
    config.partitions = memory_partitions;
    config.partition_count = 2U;
    return aMemoryInit(&config);
}

#endif

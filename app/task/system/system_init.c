#include "system_init.h"
#include "app_config.h"
#include "aOS.h"
#include "system_device.h"
#include "protocol.h"
#if APP_DATABASE_ENABLE && !ADATABASE_DYNAMIC_ENABLE
#include "aDataBase_instance.h"
#if ABUS_ENABLE
#include "aMemory_layout.h"
#endif
#endif
#if APP_LOG_ENABLE
#include "aLog.h"
#endif
#if ASHELL_ENABLE
#include "aDrv_basic.h"
#include "aShell.h"
#include "aAtomic.h"
#endif

/* --------------------------------------------------------------------------
 * 系统状态指示灯：设备初始化与周期闪烁任务。
 * -------------------------------------------------------------------------- */
static const uint32_t status_blink_period_ms = 500U;

static void statusTask(void *argument)
{
    aDevLedHandle_t *led = argument;
    for (;;) {
        if (aDevLedToggle(led) != A_STATUS_OK) {
            (void)aDevLedOff(led);
            for (;;) {
                aOSDelayMs(status_blink_period_ms);
            }
        }
        aOSDelayMs(status_blink_period_ms);
    }
}

static aStatus_t statusInit(void)
{
    aOSTaskConfig_t task_config;
    aDevLedHandle_t *led = NULL;
    aStatus_t status;

    status = appSystemStatusLedInit(&led);
    if (status != A_STATUS_OK) {
        return status;
    }
    aOSTaskConfigStructInit(&task_config);
    task_config.name = "status";
    task_config.function = statusTask;
    task_config.stack_bytes = 256U;
    task_config.priority = AOS_TASK_PRIO_NORMAL;
    task_config.argument = led;
    status = aOSCreateTask(&task_config, NULL);
    if (status != A_STATUS_OK) {
        return status;
    }

    return A_STATUS_OK;
}

#if ASHELL_ENABLE || APP_LOG_ENABLE
/* 启动失败且消费者尚未放行时，按设备、功能状态的顺序清理。 */
static aStatus_t consoleCleanup(void)
{
    aStatus_t status = appSystemConsoleDeInit();
    if (status != A_STATUS_OK) return status;
#if ASHELL_ENABLE
    status = aShellDeInit();
    if (status == A_STATUS_NOT_READY) status = A_STATUS_OK;
#endif
    return status;
}
#endif

/* --------------------------------------------------------------------------
 * 系统控制台：Shell 初始化、输入处理与输出队列发送任务。
 * -------------------------------------------------------------------------- */
#if ASHELL_ENABLE

/* 初始化期间允许 Shell 文字入队，服务全部就绪后才消费并处理命令。 */
static aAtomicBool_t shell_services_ready = A_ATOMIC_VAR_INIT(0);

static void shellTask(void *argument)
{
    (void)argument;
    while (!aAtomicLoadExplicit(&shell_services_ready,
                               A_ATOMIC_ACQUIRE)) {
        aOSDelayMs(1U);
    }
    for (;;) {
        /* Process 同时消费输出；无输入或 I/O 错误时短暂退避。 */
        if (aShellProcess() != A_STATUS_OK) {
            aOSDelayMs(1U);
        }
    }
}

static aStatus_t shellInit(void)
{
    aShellConfig_t config;
    aOSTaskConfig_t task_config;
    aStatus_t status;
    aStatus_t cleanup_status;
    uint32_t core_clock_hz;

    appSystemShellConfigInit(&config);
    status = aShellInit(&config);
    if (status != A_STATUS_OK) {
        cleanup_status = consoleCleanup();
        return cleanup_status == A_STATUS_OK ? status : cleanup_status;
    }

    core_clock_hz = aDrvGetCoreClockHz();
    ASHELL_PRINT("\r\nsystem clock: %lu Hz\r\n",
                (unsigned long)core_clock_hz);
    ASHELL_PRINT("system peripherals initialized\r\n");

    aOSTaskConfigStructInit(&task_config);
    task_config.name = "shell";
    task_config.function = shellTask;
    task_config.stack_bytes = 2048U;
    task_config.priority = AOS_TASK_PRIO_LOW;
    status = aOSCreateTask(&task_config, NULL);
    if (status != A_STATUS_OK) {
        cleanup_status = consoleCleanup();
        if (cleanup_status != A_STATUS_OK) return cleanup_status;
    }
    return status;
}

#endif

/* --------------------------------------------------------------------------
 * 系统存储：装配设备和区域配置，由 aMemory 统一初始化，不创建任务。
 * -------------------------------------------------------------------------- */
#if APP_MEMORY_ENABLE

static aStatus_t memoryInit(void)
{
    aMemoryConfig_t config;
    aStatus_t status;

    appSystemMemoryConfigInit(&config);
    status = aMemoryInit(&config);
    return status;
}

#endif

/* --------------------------------------------------------------------------
 * 系统日志：配置装配、初始化与启动日志，不创建独立任务。
 * -------------------------------------------------------------------------- */
#if APP_LOG_ENABLE

static aStatus_t logInit(void)
{
    aLogConfig_t config;
    aStatus_t status;
    aStatus_t cleanup_status;

    /* 控制台已就绪；日志直接提交串口，不受 Shell 消费者门控影响。 */
    appSystemLogConfigInit(&config);
    status = aLogInit(&config);
    if (status != A_STATUS_OK) {
        /* 日志尚未就绪，Shell 仍在等待；此时可清理未使用的控制台。 */
        cleanup_status = consoleCleanup();
        if (cleanup_status != A_STATUS_OK) return cleanup_status;
        return status;
    }
    /* 初始化成功不以启动日志是否成功提交串口作为判据。 */
    (void)ALOG_INFO("system", "EasyLogger ready");
    return A_STATUS_OK;
}

#endif

/* --------------------------------------------------------------------------
 * 系统数据总线：私有实例、点表挂载及通用访问，不创建任务。
 * -------------------------------------------------------------------------- */
#if ABUS_ENABLE

/* 应用单例只在本文件持有，产品点表由 protocol_tables.c 配置。 */
static aBusHandle_t *bus_handle;

static aStatus_t busInit(void)
{
    dataBusConfig_t config;
    aStatus_t status;

    if (bus_handle != NULL) return A_STATUS_BUSY;
    status = protocolBusConfigInit(&config);
    if (status != A_STATUS_OK) return status;
    if (config.tables == NULL || config.table_count == 0U) {
        return A_STATUS_INVALID_PARAM;
    }
#if ABUS_DYNAMIC_ENABLE
    status = aBusCreate(config.instanceID, config.tables,
                        config.table_count, &bus_handle);
#else
    if (config.instance == NULL) return A_STATUS_INVALID_PARAM;
    status = aBusInitStatic(config.instanceID, config.tables,
                           config.table_count, config.instance);
    if (status == A_STATUS_OK) bus_handle = config.instance;
#endif
#if ASHELL_ENABLE
    if (status == A_STATUS_OK) {
        ASHELL_PRINT("Data bus: %lu tables ready\r\n",
                     (unsigned long)config.table_count);
    }
#endif
    return status;
}

aStatus_t dataBusResolveKey(const aBusSigKeyQuery_t *query,
    size_t *sigIndex)
{
    if (query == NULL || sigIndex == NULL) return A_STATUS_INVALID_PARAM;
    if (bus_handle == NULL) return A_STATUS_NOT_READY;
    return aBusResolveKey(bus_handle, query, sigIndex);
}

aStatus_t dataBusGetInfo(const aBusSigQuery_t *query, aBusSigInfo_t *info)
{
    if (query == NULL || info == NULL) return A_STATUS_INVALID_PARAM;
    if (bus_handle == NULL) return A_STATUS_NOT_READY;
    return aBusGetSigInfo(bus_handle, query, info);
}

aStatus_t dataBusSet(const aBusSetIndexRequest_t *request)
{
    aBusSigQuery_t query;
    aBusSigInfo_t info;
    aStatus_t status;

    if (request == NULL || request->src == NULL ||
        !aTimeoutIsValid(request->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    query.deviceID = request->deviceID;
    query.sigIndex = request->sigIndex;
    status = dataBusGetInfo(&query, &info);
    if (status != A_STATUS_OK) return status;
    if (request->size != info.size) {
        return A_STATUS_INVALID_PARAM;
    }
    return aBusSetByIndex(bus_handle, request);
}

aStatus_t dataBusGet(const aBusGetIndexRequest_t *request)
{
    aBusSigQuery_t query;
    aBusSigInfo_t info;
    aStatus_t status;

    if (request == NULL || request->dst == NULL ||
        !aTimeoutIsValid(request->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    query.deviceID = request->deviceID;
    query.sigIndex = request->sigIndex;
    status = dataBusGetInfo(&query, &info);
    if (status != A_STATUS_OK) return status;
    if (request->size != info.size) {
        return A_STATUS_INVALID_PARAM;
    }
    return aBusGetByIndex(bus_handle, request);
}

aStatus_t dataBusSetParam(const aBusSetParamRequest_t *request)
{
    if (bus_handle == NULL) return A_STATUS_NOT_READY;
    return aBusSetParam(bus_handle, request);
}

aStatus_t dataBusGetParam(const aBusGetParamRequest_t *request)
{
    if (bus_handle == NULL) return A_STATUS_NOT_READY;
    return aBusGetParam(bus_handle, request);
}

#endif

/* --------------------------------------------------------------------------
 * 系统数据库：私有 KV/TSDB 实例、初始化及通用访问，不创建任务。
 * -------------------------------------------------------------------------- */
#if APP_DATABASE_ENABLE

static aDataBaseKvHandle_t *kv_handle;
static aDataBaseTsHandle_t *ts_handle;
#if !ADATABASE_DYNAMIC_ENABLE
static aDataBaseKvHandle_t kv_instance;
static aDataBaseTsHandle_t ts_instance;
#if ABUS_ENABLE
/* 数据库索引属于存储服务；点表只提供选择清单及其编译期数量。
 * SIG 偏移按持久化项计数，扇区缓存按实际参数分区容量计数。
 */
#if PROTOCOL_PERSIST_SIG_COUNT > 0
static uint32_t persistent_offsets[PROTOCOL_PERSIST_SIG_COUNT];
#endif
static struct kvdb_sec_info persistent_sectors[
    AMEMORY_PART_PARAM_SIZE / AMEMORY_FLASH_BLOCK_SIZE];
static aDataBaseKvIndexStorage_t persistent_index = {
#if PROTOCOL_PERSIST_SIG_COUNT > 0
    .sig_offsets = persistent_offsets,
#endif
    .sig_capacity = PROTOCOL_PERSIST_SIG_COUNT,
    .sectors = persistent_sectors,
    .sector_capacity =
        sizeof(persistent_sectors) / sizeof(persistent_sectors[0])
};
#endif
#endif

aStatus_t appDatabaseOpen(aBool_t format_if_needed)
{
    aDataBaseKvConfig_t kv_config;
    aDataBaseTsConfig_t ts_config;
    aStatus_t status;
    aStatus_t cleanup_status;

    if (kv_handle != NULL || ts_handle != NULL) return A_STATUS_BUSY;
    appSystemDatabaseKvConfigInit(&kv_config);
#if ABUS_ENABLE
    protocolPersistenceConfigInit(&kv_config);
#if !ADATABASE_DYNAMIC_ENABLE
    kv_config.index_storage = &persistent_index;
#endif
#endif
    kv_config.format_if_needed = format_if_needed;
#if ADATABASE_DYNAMIC_ENABLE
    status = aDataBaseKvCreate(&kv_config, &kv_handle);
#else
    status = aDataBaseKvInitStatic(&kv_config, &kv_instance);
    if (status == A_STATUS_OK) kv_handle = &kv_instance;
#endif
    if (status != A_STATUS_OK) return status;

    appSystemDatabaseTsConfigInit(&ts_config);
    ts_config.format_if_needed = format_if_needed;
#if ADATABASE_DYNAMIC_ENABLE
    status = aDataBaseTsCreate(&ts_config, &ts_handle);
#else
    status = aDataBaseTsInitStatic(&ts_config, &ts_instance);
    if (status == A_STATUS_OK) ts_handle = &ts_instance;
#endif
    if (status != A_STATUS_OK) {
        cleanup_status = appDatabaseClose();
        return cleanup_status == A_STATUS_OK ? status : cleanup_status;
    }
    return A_STATUS_OK;
}

aStatus_t appDatabaseClose(void)
{
    aStatus_t status;

    if (ts_handle != NULL) {
#if ADATABASE_DYNAMIC_ENABLE
        status = aDataBaseTsDestroy(ts_handle);
#else
        status = aDataBaseTsDeInitStatic(ts_handle);
#endif
        if (status != A_STATUS_OK) return status;
        ts_handle = NULL;
    }
    if (kv_handle != NULL) {
#if ADATABASE_DYNAMIC_ENABLE
        status = aDataBaseKvDestroy(kv_handle);
#else
        status = aDataBaseKvDeInitStatic(kv_handle);
#endif
        if (status != A_STATUS_OK) return status;
        kv_handle = NULL;
    }
    return A_STATUS_OK;
}

#if ABUS_ENABLE
/* 应用参数的显式保存/恢复；仅持久化清单中的项参与操作。
 * 默认值由 aBus 创建时设置；缺少记录或旧记录长度不符时保留 RAM。
 * 长度不符的旧记录通过 FlashDB 逻辑删除；范围或存储错误仍拒绝恢复。
 * 各 SIG 独立提交，不承诺整张表或多条 Flash 记录的事务性。
 */
static aStatus_t persist_one(const aBusSigKeyQuery_t *key, aBool_t save)
{
    uint8_t buffer[256U];
    aBusSigQuery_t query = {.deviceID = key->deviceID};
    aBusSigInfo_t info;
    aStatus_t status;

    status = dataBusResolveKey(key, &query.sigIndex);
    if (status != A_STATUS_OK) return status;
    status = dataBusGetInfo(&query, &info);
    if (status != A_STATUS_OK) return status;
    if (info.size > sizeof(buffer)) return A_STATUS_NO_MEMORY;
    if (save) {
        const aBusGetIndexRequest_t read = {
            .deviceID = query.deviceID, .sigIndex = query.sigIndex,
            .dst = buffer, .size = info.size, .timeout = A_TIMEOUT_MS(100U)
        };
        const aDataBaseSigSetRequest_t write = {
            .deviceID = query.deviceID, .sigIndex = query.sigIndex,
            .data = buffer, .size = info.size, .timeout = A_TIMEOUT_MS(5000U)
        };
        status = dataBusGet(&read);
        if (status == A_STATUS_OK) status = aDataBaseSigSet(kv_handle, &write);
    } else {
        const aDataBaseSigGetRequest_t read = {
            .deviceID = query.deviceID, .sigIndex = query.sigIndex,
            .data = buffer, .size = info.size, .timeout = A_TIMEOUT_MS(5000U)
        };
        const aBusSetIndexRequest_t write = {
            .deviceID = query.deviceID, .sigIndex = query.sigIndex,
            .src = buffer, .size = info.size, .timeout = A_TIMEOUT_MS(100U)
        };
        status = aDataBaseSigGet(kv_handle, &read);
        if (status == A_STATUS_DATA_MISMATCH) {
            const aDataBaseSigDeleteRequest_t discard = {
                .deviceID = query.deviceID, .sigIndex = query.sigIndex,
                .timeout = A_TIMEOUT_MS(5000U)
            };
            /* 只作废不兼容记录，不把默认值写回 Flash；删除失败必须上报。 */
            status = aDataBaseSigDelete(kv_handle, &discard);
            return status == A_STATUS_OK ? A_STATUS_NOT_FOUND : status;
        }
        if (status == A_STATUS_OK) status = dataBusSet(&write);
    }
    return status;
}

static aStatus_t persist(const aBusSigKeyQuery_t *key, aBool_t save)
{
    aDataBaseKvConfig_t config;
    aStatus_t status;

    if (bus_handle == NULL || kv_handle == NULL) return A_STATUS_NOT_READY;
    aDataBaseKvConfigStructInit(&config);
    protocolPersistenceConfigInit(&config);
    for (size_t i = 0U; i < config.persist_count; i++) {
        aBusSigKeyQuery_t item = {
            config.persist_sigs[i].deviceID, config.persist_sigs[i].sigKey
        };
        if (key != NULL && (key->deviceID != item.deviceID ||
                            key->sigKey != item.sigKey)) continue;
        status = persist_one(&item, save);
        if (status != A_STATUS_OK &&
            !(key == NULL && !save && status == A_STATUS_NOT_FOUND)) {
            return status;
        }
        if (key != NULL) return status;
    }
    return key == NULL ? A_STATUS_OK : A_STATUS_NOT_FOUND;
}

aStatus_t dataBusSave(const aBusSigKeyQuery_t *key)
{
    return persist(key, A_TRUE);
}

aStatus_t dataBusRestore(const aBusSigKeyQuery_t *key)
{
    return persist(key, A_FALSE);
}
#endif

static aStatus_t databaseInit(void)
{
    aStatus_t status;

    status = aDataBaseInit();
    if (status != A_STATUS_OK) return status;
    /* 启动不允许因空白或损坏的扇区头而自动格式化。 */
    status = appDatabaseOpen(A_FALSE);
#if ABUS_ENABLE
    if (status == A_STATUS_OK) {
        status = dataBusRestore(NULL);
        if (status != A_STATUS_OK) return status;
    }
#endif
#if ASHELL_ENABLE
    if (status == A_STATUS_OK)
        ASHELL_PRINT("Database: KV and TSDB ready\r\n");
    else if (status == A_STATUS_NOT_READY)
        ASHELL_PRINT("Database: not formatted; run 'db init'\r\n");
    else
        ASHELL_PRINT("Database init failed: %d\r\n", (int)status);
#endif
    return status == A_STATUS_NOT_READY ? A_STATUS_OK : status;
}

aStatus_t appDatabaseKvSet(const aDataBaseKvSetRequest_t *request)
{
    if (kv_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseKvSet(kv_handle, request);
}

aStatus_t appDatabaseKvGet(const aDataBaseKvGetRequest_t *request)
{
    if (kv_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseKvGet(kv_handle, request);
}

aStatus_t appDatabaseKvDelete(const aDataBaseKvDeleteRequest_t *request)
{
    if (kv_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseKvDelete(kv_handle, request);
}

aStatus_t appDatabaseTsAppend(const aDataBaseTsAppendRequest_t *request)
{
    if (ts_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseTsAppend(ts_handle, request);
}

aStatus_t appDatabaseTsIterate(const aDataBaseTsIterateRequest_t *request)
{
    if (ts_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseTsIterate(ts_handle, request);
}

aStatus_t appDatabaseTsGetInfo(aDataBaseTsInfo_t *info)
{
    if (ts_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseTsGetInfo(ts_handle, info);
}

#endif

/* --------------------------------------------------------------------------
 * 系统初始化编排：各功能内部完成具体配置，服务就绪后允许 Shell 任务处理命令。
 * -------------------------------------------------------------------------- */
aStatus_t aSystemInit(void)
{
    aStatus_t status;

    status = statusInit();
    if (status != A_STATUS_OK) {
        return status;
    }

#if ASHELL_ENABLE || APP_LOG_ENABLE
    status = appSystemConsoleInit();
    if (status != A_STATUS_OK) return status;
#endif

#if ASHELL_ENABLE
    status = shellInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if APP_MEMORY_ENABLE
    status = memoryInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if APP_LOG_ENABLE
    status = logInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if ABUS_ENABLE
    status = busInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if APP_DATABASE_ENABLE
    status = databaseInit();
    if (status != A_STATUS_OK) {
        return status;
    }
#endif

#if ABUS_ENABLE
    /* 数据库已恢复持久化参数，协议借用同一总线后再启动生产者。 */
    status = protocolInit(bus_handle);
#else
    status = protocolInit(NULL);
#endif
    if (status != A_STATUS_OK) {
        return status;
    }

#if ASHELL_ENABLE
    /* 发布服务初始化结果，允许已创建的 Shell 任务开始处理命令。 */
    aAtomicStoreExplicit(&shell_services_ready, 1, A_ATOMIC_RELEASE);
#endif

    return A_STATUS_OK;
}

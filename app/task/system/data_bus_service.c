#include "data_bus_service.h"
#if APP_MODBUS_ENABLE
#include "data_bus_modbus.h"
#endif

/* 应用单例只在本文件持有，产品点表由 protocol 层配置。 */
static aBusHandle_t *bus_handle;

aStatus_t dataBusInit(const dataBusConfig_t *config)
{
#if !ABUS_DYNAMIC_ENABLE
    aStatus_t status;
#endif

    if (bus_handle != NULL) return A_STATUS_BUSY;
    if (config == NULL || config->tables == NULL ||
        config->table_count == 0U) return A_STATUS_INVALID_PARAM;
#if ABUS_DYNAMIC_ENABLE
    return aBusCreate(config->instanceID, config->tables,
                      config->table_count, &bus_handle);
#else
    if (config->instance == NULL) return A_STATUS_INVALID_PARAM;
    status = aBusInitStatic(config->instanceID, config->tables,
                           config->table_count, config->instance);
    if (status == A_STATUS_OK) bus_handle = config->instance;
    return status;
#endif
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

#if APP_MODBUS_ENABLE
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t dataBusModbusCreate(const aModbusConfig_t *config,
                                   aModbusHandle_t **handle_out)
{
    aModbusConfig_t bound;

    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    if (bus_handle == NULL) return A_STATUS_NOT_READY;
    bound = *config;
    bound.bus = bus_handle;
    return aModbusCreate(&bound, handle_out);
}
#endif

#if AMODBUS_STATIC_ENABLE
aStatus_t dataBusModbusInitStatic(const aModbusConfig_t *config,
                                       aModbusHandle_t *handle)
{
    aModbusConfig_t bound;

    if (config == NULL || handle == NULL) return A_STATUS_INVALID_PARAM;
    if (bus_handle == NULL) return A_STATUS_NOT_READY;
    bound = *config;
    bound.bus = bus_handle;
    return aModbusInitStatic(&bound, handle);
}
#endif
#endif

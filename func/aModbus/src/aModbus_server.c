#include "aModbus_internal.h"
#include <string.h>

void aModbusServerProcessRequestStructInit(
    aModbusServerProcessRequest_t *request)
{
    if (request != NULL) {
        const aModbusServerProcessRequest_t defaults = {
            .timeout = A_TIMEOUT_MS(100U)
        };
        *request = defaults;
    }
}


#if AMODBUS_SERVER_ENABLE
static nmbs_error server_status(aModbusHandle_t *handle, aStatus_t status)
{
    nmbs_error exception;
    switch (status) {
    case A_STATUS_OK: return NMBS_ERROR_NONE;
    case A_STATUS_NOT_FOUND:
        exception = NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        break;
    case A_STATUS_INVALID_PARAM:
        exception = NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        break;
    case A_STATUS_UNSUPPORTED:
        exception = NMBS_EXCEPTION_ILLEGAL_FUNCTION;
        break;
    default:
        exception = NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        break;
    }
    handle->exception = (uint8_t)exception;
    return exception;
}

static nmbs_error server_read(aModbusHandle_t *handle, aModbusArea_t area,
    uint16_t address, uint16_t quantity, void *data, size_t size)
{
    const aModbusAddressReadRequest_t request = {
        .area = area, .address = address, .quantity = quantity,
        .data = data, .size = size, .timeout = aModbusRemaining(handle)
    };
    return server_status(handle,
        handle->server.read == NULL ? A_STATUS_UNSUPPORTED :
        handle->server.read(handle->server.context, &request));
}

static nmbs_error server_write(aModbusHandle_t *handle, aModbusArea_t area,
    uint16_t address, uint16_t quantity, const void *data, size_t size)
{
    const aModbusAddressWriteRequest_t request = {
        .area = area, .address = address, .quantity = quantity,
        .data = data, .size = size, .timeout = aModbusRemaining(handle)
    };
    return server_status(handle,
        handle->server.write == NULL ? A_STATUS_UNSUPPORTED :
        handle->server.write(handle->server.context, &request));
}

static nmbs_error read_coils(uint16_t address, uint16_t quantity,
    nmbs_bitfield data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_COILS, address, quantity,
                       data, NMBS_BITFIELD_BYTES_MAX);
}

static nmbs_error read_discrete(uint16_t address, uint16_t quantity,
    nmbs_bitfield data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_DISCRETE_INPUTS,
                       address, quantity, data, NMBS_BITFIELD_BYTES_MAX);
}

static nmbs_error read_holding(uint16_t address, uint16_t quantity,
    uint16_t *data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_HOLDING_REGISTERS,
                       address, quantity, data, quantity * sizeof(*data));
}

static nmbs_error read_inputs(uint16_t address, uint16_t quantity,
    uint16_t *data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_INPUT_REGISTERS,
                       address, quantity, data, quantity * sizeof(*data));
}

static nmbs_error write_coil(uint16_t address, bool value,
    uint8_t unit, void *context)
{
    uint8_t data = value ? 1U : 0U;
    (void)unit;
    return server_write(context, AMODBUS_AREA_COILS, address, 1U,
                        &data, sizeof(data));
}

static nmbs_error write_register(uint16_t address, uint16_t value,
    uint8_t unit, void *context)
{
    (void)unit;
    return server_write(context, AMODBUS_AREA_HOLDING_REGISTERS,
                        address, 1U, &value, sizeof(value));
}

static nmbs_error write_coils(uint16_t address, uint16_t quantity,
    const nmbs_bitfield data, uint8_t unit, void *context)
{
    (void)unit;
    return server_write(context, AMODBUS_AREA_COILS, address, quantity,
                        data, NMBS_BITFIELD_BYTES_MAX);
}

static nmbs_error write_registers(uint16_t address, uint16_t quantity,
    const uint16_t *data, uint8_t unit, void *context)
{
    (void)unit;
    return server_write(context, AMODBUS_AREA_HOLDING_REGISTERS,
                        address, quantity, data, quantity * sizeof(*data));
}
aStatus_t aModbusServerProcess(aModbusHandle_t *handle,
    const aModbusServerProcessRequest_t *request)
{
    aStatus_t status;
    nmbs_error error;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    status = aModbusOperationBegin(handle, AMODBUS_ROLE_SERVER,
                              request->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    if (handle->recover_input) {
        aModbusTransportDiscard(&handle->core, handle);
        if (handle->io_status != A_STATUS_OK) {
            return aModbusOperationEnd(handle, NMBS_ERROR_TRANSPORT,
                                  request->result);
        }
        handle->recover_input = A_FALSE;
    }
    error = nmbs_server_poll(&handle->core);
    return aModbusOperationEnd(handle, error, request->result);
}

#endif

void aModbusServerConfigStructInit(aModbusServerConfig_t *config)
{
    if (config != NULL) {
        const aModbusServerConfig_t defaults = {.unit_id = 1U};
        *config = defaults;
        aModbusConfigStructInit(&config->modbus);
    }
}

#if AMODBUS_SERVER_ENABLE
void aModbusServerCallbacks(aModbusHandle_t *handle, nmbs_callbacks *callbacks)
{
    nmbs_callbacks_create(callbacks);
    callbacks->read_coils = read_coils;
    callbacks->read_discrete_inputs = read_discrete;
    callbacks->read_holding_registers = read_holding;
    callbacks->read_input_registers = read_inputs;
    callbacks->write_single_coil = write_coil;
    callbacks->write_single_register = write_register;
    callbacks->write_multiple_coils = write_coils;
    callbacks->write_multiple_registers = write_registers;
    callbacks->arg = handle;
}

aStatus_t aModbusServerConfigCheck(const aModbusServerConfig_t *config)
{
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aModbusConfigCheck(&config->modbus);
    if (status != A_STATUS_OK) return status;
    if ((config->modbus.transport_type == AMODBUS_TRANSPORT_RTU &&
         (config->unit_id == 0U || config->unit_id > 247U)) ||
        (config->read == NULL && config->write == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}
static aStatus_t server_init(const aModbusServerConfig_t *config,
    aModbusHandle_t *handle)
{
    aStatus_t status = aModbusStoragePrepare(handle);
    if (status != A_STATUS_OK) return status;
    handle->server = *config;
    return aModbusCoreInit(handle, AMODBUS_ROLE_SERVER, config->unit_id);
}

#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusServerInitStatic(const aModbusServerConfig_t *config,
    aModbusHandle_t *handle)
{
    aStatus_t status = aModbusServerConfigCheck(config);
    return status == A_STATUS_OK ? server_init(config, handle) : status;
}
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusServerCreate(const aModbusServerConfig_t *config,
    aModbusHandle_t **handle_out)
{
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    aStatus_t status = aModbusServerConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    aModbusHandle_t *handle = aModbusAllocate();
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = server_init(config, handle);
    if (status != A_STATUS_OK) aOSFree(handle);
    else {
        handle->dynamic = A_TRUE;
        *handle_out = handle;
    }
    return status;
}
#endif
#endif

#include "app_sig.h"
#if !ABUS_DYNAMIC_ENABLE
#include "aBus_instance.h"
#endif
#include <stddef.h>

static const appBusMotor_t motor_default = {100U, 25};
static const aBusParam_t motor_params[] = {
    {
        .offset = offsetof(appBusMotor_t, speed),
        .type = ALIB_DATA_U32,
        .min.u32 = 0U,
        .max.u32 = 6000U
    }
};

static const aBusSig_t data_sigs[APP_BUS_SIG_COUNT] = {
    [APP_BUS_MOTOR] = {
        .sigKey = APP_BUS_MOTOR_KEY,
        .flags = ABUS_SIG_FLAG_LOCK,
        .size = sizeof(appBusMotor_t),
        .default_data = &motor_default,
        .params = motor_params,
        .param_count = sizeof(motor_params) / sizeof(motor_params[0])
    },
    [APP_BUS_COUNTER] = {
        .sigKey = APP_BUS_COUNTER_KEY,
        .flags = ABUS_SIG_FLAG_LOCK,
        .size = sizeof(uint32_t)
    }
};

/* 存储方式由本模块选择，对外只提供业务读写接口。 */
static const aBusTable_t app_sig_table = {
    .sigs = data_sigs,
    .sig_count = APP_BUS_SIG_COUNT,
    .deviceID = APP_SIG_DEVICE_ID
};
static aBusHandle_t *sig_handle;
static appBusMotor_t motor_storage;
APP_SIG_BIND(motor_binding, APP_BUS_MOTOR, motor_storage);

#if !ABUS_DYNAMIC_ENABLE
static aBusHandle_t static_handle;
static aBusSigState_t static_states[APP_BUS_SIG_COUNT];
#endif

/* 启动时由 aSystemInit 调用一次；实例在应用运行期间持续存在。 */
aStatus_t appSigInit(void)
{
    aStatus_t status;
#if ABUS_DYNAMIC_ENABLE
    status = aBusCreate(&app_sig_table, &sig_handle);
#else
    aBusInstanceStructInit(&static_handle, static_states,
                          APP_BUS_SIG_COUNT);
    status = aBusInitStatic(&app_sig_table, &static_handle);
    if (status == A_STATUS_OK) sig_handle = &static_handle;
#endif
    return status;
}

aStatus_t appSigSet(const aBusSetIndexRequest_t *request)
{
    if (request == NULL || request->src == NULL ||
        !aTimeoutIsValid(request->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (sig_handle == NULL) return A_STATUS_NOT_READY;
    if (request->sigIndex >= app_sig_table.sig_count) {
        return A_STATUS_NOT_FOUND;
    }
    if (request->size != data_sigs[request->sigIndex].size) {
        return A_STATUS_INVALID_PARAM;
    }
    return aBusSetByIndex(sig_handle, request);
}

aStatus_t appSigGet(const aBusGetIndexRequest_t *request)
{
    if (request == NULL || request->dst == NULL ||
        !aTimeoutIsValid(request->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (sig_handle == NULL) return A_STATUS_NOT_READY;
    if (request->sigIndex >= app_sig_table.sig_count) {
        return A_STATUS_NOT_FOUND;
    }
    if (request->size != data_sigs[request->sigIndex].size) {
        return A_STATUS_INVALID_PARAM;
    }
    return aBusGetByIndex(sig_handle, request);
}

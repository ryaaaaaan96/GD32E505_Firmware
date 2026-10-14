/* 自动生成：只读定义和静态兜底存储，不创建任务或总线实例。 */
#include "../inc/IDU_sig_table.h"
#include "../inc/FAN_sig_table.h"

#define ABUS_BOUND_SIG(...) ABUS_SIG(__VA_ARGS__)
#define ABUS_SIG(...)
#define ABUS_PARAM(name, ...) [name] = {__VA_ARGS__},
#define ABUS_PARAMS(group, ...) \
    static const aBusParam_t group##_params[] = {__VA_ARGS__};
#include "../sig/IDU_test.inc"
#include "../sig/FAN_test.inc"
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG

#define ABUS_PARAMS(...)
#define ABUS_PARAM_REF(group) \
    .params = group##_params, \
    .param_count = sizeof(group##_params) / sizeof(group##_params[0])
#define ABUS_SIG(name, key, value_type, ...) \
    {.sigKey = (key), .size = sizeof(value_type), __VA_ARGS__},
static const aBusSig_t IDU_sigs[] = {
#include "../sig/IDU_test.inc"
};
static const aBusSig_t FAN_sigs[] = {
#include "../sig/FAN_test.inc"
};
#undef ABUS_SIG
#undef ABUS_PARAM_REF
#undef ABUS_PARAMS
#undef ABUS_BOUND_SIG

static const aBusTable_t tables[] = {
    {.sigs = IDU_sigs, .sig_count = IDU_SIG_COUNT,
     .deviceID = IDU_SIG_DEVICE_ID},
    {.sigs = FAN_sigs, .sig_count = FAN_SIG_COUNT,
     .deviceID = FAN_SIG_DEVICE_ID},
};

#if !ABUS_DYNAMIC_ENABLE
/* 已声明 bound 的项仍由应用持有，不重复注册。 */
#define ABUS_BOUND_SIG(...)
#define ABUS_PARAMS(...)
#define ABUS_SIG(name, key, value_type, ...) \
    static value_type IDU_##name##_storage; \
    ABUS_RAM_BIND_EXPORT(IDU_##name##_binding, \
        PROTOCOL_BUS_INSTANCE_ID, IDU_SIG_DEVICE_ID, \
        IDU_SIG_##name, IDU_##name##_storage);
#include "../sig/IDU_test.inc"
#undef ABUS_SIG
#define ABUS_SIG(name, key, value_type, ...) \
    static value_type FAN_##name##_storage; \
    ABUS_RAM_BIND_EXPORT(FAN_##name##_binding, \
        PROTOCOL_BUS_INSTANCE_ID, FAN_SIG_DEVICE_ID, \
        FAN_SIG_##name, FAN_##name##_storage);
#include "../sig/FAN_test.inc"
#undef ABUS_SIG
#undef ABUS_PARAMS
#undef ABUS_BOUND_SIG
#endif
